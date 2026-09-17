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
            src/cache.cppm
            src/lru.cppm
            src/lfu.cppm
            src/two_q.cppm
            src/arc.cppm
            src/lirs.cppm
            src/cache_variant.cppm
            src/belady.cppm
            src/hierarchy.cppm
            src/factory.cppm
            src/mock_db/mock_db.cppm
            src/config/config.cppm
    PRIVATE
        src/mock_db/mock_db.cpp
        src/config/config.cpp
)

add_executable(cache_sim src/main.cpp)
target_compile_features(cache_sim PRIVATE cxx_std_20)
set_target_properties(cache_sim PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(cache_sim PRIVATE cache_lib)

find_package(Python3 3.10 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    add_custom_target(generate_configs
        COMMAND
            ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/generate_configs.py
            --clean
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Generating three-level cache configurations"
        VERBATIM
    )

    add_custom_target(generate_workloads
        COMMAND
            ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/generate_workloads.py
            --clean
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Generating categorized cache workloads"
        VERBATIM
    )

    add_custom_target(benchmark
        COMMAND
            ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/benchmark.py
            --executable $<TARGET_FILE:cache_sim>
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        DEPENDS cache_sim generate_configs generate_workloads
        COMMENT "Benchmarking every generated configuration"
        USES_TERMINAL
        VERBATIM
    )
endif()

if(CACHE_RESEARCH_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
