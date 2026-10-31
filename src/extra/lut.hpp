#pragma once

#include "cuda_compat.hpp"

#include <cstddef>
#include <type_traits>

template <typename E, typename T>
requires std::is_enum_v<E>
      && std::is_unsigned_v<std::underlying_type_t<E>>
      && requires { E::COUNT; }
struct EnumLUT {
    static constexpr std::size_t size =
        static_cast<std::size_t>(E::COUNT);

    // Inline storage can be indexed on either host or device. Keep this an
    // aggregate so brace initialization works in both execution spaces.
    const T LUT_ARR[size == 0 ? 1 : size];

    MEEP_HOST_DEVICE constexpr const T& operator[](E e) const {
        return LUT_ARR[static_cast<std::size_t>(e)];
    }
};
