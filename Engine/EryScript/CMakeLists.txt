cmake_minimum_required(VERSION 3.25)
project(eryscript LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_REQUIRED_FLAGS "-freflection")
endif()
check_cxx_source_compiles([=[
    #include <meta>
    struct ReflectionProbe { int value; };
    static_assert(std::meta::identifier_of(^^ReflectionProbe) == "ReflectionProbe");
    static_assert(std::meta::nonstatic_data_members_of(
        ^^ReflectionProbe, std::meta::access_context::current()).size() == 1);
    int main() { return 0; }
]=] ERYSCRIPT_HAS_CPP26_REFLECTION)
cmake_pop_check_state()
if(NOT ERYSCRIPT_HAS_CPP26_REFLECTION)
    message(FATAL_ERROR
        "C++26 reflection is required. Use MSYS2 UCRT64 GCC 16 or newer; "
        "Engine/build.ps1 selects it automatically. MSVC 19.44 does not support reflection.")
endif()

add_library(eryscript STATIC src/eryscript.cpp)
target_include_directories(eryscript PUBLIC include)
target_compile_features(eryscript PUBLIC cxx_std_26)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(eryscript PUBLIC -freflection)
endif()
if(MINGW)
    # Keep Windows executables independent of GCC runtime DLL search paths.
    target_link_options(eryscript INTERFACE -static)
endif()
set_target_properties(eryscript PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(eryscript PRIVATE /W4 /permissive-)
else()
    target_compile_options(eryscript PRIVATE -Wall -Wextra -Wpedantic)
endif()
