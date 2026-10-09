cmake_minimum_required(VERSION 3.18)
project(fizmo_test LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(fizmo
    GIT_REPOSITORY https://github.com/ThatNewCoderJSCpp/Fizmo.git
    GIT_TAG        v0.1.36
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Fizmo)
FetchContent_MakeAvailable(fizmo)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE fizmo::fizmo)

find_package(Freetype QUIET)
if(FREETYPE_INCLUDE_DIRS)
    target_include_directories(main PRIVATE ${FREETYPE_INCLUDE_DIRS})
endif()

get_target_property(WALNUT_FIZMO_INTERFACE_INCLUDES fizmo INTERFACE_INCLUDE_DIRECTORIES)
string(REGEX MATCH "BUILD_INTERFACE:([^>]*)>" WALNUT_FIZMO_MATCH "${WALNUT_FIZMO_INTERFACE_INCLUDES}")
set(WALNUT_RUNTIME_INCLUDE_LIST ${CMAKE_MATCH_1})
if(FREETYPE_INCLUDE_DIRS)
    list(APPEND WALNUT_RUNTIME_INCLUDE_LIST ${FREETYPE_INCLUDE_DIRS})
endif()
list(REMOVE_DUPLICATES WALNUT_RUNTIME_INCLUDE_LIST)
list(JOIN WALNUT_RUNTIME_INCLUDE_LIST ";" WALNUT_RUNTIME_INCLUDES)
set(WALNUT_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Runtime")
configure_file(walnut_config.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/walnut_config.hpp" @ONLY)

add_executable(walnut walnut.cpp)
target_include_directories(walnut PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(walnut PRIVATE fizmo::fizmo)
if(FREETYPE_INCLUDE_DIRS)
    target_include_directories(walnut PRIVATE ${FREETYPE_INCLUDE_DIRS})
endif()

enable_testing()

file(GLOB WALNUT_E2E_CASES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/*.wal")
foreach(case ${WALNUT_E2E_CASES})
    get_filename_component(case_name "${case}" NAME_WE)
    add_test(NAME e2e.${case_name}
        COMMAND ${CMAKE_COMMAND} -DWALNUT=$<TARGET_FILE:walnut> -DCASE=${case} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/test-work
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_e2e.cmake")
    set_tests_properties(e2e.${case_name} PROPERTIES TIMEOUT 300)
endforeach()

file(GLOB WALNUT_SEMANTIC_SUITES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic/*.txt")
foreach(suite ${WALNUT_SEMANTIC_SUITES})
    get_filename_component(suite_name "${suite}" NAME_WE)
    add_test(NAME semantic.${suite_name}
        COMMAND ${CMAKE_COMMAND} -DWALNUT=$<TARGET_FILE:walnut> -DSUITE=${suite} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/test-work
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_semantic.cmake")
    set_tests_properties(semantic.${suite_name} PROPERTIES TIMEOUT 900)
endforeach()
