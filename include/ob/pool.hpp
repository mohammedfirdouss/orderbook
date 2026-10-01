#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace ob {

// Fixed-capacity object pool. All storage is allocated once in the
// constructor, so allocate() and release() never touch the heap.
template <class T>
class Pool {
public:
    explicit Pool(std::size_t capacity) : storage_(std::make_unique<T[]>(capacity)) {
        free_.reserve(capacity);
        // Push in reverse so the first allocations hand out slots 0, 1, 2...
        // which sit next to each other in memory.
        for (std::size_t i = capacity; i-- > 0;) free_.push_back(&storage_[i]);
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    // Returns nullptr when the pool is exhausted.
    T* allocate() {
        if (free_.empty()) return nullptr;
        T* p = free_.back();
        free_.pop_back();
        return p;
    }

    void release(T* p) { free_.push_back(p); }

    std::size_t available() const { return free_.size(); }

private:
    std::unique_ptr<T[]> storage_;
    std::vector<T*> free_;
};

}  // namespace ob
