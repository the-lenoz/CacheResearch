cmake_minimum_required(VERSION 3.28)

project(CacheResearch LANGUAGES CXX)

option(CACHE_RESEARCH_BUILD_TESTS "Build CacheResearch tests" OFF)

add_library(cache_lib)

target_compile_features(cache_lib PUBLIC cxx_std_20)
set_target_properties(cache_lib PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_SCAN_FOR_MODULES ON
)

target_sources(cache_lib
    PUBLIC
        FILE_SET CXX_MODULES FILES
            src/cache/cache.cppm
            src/lru/lru.cppm
            src/lfu/lfu.cppm
            src/two_q/two_q.cppm
            src/arc/arc.cppm
            src/lirs/lirs.cppm
            src/belady/belady.cppm
            src/hierarchy/hierarchy.cppm
            src/factory/factory.cppm
            src/config/config.cppm
    PRIVATE
        src/cache/cache.cpp
        src/lru/lru.cpp
        src/lfu/lfu.cpp
        src/two_q/two_q.cpp
        src/arc/arc.cpp
        src/lirs/lirs.cpp
        src/belady/belady.cpp
        src/hierarchy/hierarchy.cpp
        src/factory/factory.cpp
        src/config/config.cpp
)

add_executable(cache_sim src/main.cpp)
target_compile_features(cache_sim PRIVATE cxx_std_20)
set_target_properties(cache_sim PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(cache_sim PRIVATE cache_lib)

if(CACHE_RESEARCH_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
