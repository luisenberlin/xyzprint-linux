# SPDX-License-Identifier: MIT
#
# Esqueleto del port Linux de XYZprint (da Vinci 1.0 Pro).
# Compila una CLI de referencia antes que la UI.
#
# Dependencias en Debian/Ubuntu:
#   qt6-base-dev qt6-serialport-dev libssl-dev cmake ninja-build g++
#   cura-engine (solo para el subcomando slice)
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   ./build/xyzprint-cli ports

cmake_minimum_required(VERSION 3.21)

project(xyzprint-cli
    VERSION 0.1.0
    DESCRIPTION "CLI reconstruida para XYZprint (da Vinci 1.0 Pro)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core SerialPort)
find_package(OpenSSL REQUIRED)

set(CMAKE_AUTOMOC ON)

add_executable(xyzprint-cli
    main.cpp
    slicer.cpp
    slicer.h
    threew.cpp
    threew.h
    xyzserial.cpp
    xyzserial.h)

target_link_libraries(xyzprint-cli PRIVATE
    Qt6::Core
    Qt6::SerialPort
    OpenSSL::Crypto)

target_compile_options(xyzprint-cli PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wpedantic>)

install(TARGETS xyzprint-cli RUNTIME DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../profiles/davinci-1.0-pro-pla.json"
        DESTINATION share/xyzprint/profiles)

option(XYZPRINT_BUILD_TESTS "Compilar los tests del códec .3w" ON)
if(XYZPRINT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
