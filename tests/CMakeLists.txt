add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../extern/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest)

add_executable(tests tests.cpp)

target_link_libraries(tests
    concurrency::concurrency
    GTest::gtest_main
    pthread)

target_compile_features(tests PRIVATE cxx_std_26)
