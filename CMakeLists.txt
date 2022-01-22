cmake_minimum_required(VERSION 3.13.4)

idf_component_register(
        SRCS src/button.c
        INCLUDE_DIRS include
        REQUIRES freertos driver log
)
