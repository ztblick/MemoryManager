cmake_minimum_required(VERSION 3.31)
project(MemoryManager C)

set(CMAKE_C_STANDARD 11)

add_executable(MemoryManager src/initializer.c
        include/initializer.h
        include/macros.h
        include/pfn.h
        include/pte.h
        src/page_fault_handler.c
        include/page_fault_handler.h
        src/writer.c
        include/writer.h
        src/simulator.c
        include/simulator.h
        src/ager.c
        include/ager.h
        src/trimmer.c
        include/trimmer.h
        src/scheduler.c
        include/scheduler.h
        src/debug.c
        include/debug.h
        src/pte.c
        src/pfn.c
        include/page_list.h
        src/page_list.c
        src/releaser.c
        include/releaser.h
        include/locks.h
        src/locks.c
        src/disk.c
        include/disk.h
)
