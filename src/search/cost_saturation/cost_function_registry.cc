#include "cost_function_registry.h"

#include "types.h"

#include "../utils/collections.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <type_traits>

using namespace std;

namespace cost_saturation {
uint64_t CostFunctionRegistry::compute_hash(const Costs &costs) {
    uint64_t hash = 0;
    for (size_t op_id = 0; op_id < costs.size(); ++op_id) {
        hash += mix_op_cost(op_id, costs[op_id]);
    }
    return hash;
}

// Map INF to 0 and finite costs to cost + 1 to avoid a special case.
static unsigned int packed_cost_value(int cost) {
    return (cost == INF) ? 0 : static_cast<unsigned int>(cost) + 1;
}

/* Append the given values with a compile-time bit width that divides 8, so
   the loops have fixed stride. */
template<int BITS>
static void pack_values(const vector<unsigned int> &values,
                        vector<uint8_t> &blob) {
    constexpr int PER_BYTE = 8 / BITS;
    size_t offset = blob.size();
    blob.resize(offset + (values.size() + PER_BYTE - 1) / PER_BYTE, 0);
    uint8_t *bytes = blob.data() + offset;
    for (size_t i = 0; i < values.size(); ++i) {
        bytes[i / PER_BYTE] |= values[i] << (i % PER_BYTE * BITS);
    }
}

template<typename UInt>
static void pack_values_wide(const vector<unsigned int> &values,
                             vector<uint8_t> &blob) {
    size_t offset = blob.size();
    blob.resize(offset + values.size() * sizeof(UInt));
    UInt *out = reinterpret_cast<UInt *>(blob.data() + offset);
    for (size_t i = 0; i < values.size(); ++i) {
        out[i] = static_cast<UInt>(values[i]);
    }
}

void CostFunctionRegistry::pack(const Costs &costs, vector<uint8_t> &blob) {
    assert(costs.size() == baseline.size());
    size_t num_ops = costs.size();
    size_t num_blocks = (num_ops + 63) / 64;
    size_t bitmap_bytes = (num_blocks + 7) / 8;
    blob.clear();
    blob.resize(1 + bitmap_bytes, 0);
    uint8_t *bitmap = blob.data() + 1;

    /* Diffs cluster in few 64-operator blocks, so the mask is stored as a
       bitmap of the dirty blocks plus one 64-bit mask per dirty block. */
    scratch_masks.clear();
    scratch_values.clear();
    unsigned int max_value = 0;
    for (size_t block = 0; block < num_blocks; ++block) {
        uint64_t mask = 0;
        size_t end = min(num_ops, (block + 1) * 64);
        for (size_t i = block * 64; i < end; ++i) {
            if (costs[i] != baseline[i]) {
                mask |= uint64_t(1) << (i % 64);
                unsigned int value = packed_cost_value(costs[i]);
                scratch_values.push_back(value);
                max_value = max(max_value, value);
            }
        }
        if (mask) {
            bitmap[block / 8] |= 1 << (block % 8);
            scratch_masks.push_back(mask);
        }
    }
    size_t masks_offset = blob.size();
    blob.resize(masks_offset + scratch_masks.size() * sizeof(uint64_t));
    memcpy(blob.data() + masks_offset, scratch_masks.data(),
           scratch_masks.size() * sizeof(uint64_t));

    uint8_t bits_per_value = bit_ceil(
        static_cast<uint8_t>(bit_width(max_value)));
    blob[0] = bits_per_value;
    switch (bits_per_value) {
    case 1:
        pack_values<1>(scratch_values, blob);
        break;
    case 2:
        pack_values<2>(scratch_values, blob);
        break;
    case 4:
        pack_values<4>(scratch_values, blob);
        break;
    case 8:
        pack_values_wide<uint8_t>(scratch_values, blob);
        break;
    case 16:
        pack_values_wide<uint16_t>(scratch_values, blob);
        break;
    default:
        pack_values_wide<uint32_t>(scratch_values, blob);
        break;
    }
}

namespace {
// Read the index-th stored value; BITS is fixed per blob.
template<int BITS>
unsigned int read_value(const uint8_t *values, size_t index) {
    if constexpr (BITS < 8) {
        constexpr int PER_BYTE = 8 / BITS;
        return (values[index / PER_BYTE] >>
                (index % PER_BYTE * BITS)) & ((1u << BITS) - 1);
    } else {
        typename std::conditional<
            BITS == 8, uint8_t,
            typename std::conditional<
                BITS == 16, uint16_t, uint32_t>::type>::type v;
        memcpy(&v, values + index * sizeof(v), sizeof(v));
        return v;
    }
}

template<int BITS>
bool matches_blocks(
    const uint8_t *bitmap, const uint8_t *masks, const uint8_t *values,
    const Costs &costs, const Costs &baseline) {
    size_t num_ops = costs.size();
    size_t num_blocks = (num_ops + 63) / 64;
    size_t next_mask = 0;
    size_t value_index = 0;
    for (size_t block = 0; block < num_blocks; ++block) {
        size_t begin = block * 64;
        size_t end = min(num_ops, begin + 64);
        if (!(bitmap[block / 8] & (1 << (block % 8)))) {
            // Clean block: all costs must equal the baseline.
            if (!equal(costs.begin() + begin, costs.begin() + end,
                       baseline.begin() + begin)) {
                return false;
            }
            continue;
        }
        uint64_t mask;
        memcpy(&mask, masks + next_mask * sizeof(uint64_t), sizeof(uint64_t));
        ++next_mask;
        for (size_t i = begin; i < end; ++i) {
            if (mask & (uint64_t(1) << (i % 64))) {
                if (read_value<BITS>(values, value_index++) !=
                    packed_cost_value(costs[i])) {
                    return false;
                }
            } else if (costs[i] != baseline[i]) {
                return false;
            }
        }
    }
    return true;
}
}

/* Compare the cost function against a stored blob in one mask-guided pass,
   without materializing the query's blob. */
bool CostFunctionRegistry::matches(CostKey key, const Costs &costs) const {
    size_t num_blocks = (costs.size() + 63) / 64;
    size_t bitmap_bytes = (num_blocks + 7) / 8;
    const uint8_t *blob = blobs.data(key);
    const uint8_t *bitmap = blob + 1;
    int num_dirty = 0;
    for (size_t b = 0; b < bitmap_bytes; ++b) {
        num_dirty += popcount(bitmap[b]);
    }
    const uint8_t *masks = bitmap + bitmap_bytes;
    const uint8_t *values = masks + num_dirty * sizeof(uint64_t);
    switch (blob[0]) {
    case 1:
        return matches_blocks<1>(bitmap, masks, values, costs, baseline);
    case 2:
        return matches_blocks<2>(bitmap, masks, values, costs, baseline);
    case 4:
        return matches_blocks<4>(bitmap, masks, values, costs, baseline);
    case 8:
        return matches_blocks<8>(bitmap, masks, values, costs, baseline);
    case 16:
        return matches_blocks<16>(bitmap, masks, values, costs, baseline);
    default:
        return matches_blocks<32>(bitmap, masks, values, costs, baseline);
    }
}

CostKey CostFunctionRegistry::register_or_lookup(
    const Costs &costs, uint64_t hash) {
    if (baseline.empty()) {
        baseline = costs;
    }
    auto it = key_by_hash.find(hash);
    if (it == key_by_hash.end()) {
        CostKey key = size();
        pack(costs, scratch_blob);
        blobs.append(scratch_blob);
        key_by_hash.emplace(hash, key);
        return key;
    }
    if (matches(it->second, costs)) {
        return it->second;
    }
    // Hash collision: look for the cost function in the overflow list.
    for (const auto &[overflow_hash, overflow_key] : overflow) {
        if (overflow_hash == hash && matches(overflow_key, costs)) {
            return overflow_key;
        }
    }
    CostKey key = size();
    pack(costs, scratch_blob);
    blobs.append(scratch_blob);
    overflow.emplace_back(hash, key);
    return key;
}

CostKey CostFunctionRegistry::lookup(const Costs &costs, uint64_t hash) const {
    auto it = key_by_hash.find(hash);
    if (it == key_by_hash.end()) {
        return NO_KEY;
    }
    if (matches(it->second, costs)) {
        return it->second;
    }
    for (const auto &[overflow_hash, overflow_key] : overflow) {
        if (overflow_hash == hash && matches(overflow_key, costs)) {
            return overflow_key;
        }
    }
    return NO_KEY;
}

void CostFunctionRegistry::release_memory() {
    decltype(key_by_hash)().swap(key_by_hash);
    utils::release_vector_memory(overflow);
    utils::release_vector_memory(baseline);
    blobs.release_memory();
    utils::release_vector_memory(scratch_blob);
    utils::release_vector_memory(scratch_values);
}
}
