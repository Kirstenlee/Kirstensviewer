/**
 * @file   fsyspath.h
 * @author Nat Goodspeed
 * @date   2024-04-03
 * @brief  Adapt our UTF-8 std::strings for std::filesystem::path
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Copyright (c) 2024, Linden Research, Inc.
 * $/LicenseInfo$
 */

#ifndef LL_FSYSPATH_H
#define LL_FSYSPATH_H

#include <filesystem>
#include <string>
#include <string_view>

// S24 Version C++20 native no boost requirement!
class fsyspath : public std::filesystem::path
{
    using super = std::filesystem::path;

    // Convert UTF‑8 std::string_view → std::filesystem::path using C++20 char8_t constructor
    static super from_utf8(std::string_view s)
    {
        // reinterpret the bytes as char8_t without copying
        const char8_t* begin = reinterpret_cast<const char8_t*>(s.data());
        const char8_t* end   = begin + s.size();
        return super(begin, end);
    }

public:
    // default
    fsyspath() = default;

    // construct from UTF‑8 encoded string
    explicit fsyspath(std::string_view utf8)
        : super(from_utf8(utf8)) {}

    explicit fsyspath(const std::string& utf8)
        : fsyspath(std::string_view(utf8)) {}

    explicit fsyspath(const char* utf8)
        : fsyspath(std::string_view(utf8)) {}

    // construct from existing path
    fsyspath(const super& p)
        : super(p) {}

    // assignments
    fsyspath& operator=(const super& p)
    {
        super::operator=(p);
        return *this;
    }

    fsyspath& operator=(std::string_view utf8)
    {
        super::operator=(from_utf8(utf8));
        return *this;
    }

    fsyspath& operator=(const std::string& utf8)
    {
        return (*this) = std::string_view(utf8);
    }

    fsyspath& operator=(const char* utf8)
    {
        return (*this) = std::string_view(utf8);
    }

    // UTF‑8 extraction
    std::string string() const
    {
        auto u8 = super::u8string();
        return std::string(u8.begin(), u8.end());
    }

    operator std::string() const
    {
        return string();
    }
};

#endif // LL_FSYSPATH_H

