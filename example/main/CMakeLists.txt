cmake_minimum_required(VERSION 3.13.4)

idf_component_register(
        SRCS
        button_example_main.c
        button_cpp.cpp
        INCLUDE_DIRS .
)
