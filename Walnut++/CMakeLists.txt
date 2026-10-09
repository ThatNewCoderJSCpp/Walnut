cmake_minimum_required(VERSION 3.18)
project(fizmo_test LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(fizmo
    GIT_REPOSITORY https://github.com/ThatNewCoderJSCpp/Fizmo.git
    GIT_TAG        v0.1.35
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Fizmo)
FetchContent_MakeAvailable(fizmo)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE fizmo::fizmo)