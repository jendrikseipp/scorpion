#ifndef ALGORITHMS_SMALL_VECTOR_H
#define ALGORITHMS_SMALL_VECTOR_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <new>
#include <utility>

/*
  SmallVector<T, N> is a sequence container with a std::vector-like interface
  that stores its first N elements in an inline buffer, avoiding a heap
  allocation for the common case where the size never exceeds N. It spills to
  the heap only once it grows past N elements.

  This is the well-known "small buffer optimization" used by LLVM's SmallVector
  and Abseil's InlinedVector. We follow Abseil's compact layout: the inline
  buffer and the heap pointer share storage in a union, and the capacity field
  doubles as the discriminator (capacity <= N means the data lives inline).
  As a result sizeof(SmallVector<T, N>) equals that of a std::vector as long as
  N * sizeof(T) does not exceed the inline budget, so swapping it in for a
  std::vector member does not grow the enclosing object.

  Size and capacity are stored as 32-bit counts: the container is meant for
  short sequences (the motivating use is per-atom argument lists during
  grounding), so 2^32 elements is far more than enough and keeps the header
  small.
*/

namespace small_vector {
template<typename T, std::size_t N>
class SmallVector {
    static_assert(N > 0, "inline capacity must be positive");

    std::uint32_t size_ = 0;
    std::uint32_t capacity_ = N;
    union Storage {
        alignas(T) std::byte inline_buffer[N * sizeof(T)];
        T *heap;
        Storage() noexcept {}
    } storage_;

    bool is_inline() const noexcept { return capacity_ <= N; }

    T *data_ptr() noexcept {
        return is_inline() ? std::launder(reinterpret_cast<T *>(
                                 storage_.inline_buffer))
                           : storage_.heap;
    }
    const T *data_ptr() const noexcept {
        return is_inline() ? std::launder(reinterpret_cast<const T *>(
                                 storage_.inline_buffer))
                           : storage_.heap;
    }

    static T *allocate(std::size_t n) {
        return static_cast<T *>(
            ::operator new(n * sizeof(T), std::align_val_t(alignof(T))));
    }
    static void deallocate(T *p) noexcept {
        ::operator delete(p, std::align_val_t(alignof(T)));
    }

    // Move the current elements into a fresh heap buffer of capacity new_cap
    // (which must exceed the current capacity) and switch to heap storage.
    void grow(std::size_t new_cap) {
        assert(new_cap > capacity_ && new_cap <= UINT32_MAX);
        T *old = data_ptr();
        bool was_inline = is_inline();
        T *fresh = allocate(new_cap);
        std::uninitialized_move(old, old + size_, fresh);
        std::destroy(old, old + size_);
        if (!was_inline)
            deallocate(old);
        storage_.heap = fresh;
        capacity_ = static_cast<std::uint32_t>(new_cap);
    }

    // Move elements out of o assuming *this is freshly default-constructed
    // (empty, inline). Leaves o empty and inline.
    void move_from(SmallVector &&o) noexcept {
        if (o.is_inline()) {
            std::uninitialized_move(o.begin(), o.end(),
                                    std::launder(reinterpret_cast<T *>(
                                        storage_.inline_buffer)));
            size_ = o.size_;
            std::destroy(o.begin(), o.end());
            o.size_ = 0;
        } else {
            storage_.heap = o.storage_.heap;
            size_ = o.size_;
            capacity_ = o.capacity_;
            o.size_ = 0;
            o.capacity_ = N; // mark o inline so it won't free the stolen buffer
        }
    }

public:
    using value_type = T;
    using iterator = T *;
    using const_iterator = const T *;

    SmallVector() noexcept = default;

    SmallVector(std::initializer_list<T> init) {
        reserve(init.size());
        std::uninitialized_copy(init.begin(), init.end(), data_ptr());
        size_ = static_cast<std::uint32_t>(init.size());
    }

    SmallVector(const SmallVector &o) {
        reserve(o.size_);
        std::uninitialized_copy(o.begin(), o.end(), data_ptr());
        size_ = o.size_;
    }

    SmallVector(SmallVector &&o) noexcept { move_from(std::move(o)); }

    SmallVector &operator=(const SmallVector &o) {
        if (this != &o) {
            clear();
            reserve(o.size_);
            std::uninitialized_copy(o.begin(), o.end(), data_ptr());
            size_ = o.size_;
        }
        return *this;
    }

    SmallVector &operator=(SmallVector &&o) noexcept {
        if (this != &o) {
            clear();
            if (!is_inline())
                deallocate(data_ptr());
            capacity_ = N;
            move_from(std::move(o));
        }
        return *this;
    }

    ~SmallVector() {
        T *d = data_ptr();
        std::destroy(d, d + size_);
        if (!is_inline())
            deallocate(d);
    }

    void reserve(std::size_t n) {
        if (n > capacity_)
            grow(n);
    }

    void push_back(const T &value) { emplace_back(value); }
    void push_back(T &&value) { emplace_back(std::move(value)); }

    template<typename... Args>
    T &emplace_back(Args &&...args) {
        if (size_ == capacity_)
            grow(static_cast<std::size_t>(capacity_) * 2);
        T *p = std::construct_at(data_ptr() + size_, std::forward<Args>(args)...);
        ++size_;
        return *p;
    }

    void clear() noexcept {
        T *d = data_ptr();
        std::destroy(d, d + size_);
        size_ = 0;
    }

    T &operator[](std::size_t i) noexcept { return data_ptr()[i]; }
    const T &operator[](std::size_t i) const noexcept { return data_ptr()[i]; }

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    T *data() noexcept { return data_ptr(); }
    const T *data() const noexcept { return data_ptr(); }

    iterator begin() noexcept { return data_ptr(); }
    iterator end() noexcept { return data_ptr() + size_; }
    const_iterator begin() const noexcept { return data_ptr(); }
    const_iterator end() const noexcept { return data_ptr() + size_; }

    bool operator==(const SmallVector &o) const {
        return size_ == o.size_ && std::equal(begin(), end(), o.begin());
    }
};
}

#endif
