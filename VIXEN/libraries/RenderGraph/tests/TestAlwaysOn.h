#pragma once

#include <cstdio>
#include <cstdlib>

// Standalone CTest programs must keep their checks live in every build configuration.
#define VIXEN_TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "VIXEN_TEST_CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while (false)
