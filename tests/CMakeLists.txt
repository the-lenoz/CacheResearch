include(FetchContent)

find_package(GTest CONFIG QUIET)

if(NOT GTest_FOUND)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(cache_tests
    cache_test.cpp
    lru_test.cpp
    lfu_test.cpp
    two_q_test.cpp
    arc_test.cpp
    lirs_test.cpp
    belady_test.cpp
    hierarchy_test.cpp
    factory_test.cpp
    config_test.cpp
)

target_compile_features(cache_tests PRIVATE cxx_std_20)
set_target_properties(cache_tests PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(cache_tests PRIVATE cache_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(cache_tests)
