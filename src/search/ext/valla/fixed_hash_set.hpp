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
        INDEX_TYPE total_capacity_ = 0;
        INDEX_TYPE resize_at_ = 0;
        INDEX_TYPE size_ = 0;
        INDEX_TYPE _grow_size = 0;
        INDEX_TYPE grow_size_log2_ = 0; // log2 of grow size, used for segment calculations
        std::uint8_t n_seg = 1;
        Hash hash_;
        Equal eq_;

        static constexpr INDEX_TYPE ILLEGAL_INDEX = static_cast<INDEX_TYPE>(-1);

        constexpr std::pair<INDEX_TYPE, INDEX_TYPE> logical_to_segment(INDEX_TYPE idx) const noexcept {
            const bool in_growth = idx >= initial_cap_;
            INDEX_TYPE x = idx - initial_cap_;

            INDEX_TYPE seg = 1 + (x >> grow_size_log2_);      // division
            INDEX_TYPE offset = x & (_grow_size - 1);         // modulo
            return {in_growth * seg,  in_growth * offset + !in_growth * idx};

        }
        constexpr INDEX_TYPE segment_to_logical(uint8_t seg, INDEX_TYPE idx) const noexcept {
            return (seg > 0) * (initial_cap_ + (seg - 1) * _grow_size) + idx;
        }

        static constexpr INDEX_TYPE probe_vec(INDEX_TYPE base, PROBE_TYPE probe, INDEX_TYPE size) noexcept {
            return (base + probe) & (size - 1);
        }


        constexpr INDEX_TYPE calculate_initial_offsets(const INDEX_TYPE h, const uint8_t seg) const {
            return seg == 0 ? h % initial_cap_: h % _grow_size;
        }

        constexpr INDEX_TYPE get_size(const uint8_t seg) const {
            return seg == 0 ? initial_cap_ : _grow_size;
        }

    public:
        FixedHashSet(
            INDEX_TYPE initial_cap,
            Hash hash,
            Equal eq,
            INDEX_TYPE grow_size = 1 << 20)
            : initial_cap_(std::bit_floor(initial_cap)),
              total_capacity_(initial_cap_),
              resize_at_(static_cast<INDEX_TYPE>(total_capacity_ * load_factor_)),
              hash_(std::move(hash)),
              eq_(std::move(eq)),
              _grow_size(std::bit_floor(grow_size)),
              grow_size_log2_(std::countr_zero(_grow_size)) {
            table_.emplace_back(initial_cap_, T::EmptySentinel);
        }

        ~FixedHashSet() {
            utils::g_log << "FixedHashSet destroyed, size: " << size_ << " entries"<< std::endl;
            utils::g_log << "FixedHashSet destroyed, capacity: " << total_capacity_ << " entries" << std::endl;
            utils::g_log << "FixedHashSet destroyed, segments: " << static_cast<size_t>(n_seg) << " segs" << std::endl;
            utils::g_log << "FixedHashSet destroyed, byte size: " << size_ * sizeof(T) / 1024 << "KB" << std::endl;
            utils::g_log << "FixedHashSet destroyed, byte capacity: " << total_capacity_ * sizeof(T) / 1024 << "KB" << std::endl;
            utils::g_log << "FixedHashSet destroyed, load: " << static_cast<long double>(size_) / total_capacity_ * 100 << "%" << std::endl;
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
                const auto initial_offset = calculate_initial_offsets(h, seg);
                const auto seg_size = get_size(seg);
                for (uint8_t stride = 0; stride < PROBE_STRIDE; ++stride) {
                    INDEX_TYPE offset = probe_vec(initial_offset, stride, seg_size);
                    T &slot = table_[seg][offset];

                    if (slot == T::EmptySentinel) {
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

            utils::g_log << "Insertion failed, no empty slot found for value. Growing." << std::endl;

            do_grow();

            return insert(value); // Retry insertion after growing
        }

        // Lookup: true if present
        bool contains(const T &value) const {
            assert(value != T::EmptySentinel);

            const auto h = hash_(value);
            for (int8_t seg = n_seg - 1; seg >= 0; --seg) {
                const auto initial_offset = calculate_initial_offsets(h, seg);
                const auto seg_size = get_size(seg);
                for (uint8_t stride = 0; stride < PROBE_STRIDE; ++stride) {
                    INDEX_TYPE offset = probe_vec(initial_offset, stride, seg_size);
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
                const auto initial_offset = calculate_initial_offsets(h, seg);
                const auto seg_size = get_size(seg);
                for (uint8_t stride = 0; stride < PROBE_STRIDE; ++stride) {
                    INDEX_TYPE offset = probe_vec(initial_offset, stride, seg_size);
                    const T &slot = table_[seg][offset];

                    if (slot == T::EmptySentinel) return {ILLEGAL_INDEX, false};
                    if (eq_(slot, value)) return {segment_to_logical(seg, offset), true};
                }
            }
            return {ILLEGAL_INDEX, false};
        }

        // Get by stable logical index
        T get(INDEX_TYPE idx) const {
            assert(idx != T::EmptySentinel.lhs && "Cannot get EmptySentinel");
            assert(idx < total_capacity_);
            auto [seg, local] = logical_to_segment(idx);
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
            table_.emplace_back(Segment(_grow_size, T::EmptySentinel));
            total_capacity_ += _grow_size;
            resize_at_ = static_cast<INDEX_TYPE>(static_cast<double>(total_capacity_) * load_factor_);
            ++n_seg;
        }
    };
} // namespace valla
