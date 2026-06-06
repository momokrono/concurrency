cmake_minimum_required(VERSION 3.20)

project(concurrency VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-interference-size")

# ── Library ────────────────────────────────────────────────────────────────

add_library(concurrency INTERFACE)
add_library(concurrency::concurrency ALIAS concurrency)

target_include_directories(concurrency INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(concurrency INTERFACE cxx_std_26)

# ── Install ────────────────────────────────────────────────────────────────

include(GNUInstallDirs)

install(TARGETS concurrency EXPORT concurrency-targets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT concurrency-targets
    FILE concurrency-config.cmake
    NAMESPACE concurrency::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/concurrency)

# ── Tests (opt-in via -DBUILD_TESTING=ON) ──────────────────────────────────

option(BUILD_TESTING "Build tests" OFF)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
