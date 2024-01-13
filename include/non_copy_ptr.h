#pragma once

#include <memory> 

// non_copy_ptr
//
// A smart pointer analogous to unique_ptr, but with the semantic that copies are made empty.

template<typename T>
class non_copy_ptr : public std::unique_ptr<T> {
  public:
  non_copy_ptr() {};
  explicit non_copy_ptr(T* raw) : std::unique_ptr<T>(raw) {};
  explicit non_copy_ptr(std::unique_ptr<T> unique) : std::unique_ptr<T>(std::move(unique)) {};
  non_copy_ptr(const non_copy_ptr<T>& c) {};  // copy arrives empty
  non_copy_ptr(non_copy_ptr<T>&& c) = default;

  non_copy_ptr<T>& operator=(const non_copy_ptr<T>& c) { this->release(); return *this; };  // copy arrives empty
  non_copy_ptr<T>& operator=(non_copy_ptr<T>&& c) = default;

  // The rest of the API is inherited from std::unique_ptr
};

// Analogue of make_unique
template< class T, class... Args >
inline non_copy_ptr<T> make_non_copy( Args&&... args ) {
#if __cplusplus >= 201402L
  return non_copy_ptr<T> { std::make_unique<T>(std::forward<Args>(args)...) };
#else
  return non_copy_ptr<T>(new T(std::forward<Args>(args)...));
#endif
}


