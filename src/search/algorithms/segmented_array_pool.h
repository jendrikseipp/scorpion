#ifndef ALGORITHMS_SEGMENTED_ARRAY_POOL_H
#define ALGORITHMS_SEGMENTED_ARRAY_POOL_H

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

/*
  ArrayPool is intended as a compact representation of a large collection of
  arrays that are allocated individually but deallocated together.

  The code below is a templatized version of the ArrayPool variant in the
  heuristics directory.
*/
namespace segmented_array_pool_template {
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

    Value operator[](int index) const {
        assert(first + index < last);
        return *(first + index);
    }

    size_t size() const { return last-first; }
private:
    friend class ArrayPool<Value>;

    Iterator first;
    Iterator last;

    ArrayPoolSlice(Iterator first, Iterator last)
        : first(first),
          last(last) {
    }

    friend std::ostream &operator<<(std::ostream &os, const ArrayPoolSlice<Value> &slice) {
        os << "[";
        std::string sep;
        Iterator pos = slice.begin();
        while (pos != slice.end()) {
            os << sep << *pos;
            sep = ", ";
            ++pos;
        }
        return os << "]";
    }
};

template<typename Value>
class ArrayPool {
    std::vector<std::vector<Value>> m_data;
    size_t m_cur_segment;
    size_t m_cur_pos;
    size_t m_prev_pos;
    size_t m_size;

    void resize_to_fit(size_t size) {
        const auto segment_space = m_data[m_cur_segment].size() - m_cur_pos;

        if (segment_space < size) {
            const auto new_segment_size = std::max(static_cast<size_t>(std::pow(m_data[m_cur_segment].size(), 2)), size); 
            m_data.push_back(std::vector<Value>(new_segment_size));
            ++m_cur_segment;
            m_cur_pos = 0;
        }
    }

public:
    ArrayPool() : m_data(1, std::vector<int>(1)), m_cur_segment(0), m_cur_pos(0), m_prev_pos(0), m_size(0) {  }

    void push_back(const std::vector<Value>& vec) {
        resize_to_fit(vec.size());
        m_prev_pos = m_cur_pos;
        std::copy(vec.begin(), vec.end(), m_data[m_cur_segment].begin() + m_cur_pos);
        m_cur_pos += vec.size();
        ++m_size;
    }

    ArrayPoolSlice<Value> back() const {
        assert(size() > 0);
        return ArrayPoolSlice<Value>(
            m_data[m_cur_segment].begin() + m_prev_pos, 
            m_data[m_cur_segment].begin() + m_cur_pos);
    }

    void pop_back() {
        assert(size() > 0);
        m_cur_pos = m_prev_pos;
        --m_size;
    }

    int size() const {
        return m_size;
    }
};
}

#endif
