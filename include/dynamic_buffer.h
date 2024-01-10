#pragma once

#include <vector>
#include <default_init_allocator.h>

// Reusable dynamically allocated buffer class
// Basically a wrapper for std::vector, but permits casting to a pointer type
// This lets us provide a RAII-safe abstrction that's close to a drop-in replacement for a C-style pointer.
//
// We go the extra mile to support an allocator so that we can track memory pool utilization, eg. 
// tracking effect buffer memory.

template<typename T, class Allocator = default_init_allocator<T>>
class dynamic_buffer : public std::vector<T, Allocator> {
  public:
  // Forward constructors, C++11 style!
  using std::vector<T, Allocator>::vector;

  // Avoid ambiguity
  using std::vector<T, Allocator>::operator[];

  // Expose a cast of the data pointer, for implementation convenience
  explicit operator const T*() const { return this->data(); }
  explicit operator T*() { return this->data(); }

  // Also permit void*
  explicit operator const void*() const { return this->data(); }
  explicit operator void*() { return this->data(); }

  // And a built in reinterpret
  template<typename Z>
  const Z* as() const { return reinterpret_cast<const Z*>(this->data()); }
  template<typename Z>
  Z* as() { return reinterpret_cast<Z*>(this->data()); }

  // Also an offset operator
  const T* operator+(int i) const { return this->data() + i; }
  T* operator+(int i) { return this->data() + i; }
};
