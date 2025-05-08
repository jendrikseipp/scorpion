#pragma once
#include <vector>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <cassert>
#include <concepts>


namespace valla {

template <std::size_t Bits>
struct cleary_storage_type;
template<> struct cleary_storage_type<16> { using type = uint16_t; };
template<> struct cleary_storage_type<32> { using type = uint32_t; };
template<> struct cleary_storage_type<64> { using type = uint64_t; };


// Example: PackedKey<16> gives a key type with 14 data bits, 2 control bits
template <std::size_t EntryBits>
struct PackedKey {
    static_assert(EntryBits == 16 || EntryBits == 32 || EntryBits == 64, "Supported: 16/32/64");
    using storage_type = typename cleary_storage_type<EntryBits>::type;
    static constexpr storage_type CTRL_MASK = 0b11;
    static constexpr std::size_t KEY_BITS = EntryBits - 2;
    static constexpr storage_type KEY_MASK = (static_cast<storage_type>(1) << KEY_BITS) - 1;

    static constexpr storage_type CTRL_EMPTY = 0b00;
    static constexpr storage_type CTRL_OCCUPIED = 0b01;
    static constexpr storage_type CTRL_TOMBSTONE = 0b10;

    storage_type data;

    PackedKey() : data(0) {}

    inline void set_key(storage_type key) noexcept {
        data = (data & CTRL_MASK) | ((key & KEY_MASK) << 2);
    }

    inline storage_type get_key() const noexcept {
        return (data >> 2) & KEY_MASK;
    }

    inline void set_ctrl(uint8_t ctrl) noexcept {
        data = (data & ~CTRL_MASK) | (ctrl & CTRL_MASK);
    }

    inline uint8_t get_ctrl() const noexcept {
        return data & CTRL_MASK;
    }

    // Set both at once
    inline void set(storage_type key, uint8_t ctrl) noexcept {
        data = ((key & KEY_MASK) << 2) | (ctrl & CTRL_MASK);
    }

    inline bool empty() const noexcept { return get_ctrl() == 0; }
    inline bool occupied() const noexcept { return get_ctrl() == 1; }
    inline bool tombstone() const noexcept { return get_ctrl() == 2; }
};

template <std::size_t EntryBits, typename Value>
class ClearyTable {
    using key_storage_type = typename cleary_storage_type<EntryBits>::type;
    static constexpr std::size_t KEY_BITS = sizeof(key_storage_type) - 2;
    using key_type = key_storage_type; // User must guarantee actual keys fit in KEY_BITS

    static constexpr auto CTRL_EMPTY = PackedKey<EntryBits>::CTRL_EMPTY;
    static constexpr auto CTRL_OCCUPIED = PackedKey<EntryBits>::CTRL_OCCUPIED;
    static constexpr auto CTRL_TOMBSTONE = PackedKey<EntryBits>::CTRL_TOMBSTONE;

    using mapped_type = Value;
    using size_t = std::size_t;

    struct Entry {
        PackedKey<KEY_BITS> key;
        Value value;

        Entry() : key(), value(0) {}

        inline void set_key(key_storage_type k) { key.set_key(k); }
        inline key_storage_type get_key() const { return key.get_key(); }
        inline void set_value(size_t v) { value = v; }
        inline size_t get_value() const { return value; }
        inline void set_ctrl(uint8_t ctrl) { key.set_ctrl(ctrl); }
        inline uint8_t get_ctrl() const { return key.get_ctrl(); }
        inline bool empty() const { return key.empty(); }
        inline bool occupied() const { return key.occupied(); }
        inline bool tombstone() const { return key.tombstone(); }

        Entry& operator=(Entry&& other) noexcept {
            if (this != &other) {
                key = std::move(other.key);
                value = std::move(other.value);
            }
            return *this;
        }
    };



    std::vector<Entry> entries;
    size_t size_;
    size_t cap_m1_;

    // Hash primitive
    inline size_t hash_key(key_storage_type key) const noexcept {
        // Use a fast (identity or mul) hash since keys are integers and table is power-of-2
        return static_cast<size_t>(key * 0x9e3779b1u) & cap_m1_;
    }

    void grow_if_needed() {
        if (size_ * 10 / 8 >= cap_m1_ + 1)
            resize((cap_m1_ + 1) * 2);
    }

    void resize(size_t new_cap) {
        new_cap = round_up_pow2(new_cap);
        std::vector<Entry> new_entries(new_cap);
        for (size_t i = 0; i <= cap_m1_; ++i)
            if (entries[i].occupied()) {
                insert_internal(new_entries, new_cap-1, entries[i]);
            }
        entries.swap(new_entries);
        cap_m1_ = new_cap - 1;
    }
    static size_t round_up_pow2(size_t n) {
        size_t rp = 4;
        while(rp < n) rp <<= 1;
        return rp;
    }
    // Used during rehash
    void insert_internal(std::vector<Entry>& entries, size_t mask, const Entry& entry) {
        size_t h = static_cast<size_t>(entry.get_key() * 0x9e3779b1u) & mask;

        while (!entries[h].empty() && !entries[h].tombstone())
            h = (h + 1) & mask;

        entries[h] = entry;
    }

public:
    explicit ClearyTable(size_t cap=64)
        : entries(round_up_pow2(cap)), size_(0), cap_m1_(round_up_pow2(cap)-1)
    { clear(); }

    void clear() noexcept {
        for (auto& pk : entries) pk.set_ctrl(CTRL_EMPTY);
        size_ = 0;
    }
    size_t size() const noexcept { return size_; }
    size_t capacity() const noexcept { return cap_m1_+1; }
    bool empty() const noexcept { return size_ == 0; }

    // Insert
    bool insert(const Value& value) {
        grow_if_needed();
        auto [addr, rem] = hash_key(value);
        size_t first_tomb = capacity();
        for (;;) {
            auto ctrl = entries[addr].get_ctrl();
            if (entries[addr].get_ctrl() == entries[addr].get_ctrl()) {
                auto slot = (first_tomb != capacity()) ? first_tomb : h;
                entries[slot].set(rem, CTRL_OCCUPIED);
                entries[slot].set_value(value);
                ++size_;
                return true;
            }
            else if (ctrl == CTRL_TOMBSTONE && first_tomb == capacity()) {
                first_tomb = addr;
            }
            else if (ctrl == CTRL_OCCUPIED && entries[addr].get_key() == rem) {
                entries[addr] = value;
                return false;
            }
            addr = (addr + 1) % cap_m1_;
        }
    }
    // Find
    Value* find(const Value& value) {
        auto [addr, rem] = hash_key(value);

        for (;;) {
            auto ctrl = entries[addr].get_ctrl();
            if (ctrl == CTRL_EMPTY) return nullptr;
            if (ctrl == CTRL_OCCUPIED && entries[addr].get_key() == rem)
                return &entries[addr].get_value();
            addr = (addr + 1) % cap_m1_;
        }
    }
    // Erase
    bool erase(key_storage_type key) {
        size_t h = hash_key(key);
        for(;;) {
            auto ctrl = packed_keys_[h].get_ctrl();
            if (ctrl == CTRL_EMPTY) return false;
            if (ctrl == CTRL_OCCUPIED && packed_keys_[h].get_key() == key) {
                packed_keys_[h].set_ctrl(CTRL_TOMBSTONE);
                --size_;
                return true;
            }
            h = (h + 1) & cap_m1_;
        }
    }
    // [] operator
    Value& operator[](key_storage_type key) {
        Value* x = find(key);
        if (x) return *x;
        insert(key, Value{});
        return *find(key);
    }
    // Iterator (trivial version)
    struct iterator {
        ClearyTable* tab;
        size_t idx;
        void skip() { while(idx < tab->capacity() && !tab->packed_keys_[idx].occupied()) ++idx; }
        iterator(ClearyTable* t, size_t i) : tab(t), idx(i) { skip(); }
        iterator& operator++() { ++idx; skip(); return *this; }
        bool operator!=(const iterator& other) const { return idx != other.idx; }
        std::pair<key_storage_type, Value&> operator*() { return {tab->packed_keys_[idx].get_key(), tab->values_[idx]}; }
    };
    using const_iterator = iterator;

    iterator begin() { return iterator(this,0);}
    iterator end() { return iterator(this,capacity());}

    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend()   const noexcept { return end(); }

};

} // namespace valla

// --- USAGE/TEST ---
// #define CLEARY_TINY_TABLE_TEST
#ifdef CLEARY_TINY_TABLE_TEST

#include <iostream>
int main() {
    using Table = valla::ClearyTable<16, int>; // Key: 14 bits, Value: int
    Table t(16);
    for (int k=0; k<20; ++k) t.insert(k, k*k);
    for (int k=0; k<20; ++k) {
        int* v = t.find(k);
        assert(v && *v == k*k);
    }
    t.erase(0);
    assert(!t.find(0));
    for (auto kv : t) { std::cout << kv.first << '=' << kv.second << '\n'; }
    std::cout << "OK\n";
    return 0;
}
#endif