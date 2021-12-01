#ifndef ALGORITHMS_ARRAY_POOL_H
#define ALGORITHMS_ARRAY_POOL_H

#include <cassert>
#include <vector>

/*
  ArrayPool is intended as a compact representation of a large collection of
  arrays that are allocated individually but deallocated together.

  The code below is a templatized version of the ArrayPool variant in the
  heuristics directory.
*/
namespace array_pool_template {
template<typename Value>
class ArrayPool;

template<typename Value>
class ArrayPoolSlice {
public:
    using Iterator = typename std::vector<Value>::const_iterator;
    Iterator begin() const {
        return first;
    }

    Iterator end() const {
        return last;
    }
private:
    friend class ArrayPool<Value>;

    Iterator first;
    Iterator last;

    ArrayPoolSlice(Iterator first, Iterator last)
        : first(first),
          last(last) {
    }
};

template<typename Value>
class ArrayPool {
    std::vector<Value> data;
    // First indices of all stored vectors plus first index for the next vector.
    std::vector<int> positions;
public:
    ArrayPool()
        : positions({0}) {
    }

    void push_back(std::vector<Value> &&vec) {
        data.insert(
            data.end(),
            std::make_move_iterator(vec.begin()),
            std::make_move_iterator(vec.end()));
        positions.push_back(data.size());
    }

    ArrayPoolSlice<Value> get_slice(int index) const {
        assert(index >= 0 && index < size());
        typename ArrayPoolSlice<Value>::Iterator first = data.begin() + positions[index];
        typename ArrayPoolSlice<Value>::Iterator last = data.begin() + positions[index + 1];
        return ArrayPoolSlice<Value>(first, last);
    }

    void reserve(int num_vectors, int total_num_entries) {
        data.reserve(total_num_entries);
        positions.reserve(num_vectors + 1);
    }

    int size() const {
        return positions.size() - 1;
    }
};
}

#endif
