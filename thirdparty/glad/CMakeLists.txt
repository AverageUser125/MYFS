cmake_minimum_required(VERSION 3.6)
project(glad)

if (POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
endif()

add_library(glad)
target_sources(glad PRIVATE src/glad.c)
target_include_directories(glad PUBLIC include)