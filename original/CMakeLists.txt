# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# Claude Code VSCode extension injects GIT_CONFIG_* env vars that set
# safe.bareRepository=explicit, breaking the IDF component manager's bare
# git repo cache. Unset them here so subprocesses (including the Python
# component manager and its git calls) inherit a clean environment.
unset(ENV{GIT_CONFIG_COUNT})
unset(ENV{GIT_CONFIG_KEY_0})
unset(ENV{GIT_CONFIG_VALUE_0})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
add_compile_options(-Wno-format)
project(S3Watch)
