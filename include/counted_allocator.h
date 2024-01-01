#pragma once

// A C++ allocator that counts and limits memory usage
// This permits setting limits on different classes of memory, without requiring pre-allocated pools.
//
// Usage:
// Define a "tag" struct for the specific pool, with a 'pool_size' static constexpr that defines the maximum pool allocation.
// Then you can use counted_allocators with that tag, eg:
//
// struct my_pool { static constexpr size_t pool_size = 5000; /* bytes */ };
// std::vector<my_type, counted_allocator<my_type, my_pool>> my_counted_vector;
//
// For debugging, the tag type may also export a function of the signature
// static void debug(size_t alloc, bool is_allocate, size_t available);

#include <memory>


template<typename tag_t, typename = int>
struct static_counter_debug_check : std::false_type {};

template<typename tag_t>
struct static_counter_debug_check<tag_t, decltype(&tag_t::debug, 0)> : std::true_type {};


template<typename tag_t>
struct static_counter {
  static inline size_t& _available() {
    static size_t _counter = tag_t::pool_size;  // initialize on first use, to avoid needing explicit allocation somewhere else.
                                                // TODO: validate the assembly output of this.
    return _counter;
  }  

  static inline void dbg_allocate(size_t count, bool is_allocate) {
    if constexpr (static_counter_debug_check<tag_t>::value) {
      tag_t::debug(count, is_allocate, _available());
    }
  }

  // Allocator API
  static inline bool consume(size_t s) { 
    dbg_allocate(s,true);
    if (s > _available()) return false;
    _available() -= s;    
    return true;
  }
  static inline void release(size_t s) {
    dbg_allocate(s,false);
    _available() += s;    
  }
  static inline size_t available() { return _available(); };
  static inline size_t used() { return tag_t::pool_size - _available(); };
};


template <typename T, typename tag_t>
struct counted_allocator {
  typedef T value_type;
  static_counter<tag_t> counter;

  counted_allocator() noexcept {}
  template<class U> counted_allocator(const counted_allocator<U,tag_t>&) noexcept {}
  template<class U> bool operator==(const counted_allocator<U,tag_t>&) const noexcept { return true; }
  template<class U> bool operator!=(const counted_allocator<U,tag_t>&) const noexcept { return false; }

  // Ensure propagation on copies
  typedef std::true_type propagate_on_container_copy_assignment;
  typedef std::true_type propagate_on_container_move_assignment;
  typedef std::true_type propagate_on_container_swap;

  inline T* allocate(size_t n, void* = nullptr) const {
    if (counter.consume(sizeof(T[n]))) {
      return new T[n];
    } else {
      return nullptr; // out of pool space
    }
  }

  inline void deallocate(T* p, size_t n) const noexcept {
    delete[] p;
    if (p != nullptr) counter.release(n * sizeof(T));
  }

  inline size_t max_size() const noexcept {
    return counter.available() / sizeof(T);
  }
};




