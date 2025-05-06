cmake_minimum_required(VERSION 3.31)
project(MemoryManager C)

set(CMAKE_C_STANDARD 11)

add_executable(MemoryManager initializer.c
        initializer.h
        macros.h
        pfn.h
        pte.h
        page_fault_handler.c
        page_fault_handler.h
        writer.c
        writer.h
        simulator.c
        simulator.h
        ager.c
        ager.h
        trimmer.c
        trimmer.h
        scheduler.c
        scheduler.h)
