# Host (native) unit tests for S3Watch pure-logic modules.
#
# These run on the build host, not the ESP32. They exercise extracted
# header-only helpers under components/*/include/ that have no ESP-IDF
# dependencies. The firmware build includes the same headers, so a passing
# test here means the same code in the firmware is exercised.
#
# Build + run:
#   cmake -S tests -B tests/build
#   cmake --build tests/build
#   ctest --test-dir tests/build --output-on-failure
#
# Or one-shot:
#   cmake -S tests -B tests/build && cmake --build tests/build && tests/build/s3watch_tests

cmake_minimum_required(VERSION 3.16)
project(s3watch_tests C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(
    -Wall -Wextra -Werror
    -Wno-unused-parameter
    -g -O0
)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(s3watch_tests
    test_main.c
    test_utc_tm.c
    test_ip_parse.c
    test_signalk_alert.c
)

target_include_directories(s3watch_tests PRIVATE
    .
    ${REPO_ROOT}/components/bsp_extra/include
    ${REPO_ROOT}/components/gui/include
    ${REPO_ROOT}/components/signalk_client/include
)

enable_testing()
add_test(NAME unit COMMAND s3watch_tests)
