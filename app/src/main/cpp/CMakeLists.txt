cmake_minimum_required(VERSION 3.22.1)
project(dgcore LANGUAGES CXX)

add_library(dgcore SHARED
    native-lib.cpp
    console/command_parser.cpp
)

target_compile_features(dgcore PRIVATE cxx_std_17)
target_compile_options(dgcore PRIVATE -O2 -ffast-math -fno-rtti)

target_link_libraries(dgcore
    android
    log
    GLESv2
)
