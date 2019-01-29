#ifndef ALGORITHMS_ARRAY_POOL_H
#define ALGORITHMS_ARRAY_POOL_H

#include <cassert>
#include <vector>

/*
  ArrayPool is intended as a compact representation of a large collection of
  arrays that are allocated individually but deallocated together.

  Each array may have a different size, but ArrayPool does not keep track of
  the array sizes; its user must maintain this information themselves. See the
  relaxation heuristics for usage examples.

  If the class turns out to be more generally useful, it could be templatized
  (currently, ValueType = int is hardcoded) and moved to the algorithms
  directory.
*/
namespace array_pool {
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
    Iterator begin() {
        return first;
    }

    Iterator end() {
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
public:
    ArrayPoolIndex<Value> append(const std::vector<Value> &vec) {
        ArrayPoolIndex<Value> index(data.size());
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
};
}

#endif
