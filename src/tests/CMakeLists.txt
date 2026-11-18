# SPDX-License-Identifier: MIT
#
# Tests del códec .3w y del perfil de CuraEngine. Ejecutar desde el build:
#   cmake --build build && ctest --test-dir build --output-on-failure

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_threew
    test_threew.cpp
    ../threew.cpp
    ../threew.h)

target_link_libraries(test_threew PRIVATE
    Qt6::Test
    Qt6::Core
    OpenSSL::Crypto)

add_test(NAME threew COMMAND test_threew)

add_executable(test_profile
    test_profile.cpp
    ../slicer.cpp
    ../slicer.h)

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../.." XYZPRINT_SOURCE_DIR)
target_compile_definitions(test_profile PRIVATE
    XYZPRINT_SOURCE_DIR="${XYZPRINT_SOURCE_DIR}")

target_link_libraries(test_profile PRIVATE
    Qt6::Test
    Qt6::Core)

add_test(NAME profile COMMAND test_profile)
