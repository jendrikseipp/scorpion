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
concept TrivialCopyable = std::is_trivially_copyable_v<T> && std::copyable<T>;

// Tune how many slots per segment get scanned per pass
constexpr std::size_t PROBE_STRIDE = 8;  // Adjust based on cache line size and performance testing

/*
 * Segmented, dynamically growing, stable-index FixedHashSet (optimized):
 * - Probes several slots per segment before switching (cache-locality improvement)
 * - Newest segment probed first (adaptive/temporal locality)
 * - Prefetches next slot to mitigate memory stalls
 */
template<
    TrivialCopyable T,
    T EmptySentinel,
    typename Hash,
    typename Equal
>
class FixedHashSet {
    static constexpr double load_factor_ = 0.75;

    using Segment = std::vector<T>;
    std::vector<Segment> table_;
    std::size_t total_capacity_ = 0;
    std::size_t initial_cap_ = 0;
    std::size_t resize_at_ = 0;
    std::size_t size_ = 0;
    Hash hash_;
    Equal eq_;

    static constexpr std::size_t ILLEGAL_INDEX = static_cast<std::size_t>(-1);

    // Map logical index to (segment, local index) pair
    [[nodiscard]] std::pair<size_t, size_t> logical_to_segment(std::size_t idx) const {
        assert(idx < total_capacity_);
        auto segment_index = std::bit_width(idx / initial_cap_);
        auto offset = initial_cap_ * ((1ULL << segment_index) - 1);
        return {segment_index, idx - offset};
    }

    [[nodiscard]] size_t segment_to_logical(size_t seg, size_t idx) const {
        return ((1ULL << seg) - 1) * initial_cap_ + idx;
    }

    [[nodiscard]] static size_t probe_vec(size_t base, size_t probe, size_t size) noexcept {
        return (base + probe) % size;
    }

    [[nodiscard]] std::vector<size_t> calculate_initial_offsets(size_t h) const {
        std::vector<size_t> segment_offsets(table_.size());
        for (auto i = 0u; i < segment_offsets.size(); ++i) {
            segment_offsets[i] = h % table_[i].size();
        }
        return segment_offsets;
    }

public:
    FixedHashSet(
        std::size_t initial_cap,
        Hash hash,
        Equal eq)
        : total_capacity_(initial_cap),
          initial_cap_(initial_cap),
          resize_at_(static_cast<std::size_t>(initial_cap * load_factor_)),
          hash_(std::move(hash)),
          eq_(std::move(eq))
    {
        table_.emplace_back(Segment(initial_cap, EmptySentinel));
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return total_capacity_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    // Insert; returns {idx, true} if inserted; {idx, false} if already present.
    std::pair<std::size_t, bool> insert(const T& value) {
        assert(value != EmptySentinel);

        // If load-factor exceeded, grow before inserting
        if(size_ + 1 > resize_at_) do_grow();

        auto h = hash_(value);
        std::vector<size_t> probe(table_.size(), 0);
        auto segment_offsets = calculate_initial_offsets(h);

        // Probe order: new segments first (most likely to succeed!).
        for (std::size_t cycle = 0; cycle < total_capacity_; ++cycle) {
            for (std::size_t seg_rev = 0; seg_rev < table_.size(); ++seg_rev) {
                std::size_t seg = table_.size() - 1 - seg_rev;

                for (std::size_t stride = 0; stride < PROBE_STRIDE; ++stride) {
                    std::size_t offset = probe_vec(segment_offsets[seg], probe[seg], table_[seg].size());
                    T& slot = table_[seg][offset];

                    // Prefetch next slot in same segment (if safe)
                    if constexpr (PROBE_STRIDE > 1) {
                        if(stride + 1 < PROBE_STRIDE) {
                            auto prefetch_idx = probe_vec(segment_offsets[seg], probe[seg]+1, table_[seg].size());
                            VALLA_PREFETCH(&table_[seg][prefetch_idx]);
                        }
                    }

                    if(slot == EmptySentinel) {
                        slot = value;
                        ++size_;
                        return {segment_to_logical(seg, offset), true};
                    }
                    if(eq_(slot, value))
                        return {segment_to_logical(seg, offset), false};

                    ++probe[seg];
                }
            }
        }
        // Should be unreachable if max load respected
        assert(false && "HashSet insertion failed: table full?");
        return {ILLEGAL_INDEX, false};
    }

    // Lookup: true if present
    bool contains(const T& value) const {
        assert(value != EmptySentinel);

        auto h = hash_(value);
        auto segment_offsets = calculate_initial_offsets(h);
        std::vector<size_t> probe(table_.size(), 0);

        // Adaptive: check newest segment first for locality
        for (std::size_t cycle = 0; cycle < total_capacity_; ++cycle) {
            for (std::size_t seg_rev = 0; seg_rev < table_.size(); ++seg_rev) {
                std::size_t seg = table_.size() - 1 - seg_rev;

                for (std::size_t stride = 0; stride < PROBE_STRIDE; ++stride) {
                    std::size_t offset = probe_vec(segment_offsets[seg], probe[seg], table_[seg].size());
                    const T& slot = table_[seg][offset];
                    // Prefetch next, as in insert
                    if constexpr (PROBE_STRIDE > 1) {
                        if(stride + 1 < PROBE_STRIDE) {
                            auto prefetch_idx = probe_vec(segment_offsets[seg], probe[seg]+1, table_[seg].size());
                            VALLA_PREFETCH(&table_[seg][prefetch_idx]);
                        }
                    }

                    if(slot == EmptySentinel) break; // End-of-chain for this segment
                    if(eq_(slot, value)) return true;
                    ++probe[seg];
                }
            }
        }
        return false;
    }

    // Find: same as contains, but returns first location or ILLEGAL_INDEX
    std::pair<std::size_t, bool> find(const T& value) const {
        assert(value != EmptySentinel);

        auto h = hash_(value);
        auto segment_offsets = calculate_initial_offsets(h);
        std::vector<size_t> probe(table_.size(), 0);

        for (std::size_t cycle = 0; cycle < total_capacity_; ++cycle) {
            for (std::size_t seg_rev = 0; seg_rev < table_.size(); ++seg_rev) {
                std::size_t seg = table_.size() - 1 - seg_rev;
                for (std::size_t stride = 0; stride < PROBE_STRIDE; ++stride) {
                    std::size_t offset = probe_vec(segment_offsets[seg], probe[seg], table_[seg].size());
                    const T& slot = table_[seg][offset];
                    // Prefetch as above
                    if constexpr (PROBE_STRIDE > 1) {
                        if(stride + 1 < PROBE_STRIDE) {
                            auto prefetch_idx = probe_vec(segment_offsets[seg], probe[seg]+1, table_[seg].size());
                            VALLA_PREFETCH(&table_[seg][prefetch_idx]);
                        }
                    }

                    if(slot == EmptySentinel) return {ILLEGAL_INDEX, false};
                    if(eq_(slot, value)) return {segment_to_logical(seg, offset), true};
                    ++probe[seg];
                }
            }
        }
        return {ILLEGAL_INDEX, false};
    }

    // Get by stable logical index
    T get(std::size_t idx) const {
        assert(idx < total_capacity_);
        auto [seg, local] = logical_to_segment(idx);
        return table_[seg][local];
    }

    // Erase; returns true if erased
    bool erase(const T& value) {
        assert(value != EmptySentinel);
        auto h = hash_(value);

        for (std::size_t seg_rev = 0; seg_rev < table_.size(); ++seg_rev) {
            std::size_t seg = table_.size() - 1 - seg_rev;
            std::size_t seg_cap = table_[seg].size();
            for(std::size_t probe_idx = 0; probe_idx < seg_cap; ++probe_idx) {
                std::size_t idx = (h + probe_idx) % seg_cap;
                T& slot = table_[seg][idx];
                if(slot == EmptySentinel) break;
                if(eq_(slot, value)) {
                    slot = EmptySentinel; --size_;
                    return true;
                }
            }
        }
        return false;
    }

    void clear() {
        for (auto& seg : table_)
            std::fill(seg.begin(), seg.end(), EmptySentinel);
        size_ = 0;
    }

    // Report memory usage (elements only, not including indirection)
    std::size_t get_memory_usage() const {
        return total_capacity_ * sizeof(T);
    }

private:
    void do_grow() {
        table_.emplace_back(Segment(total_capacity_, EmptySentinel));
        total_capacity_ *= 2;
        resize_at_ = static_cast<std::size_t>(total_capacity_ * load_factor_);
    }
};

} // namespace valla