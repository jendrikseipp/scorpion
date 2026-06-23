#ifndef UTILS_CPYTHON_RANDOM_H
#define UTILS_CPYTHON_RANDOM_H

#include <cstddef>
#include <cstdint>

/*
  SELF-CONTAINED, REMOVABLE MODULE.

  A bit-exact reimplementation of CPython's `random.Random` for the subset
  the translator uses: integer seeding + `randrange(n)` (== `_randbelow`).
  Its purpose is to let the C++ translator draw the *same* pseudo-random
  sequence as the Python translator's invariant balance checker, so that the
  two produce byte-identical SAS+ output on instances where the randomized
  invariant search otherwise picks a different (but equally valid) mutex
  grouping. See invariants/invariant_finder.cc (BalanceChecker::next_index)
  and the `--no-cpython-rng` flag.

  Both CPython and std::mt19937 use the MT19937 core, but CPython differs in
  (a) seeding (Knuth's init_by_array on the seed split into 32-bit words) and
  (b) the integer-in-range method (getrandbits + rejection, vs the
  implementation-defined std::uniform_int_distribution). This class matches
  both, reproducing CPython's _randommodule.c exactly.

  To remove this feature later: delete this header, the `cpython_rng` option,
  the `--no-cpython-rng` flag, and the cpython_random_ member + dispatch in
  BalanceChecker (revert next_index to the std::mt19937 path).
*/
namespace translate::utils {

class CPythonRandom {
public:
    explicit CPythonRandom(std::uint32_t seed) {
        // CPython's Random.seed(int) uses init_by_array on abs(seed) split
        // into 32-bit little-endian words; a seed < 2^32 is a 1-word key.
        std::uint32_t key[1] = {seed};
        init_by_array(key, 1);
    }

    // Equivalent to CPython's random.randrange(n) / _randbelow(n).
    std::uint64_t randbelow(std::uint64_t n) {
        if (n == 0) return 0;
        int k = bit_length(n);
        std::uint64_t r = getrandbits(k);
        while (r >= n) r = getrandbits(k);
        return r;
    }

private:
    static constexpr int N = 624;
    static constexpr int M = 397;
    static constexpr std::uint32_t MATRIX_A = 0x9908b0dfU;
    static constexpr std::uint32_t UPPER_MASK = 0x80000000U;
    static constexpr std::uint32_t LOWER_MASK = 0x7fffffffU;

    std::uint32_t mt_[N];
    int mti_ = N + 1;

    static int bit_length(std::uint64_t n) {
        int b = 0;
        while (n) { ++b; n >>= 1; }
        return b;
    }

    void init_genrand(std::uint32_t s) {
        mt_[0] = s;
        for (mti_ = 1; mti_ < N; ++mti_)
            mt_[mti_] = 1812433253U * (mt_[mti_ - 1] ^ (mt_[mti_ - 1] >> 30))
                        + static_cast<std::uint32_t>(mti_);
    }

    void init_by_array(const std::uint32_t *init_key, std::size_t key_length) {
        init_genrand(19650218U);
        std::size_t i = 1, j = 0;
        std::size_t k = (N > key_length ? N : key_length);
        for (; k; --k) {
            mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1664525U))
                     + init_key[j] + static_cast<std::uint32_t>(j);
            ++i; ++j;
            if (i >= N) { mt_[0] = mt_[N - 1]; i = 1; }
            if (j >= key_length) j = 0;
        }
        for (k = N - 1; k; --k) {
            mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1566083941U))
                     - static_cast<std::uint32_t>(i);
            ++i;
            if (i >= N) { mt_[0] = mt_[N - 1]; i = 1; }
        }
        mt_[0] = 0x80000000U;
    }

    std::uint32_t genrand_uint32() {
        static const std::uint32_t mag01[2] = {0x0U, MATRIX_A};
        std::uint32_t y;
        if (mti_ >= N) {
            int kk;
            for (kk = 0; kk < N - M; ++kk) {
                y = (mt_[kk] & UPPER_MASK) | (mt_[kk + 1] & LOWER_MASK);
                mt_[kk] = mt_[kk + M] ^ (y >> 1) ^ mag01[y & 0x1U];
            }
            for (; kk < N - 1; ++kk) {
                y = (mt_[kk] & UPPER_MASK) | (mt_[kk + 1] & LOWER_MASK);
                mt_[kk] = mt_[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1U];
            }
            y = (mt_[N - 1] & UPPER_MASK) | (mt_[0] & LOWER_MASK);
            mt_[N - 1] = mt_[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
            mti_ = 0;
        }
        y = mt_[mti_++];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9d2c5680U;
        y ^= (y << 15) & 0xefc60000U;
        y ^= (y >> 18);
        return y;
    }

    // CPython's getrandbits(k) for 0 < k <= 64.
    std::uint64_t getrandbits(int k) {
        if (k <= 32)
            return genrand_uint32() >> (32 - k);
        std::uint64_t result = 0;
        for (int shift = 0; k > 0; shift += 32, k -= 32) {
            std::uint32_t r = genrand_uint32();
            if (k < 32) r >>= (32 - k);
            result |= static_cast<std::uint64_t>(r) << shift;
        }
        return result;
    }
};

}

#endif
