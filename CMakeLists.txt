cmake_minimum_required(VERSION 3.31)
project(MemoryManager C)

set(CMAKE_C_STANDARD 11)

add_executable(MemoryManager vm.c
        vm.h)
