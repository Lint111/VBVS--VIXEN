#pragma once

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cmath>
#include <codecvt>
#include <locale>

#include <FidelityFX/host/ffx_util.h>

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif

using std::floor;
using std::log2;
using std::memcpy;
using std::memset;
using std::strcmp;
using std::strlen;
using std::wcscmp;

inline int sprintf_s(char* buffer, std::size_t bufferSize, const char* format, ...) {
    if (!buffer || bufferSize == 0 || !format) return EINVAL;
    std::va_list args;
    va_start(args, format);
    const int result = std::vsnprintf(buffer, bufferSize, format, args);
    va_end(args);
    if (result < 0 || static_cast<std::size_t>(result) >= bufferSize) {
        buffer[0] = '\0';
        return result < 0 ? EINVAL : ERANGE;
    }
    return result;
}

inline int strcpy_s(char* destination, std::size_t destinationSize, const char* source) {
    if (!destination || destinationSize == 0 || !source) return EINVAL;
    const std::size_t sourceLength = std::strlen(source);
    if (sourceLength >= destinationSize) {
        destination[0] = '\0';
        return ERANGE;
    }
    std::memcpy(destination, source, sourceLength + 1);
    return 0;
}

inline int wcscpy_s(wchar_t* destination, std::size_t destinationSize,
                    const wchar_t* source) {
    if (!destination || destinationSize == 0 || !source) return EINVAL;
    const std::size_t sourceLength = std::wcslen(source);
    if (sourceLength >= destinationSize) {
        destination[0] = L'\0';
        return ERANGE;
    }
    std::wmemcpy(destination, source, sourceLength + 1);
    return 0;
}

template <std::size_t N>
inline int wcscpy_s(wchar_t (&destination)[N], const wchar_t* source) {
    return wcscpy_s(destination, N, source);
}
