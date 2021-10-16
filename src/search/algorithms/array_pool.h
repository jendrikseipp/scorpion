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

const int INVALID_INDEX = -1;

template<typename Value>
class ArrayPoolIndex {
    friend class ArrayPool<Value>;
    int position;
    ArrayPoolIndex(int position)
        : position(position) {
    }
public:
    ArrayPoolIndex()
        : position(INVALID_INDEX) {
    }
};

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
    std::vector<int> positions;
public:
    ArrayPoolIndex<Value> append(std::vector<Value> &&vec) {
        ArrayPoolIndex<Value> index(data.size());
        positions.push_back(data.size());
        data.insert(
            data.end(),
            std::make_move_iterator(vec.begin()),
            std::make_move_iterator(vec.end()));
        return index;
    }

    ArrayPoolIndex<Value> append(const std::vector<Value> &vec) {
        ArrayPoolIndex<Value> index(data.size());
        positions.push_back(data.size());
        data.insert(data.end(), vec.begin(), vec.end());
        return index;
    }

    ArrayPoolSlice<Value> get_slice(ArrayPoolIndex<Value> index, int size) const {
        assert(index.position >= 0 &&
               size >= 0 &&
               index.position + size <= static_cast<int>(data.size()));
        return ArrayPoolSlice<Value>(
            data.begin() + index.position, data.begin() + index.position + size);
    }

    ArrayPoolSlice<Value> get_slice(int index) const {
        typename ArrayPoolSlice<Value>::Iterator first = data.begin() + positions[index];
        typename ArrayPoolSlice<Value>::Iterator last = (index == size() - 1)
            ? data.end()
            : data.begin() + positions[index + 1];
        return ArrayPoolSlice<Value>(first, last);
    }

    void reserve(int num_vectors, int total_num_entries) {
        data.reserve(total_num_entries);
        positions.reserve(num_vectors);
    }

    int size() const {
        return positions.size();
    }
};
}

#endif
