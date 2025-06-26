#pragma once
#include <vector>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <cassert>
#include <algorithm>

#if defined(__GNUC__) || defined(__clang__)
#define VALLA_PREFETCH(addr) __builtin_prefetch((addr))
#else
    #define VALLA_PREFETCH(addr) do {} while(0)
#endif

namespace valla {
    template<typename T>
    concept HasEmptySentinel = requires
    {
        { T::EmptySentinel } -> std::convertible_to<T>;
    };

    template<typename T>
    concept TrivialCopyable = std::is_trivially_copyable_v<T> && std::copyable<T>;

    // Tune how many slots per segment get scanned per pass
    using PROBE_TYPE = uint8_t;
    using INDEX_TYPE = uint32_t;

    constexpr std::size_t PROBE_STRIDE = 16; // Adjust based on cache line size and performance testing
    constexpr std::size_t MAX_SEG = 32;


    /*
     * Segmented, dynamically growing, stable-index FixedHashSet (optimized):
     * - Probes several slots per segment before switching (cache-locality improvement)
     * - Newest segment probed first (adaptive/temporal locality)
     * - Prefetches next slot to mitigate memory stalls
     */
    template<
        HasEmptySentinel T,
        typename Hash,
        typename Equal
    >
    class FixedHashSet {
        static constexpr double load_factor_ = 0.75;

        using Segment = std::vector<T>;
        std::vector<Segment> table_;
        INDEX_TYPE initial_cap_ = 0;
        INDEX_TYPE initial_cap_log2_ = 0;
        INDEX_TYPE max_grow_size_ = 0;
        INDEX_TYPE max_grow_size_log2 = 0;
        INDEX_TYPE total_capacity_ = 0;
        INDEX_TYPE resize_at_ = 0;
        INDEX_TYPE size_ = 0;
        INDEX_TYPE _dseg = 0;
        INDEX_TYPE _thresh = 0;
        int _thresh_log2 = 0;
        std::uint8_t n_seg = 1;
        Hash hash_;
        Equal eq_;

        static constexpr INDEX_TYPE ILLEGAL_INDEX = static_cast<INDEX_TYPE>(-1);


        constexpr static INDEX_TYPE doubling_segs(INDEX_TYPE ic, INDEX_TYPE mg) noexcept {
            return std::countr_zero(mg) - std::countr_zero(ic);
        }
        constexpr static INDEX_TYPE double_threshold(INDEX_TYPE ic, INDEX_TYPE doubling_segs) noexcept {
            return ic * ((1ULL << doubling_segs) - 1);
        }

        // Maps global index to (segment index, index within segment)
        constexpr std::pair<INDEX_TYPE, INDEX_TYPE>
        logical_to_segment(INDEX_TYPE idx) const noexcept {
            // Everything up to (but not including) _thresh is the doubling region
            const auto out_initial = (idx >= initial_cap_);
            if (idx < _thresh) {
                // For doubling segments after the first
                // Find seg such that: start = initial_cap_ << (seg-1)
                // Essentially seg = 1 + floor(log2(idx / initial_cap_))
                INDEX_TYPE seg = std::bit_width(idx) - initial_cap_log2_;
                INDEX_TYPE seg_start = initial_cap_ << (seg - 1);
                return {out_initial * seg, idx - out_initial * seg_start};
            }
            // Fixed-size segments after threshold
            INDEX_TYPE fixed_idx = idx - _thresh;
            INDEX_TYPE seg = _dseg + (fixed_idx / max_grow_size_);
            INDEX_TYPE offset = fixed_idx % max_grow_size_;
            return {seg, offset};
        }

        // Maps (segment index, index within segment) to global logical index
        constexpr INDEX_TYPE
        segment_to_logical(uint8_t seg, INDEX_TYPE idx) const noexcept {
            const bool out_initial = seg != 0;
            const bool in_doubling_region = seg < _dseg;
            return (out_initial) * (
                !in_doubling_region * (_thresh + (seg - _dseg) * max_grow_size_) +
                in_doubling_region * (initial_cap_ << (seg - 1))) + idx;
        }

        static constexpr INDEX_TYPE probe_vec(INDEX_TYPE base, PROBE_TYPE probe, INDEX_TYPE mask) noexcept {
            return (base + probe) & mask;
        }


        static constexpr INDEX_TYPE calculate_initial_offset(const INDEX_TYPE h, const INDEX_TYPE mask) {
            assert(mask == (std::bit_ceil(mask) - 1) && "Size must be a power of two");
            return h & mask;
        }

        constexpr INDEX_TYPE get_mask(const uint8_t seg) const {
            // Apply the Max Grow size mask, as well as the current bit mask. The most restrictive wins.
            return (max_grow_size_ - 1) & ((initial_cap_ << (seg - (seg != 0))) - 1);
        }

    public:
        FixedHashSet(
            INDEX_TYPE initial_cap,
            Hash hash,
            Equal eq,
            INDEX_TYPE max_grow_size = 1 << 22)
            : initial_cap_(std::bit_floor(initial_cap)),
              initial_cap_log2_(std::countr_zero(initial_cap_)),
              max_grow_size_(std::bit_floor(max_grow_size)),
              max_grow_size_log2(std::countr_zero(max_grow_size)),
              total_capacity_(initial_cap_),
              resize_at_(static_cast<INDEX_TYPE>(total_capacity_ * load_factor_)),
              hash_(std::move(hash)),
              eq_(std::move(eq)),
              _dseg(doubling_segs(initial_cap_, max_grow_size)),
              _thresh(double_threshold(initial_cap_, _dseg)),
              _thresh_log2(std::countr_zero(_thresh))
        {

            assert(initial_cap_ >= 2 && "Initial cap too small");
            assert((initial_cap_ & (initial_cap_ - 1)) == 0 && "initial_cap_ must be power of two");
            assert((max_grow_size_ & (max_grow_size_ - 1)) == 0 && "max_grow_size must be power of two");
            assert(max_grow_size >= initial_cap_);
            assert(std::bit_ceil(initial_cap_) == initial_cap_ && "initial_cap_ must be power of two");
            assert(std::bit_ceil(max_grow_size_) == max_grow_size_ && "max_grow_size must be power of two");
            assert(_dseg > 0 && "There must be at least 1 doubling segment");
            assert(_thresh > 0);

            table_.emplace_back(initial_cap_, T::EmptySentinel);
        }

        ~FixedHashSet() {
            utils::g_log << "State set destroyed, size: " << size_ << " entries"<< std::endl;
            utils::g_log << "State set destroyed, size per entry: " << 2 << " blocks"<< std::endl;
            utils::g_log << "State set destroyed, capacity: " << total_capacity_ << " entries" << std::endl;
            utils::g_log << "State set destroyed, segments: " << static_cast<size_t>(n_seg) << " segs" << std::endl;
            utils::g_log << "State set destroyed, byte size: " << static_cast<double>(size_ * sizeof(T)) / (1024 * 1024) << "MB" << std::endl;
            utils::g_log << "State set destroyed, byte capacity: " << static_cast<double>(total_capacity_ * sizeof(T)) / (1024 * 1024) << "MB" << std::endl;
            utils::g_log << "State set destroyed, load: " << static_cast<long double>(size_) / total_capacity_ * 100 << "%" << std::endl;
        };

        [[nodiscard]] INDEX_TYPE size() const noexcept { return size_; }
        [[nodiscard]] INDEX_TYPE capacity() const noexcept { return total_capacity_; }
        [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

        std::pair<INDEX_TYPE, bool> insert(const T &value) {
            assert(value != T::EmptySentinel);
            if (size_ + 1 > resize_at_) do_grow();

            const auto h = hash_(value);
            std::optional<std::pair<INDEX_TYPE, INDEX_TYPE> > first_empty;

            for (int8_t seg = n_seg - 1; seg >= 0; --seg) {
                const auto seg_mask = get_mask(seg);
                assert(std::countr_one(seg_mask) == std::bit_width(seg_mask) && "Mask must be a power of two minus one");
                for (uint8_t stride = 0; stride < PROBE_STRIDE; ++stride) {
                    const INDEX_TYPE offset = probe_vec(h, stride, seg_mask);
                    T &slot = table_[seg][offset];

                    if (slot == T::EmptySentinel && !first_empty) {
                        first_empty = std::make_pair(seg, offset);
                        break;
                    }
                    if (eq_(slot, value))
                        return {segment_to_logical(seg, offset), false}; // Already present!

                }
            }

            if (first_empty) {
                auto [seg, offset] = *first_empty;
                table_[seg][offset] = value;
                ++size_;
                return {segment_to_logical(seg, offset), true};
            }

            utils::g_log << "Insertion failed, no empty slot found for value (Load " <<
                static_cast<double>(size())/total_capacity_ * 100<< "%). Growing." << std::endl;

            // std::size_t non_empty = 0;
            // for (auto i = 0; i < total_capacity_; ++i) {
            //     const auto [seg, idx] = logical_to_segment(i);
            //     if (table_[seg][idx] != T::EmptySentinel) ++non_empty;
            //
            //     if (i % 100 == 0) {
            //         utils::g_log << "Non-empty entries: " << non_empty << std::endl;
            //         non_empty = 0;
            //         }
            //     }
            //

            do_grow();

            return insert(value); // Retry insertion after growing
        }

        // Lookup: true if present
        bool contains(const T &value) const {
            assert(value != T::EmptySentinel);

            const auto h = hash_(value);
            for (int8_t seg = n_seg - 1; seg >= 0; --seg) {
                const auto seg_mask = get_mask(seg);
                for (uint8_t stride = 0; stride < PROBE_STRIDE; ++stride) {
                    INDEX_TYPE offset = probe_vec(h, stride, seg_mask);
                    const T &slot = table_[seg][offset];


                    if (slot == T::EmptySentinel) break; // End-of-chain for this segment
                    if (eq_(slot, value)) return true;
                }
            }
            return false;
        }

        // Find: same as contains, but returns first location or ILLEGAL_INDEX
        std::pair<INDEX_TYPE, bool> find(const T &value) const {
            assert(value != T::EmptySentinel);

            const auto h = hash_(value);
            for (int8_t seg = n_seg - 1; seg >= 0; --seg) {
                const auto seg_mask = get_mask(seg);
                for (uint8_t stride = 0; stride < PROBE_STRIDE; ++stride) {
                    INDEX_TYPE offset = probe_vec(h, stride, seg_mask);
                    const T &slot = table_[seg][offset];

                    if (slot == T::EmptySentinel) return {ILLEGAL_INDEX, false};
                    if (eq_(slot, value)) return {segment_to_logical(seg, offset), true};
                }
            }
            return {ILLEGAL_INDEX, false};
        }

        // Get by stable logical index
        T get(INDEX_TYPE idx) const {
            assert(idx != ILLEGAL_INDEX && "Cannot get ILLEGAL_INDEX");
            assert(idx < total_capacity_);
            auto [seg, local] = logical_to_segment(idx);
            assert(table_[seg][local] != T::EmptySentinel && "Cannot get EmptySentinel");
            return table_[seg][local];
        }

        // Report memory usage (elements only, not including indirection)
        std::size_t get_memory_usage() const {
            return total_capacity_ * sizeof(T);
        }

        // Report memory usage (elements only, not including indirection)
        std::size_t get_occupied_memory_usage() const {
            return size_ * sizeof(T);
        }

    private:
        void do_grow() {
            //auto grow_size = std::min(total_capacity_, _max_grow_size);
            const auto grow_size = std::min(total_capacity_, max_grow_size_);
            table_.emplace_back(Segment(grow_size, T::EmptySentinel));
            total_capacity_ += grow_size;
            resize_at_ = static_cast<INDEX_TYPE>(static_cast<double>(total_capacity_) * load_factor_);
            ++n_seg;
        }
    };
} // namespace valla
