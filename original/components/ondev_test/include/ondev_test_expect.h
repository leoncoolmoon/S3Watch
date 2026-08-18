#pragma once
// Drop-in twin of tests/test.h, but routes failures via ESP_LOGE instead of
// fprintf(stderr). The macro names + shapes match the host runner so the
// same test source files (tests/test_*.c) compile under both build systems.

#include "esp_log.h"
#include <string.h>

#define FAILF(fmt, ...) do {                                            \
    ESP_LOGE("ONDEV_TEST", "  FAIL %s:%d (%s): " fmt,                   \
             __FILE__, __LINE__, __func__, ##__VA_ARGS__);              \
    ++*fails;                                                           \
} while (0)

#define EXPECT_EQ(actual, expected) do {                                \
    long long _a = (long long)(actual);                                 \
    long long _e = (long long)(expected);                               \
    if (_a != _e) FAILF("%s == %lld, expected %lld", #actual, _a, _e);  \
} while (0)

#define EXPECT_TRUE(cond) do {                                          \
    if (!(cond)) FAILF("%s", #cond);                                    \
} while (0)

#define EXPECT_FALSE(cond) do {                                         \
    if ((cond)) FAILF("!%s", #cond);                                    \
} while (0)

#define EXPECT_STREQ(actual, expected) do {                             \
    const char *_a = (actual);                                          \
    const char *_e = (expected);                                        \
    if (!_a || !_e || strcmp(_a, _e) != 0) {                            \
        FAILF("%s == \"%s\", expected \"%s\"",                          \
              #actual, _a ? _a : "(null)", _e ? _e : "(null)");         \
    }                                                                   \
} while (0)
