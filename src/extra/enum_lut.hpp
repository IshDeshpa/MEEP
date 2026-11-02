#pragma once

#include "cuda_compat.hpp"

#include <cstddef>
#include <type_traits>

template <typename E, typename T>
struct EnumLUT {
    // E must be an unsigned enum with a COUNT member (the size of the table)
    static_assert(std::is_enum_v<E>, "EnumLUT key must be an enum");
    static_assert(std::is_unsigned_v<std::underlying_type_t<E>>,
                  "EnumLUT key must have an unsigned underlying type");

    static constexpr std::size_t size =
        static_cast<std::size_t>(E::COUNT);

    const T LUT_ARR[size == 0 ? 1 : size];

    MEEP_HOST_DEVICE constexpr const T& operator[](E e) const {
        return LUT_ARR[static_cast<std::size_t>(e)];
    }
};

// Namespace-scope tables have independent, statically initialized host and
// device storage. Internal linkage allows this helper in headers, with or
// without CUDA relocatable device code. No runtime copy is required.
#if defined(__CUDA_ARCH__)
#define MEEP_ENUM_LUT_STORAGE static __constant__
#else
#define MEEP_ENUM_LUT_STORAGE static constexpr
#endif

// Values must be constant-initializable. Use [] in either execution space;
// references and addresses still refer to that execution space's own table.
#define DEFINE_ENUM_LUT(name, enum_type, value_type, ...) \
    MEEP_ENUM_LUT_STORAGE EnumLUT<enum_type, value_type> name{{__VA_ARGS__}}
