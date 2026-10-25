cmake_minimum_required(VERSION 3.20)
project(VirtualDashboard LANGUAGES CXX)

set(FH2MOD_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../FH2-Mod-Platform-v2.7.1/include")

add_library(VirtualDashboard SHARED VirtualDashboard.cpp)
target_compile_features(VirtualDashboard PRIVATE cxx_std_17)
target_include_directories(VirtualDashboard PRIVATE "${FH2MOD_INCLUDE_DIR}")
target_compile_definitions(VirtualDashboard PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(VirtualDashboard PRIVATE ws2_32 xinput)
set_target_properties(VirtualDashboard PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" u)
    set_target_properties(VirtualDashboard PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${u} "${CMAKE_CURRENT_SOURCE_DIR}/dist"
        LIBRARY_OUTPUT_DIRECTORY_${u} "${CMAKE_CURRENT_SOURCE_DIR}/dist")
endforeach()
