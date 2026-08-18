#pragma once
// Minimal assertion macros for host unit tests. No external deps; each
// EXPECT_* prints on failure and increments the suite's failure counter,
// then continues with the rest of the suite (no early-out on first fail).

#include <stdio.h>
#include <string.h>

// Each test function takes an `int *fails` and bumps it on failure.
#define FAILF(fmt, ...) do {                                          \
    fprintf(stderr, "  FAIL %s:%d (%s): " fmt "\n",                   \
            __FILE__, __LINE__, __func__, ##__VA_ARGS__);             \
    ++*fails;                                                         \
} while (0)

#define EXPECT_EQ(actual, expected) do {                              \
    long long _a = (long long)(actual);                               \
    long long _e = (long long)(expected);                             \
    if (_a != _e) FAILF("%s == %lld, expected %lld", #actual, _a, _e); \
} while (0)

#define EXPECT_TRUE(cond) do {                                        \
    if (!(cond)) FAILF("%s", #cond);                                  \
} while (0)

#define EXPECT_FALSE(cond) do {                                       \
    if ((cond)) FAILF("!%s", #cond);                                  \
} while (0)

#define EXPECT_STREQ(actual, expected) do {                           \
    const char *_a = (actual);                                        \
    const char *_e = (expected);                                      \
    if (!_a || !_e || strcmp(_a, _e) != 0) {                          \
        FAILF("%s == \"%s\", expected \"%s\"",                        \
              #actual, _a ? _a : "(null)", _e ? _e : "(null)");       \
    }                                                                 \
} while (0)
