// S24 New - C++17 implementation of llhash removes the need for boost.

#ifndef LL_LLHASH_H
#define LL_LLHASH_H

#include <string_view>
#include <functional>

namespace detail {
    inline size_t fnv1a_hash(const char* value)
    {
        // 64-bit FNV-1a hash constants
        constexpr size_t fnv_offset_basis = 1469598103934665603ULL;
        constexpr size_t fnv_prime = 1099511628211ULL;

        size_t hash = fnv_offset_basis;
        for (; *value; ++value)
        {
            hash ^= static_cast<size_t>(*value);
            hash *= fnv_prime;
        }
        return hash;
    }
}

/// Primary: Use C++17 std::hash with std::string_view.
/// Fallback: Use FNV-1a if the computed hash is zero (for a non-empty string).
inline size_t llhash(const char* value)
{
    std::string_view strView(value);
    size_t seed = std::hash<std::string_view>{}(strView);

    // Fallback condition: if the computed seed is zero (and the string is non-empty),
    // use the FNV-1a hash instead.
    if (seed == 0 && !strView.empty())
    {
        seed = detail::fnv1a_hash(value);
    }
    return seed;
}

#endif // LL_LLHASH_H