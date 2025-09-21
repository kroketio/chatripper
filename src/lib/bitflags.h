#pragma once
#include <iostream>
#include <type_traits>

// generic bitmask helper for any enum class

template <typename E>
struct Flags {
  using T = std::underlying_type_t<E>;
  T bits;

  constexpr explicit Flags(T b = 0) : bits(b) {}

  // set one or more
  template <typename... Es>
  constexpr void set(E e, Es... es) {
    bits |= static_cast<T>(e);
    if constexpr (sizeof...(es) > 0) set(es...);
  }

  // clear one or more
  template <typename... Es>
  constexpr void clear(E e, Es... es) {
    bits &= ~static_cast<T>(e);
    if constexpr (sizeof...(es) > 0) clear(es...);
  }

  // contains
  constexpr bool has(E e) const { return (bits & static_cast<T>(e)) != 0; }

  // sad
  [[nodiscard]] constexpr bool empty() const {
    return bits == 0;
  }

  // equality
  constexpr bool operator==(const Flags& other) const {
    return bits == other.bits;
  }
  constexpr bool operator!=(const Flags& other) const {
    return bits != other.bits;
  }
};