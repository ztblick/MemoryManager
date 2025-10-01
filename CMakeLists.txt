cmake_minimum_required(VERSION 3.31)
project(MemoryManager C)

set(CMAKE_C_STANDARD 11)

add_executable(MemoryManager threads/initializer.c
        threads/initializer.h
        data_structures/pfn.h
        data_structures/pte.h
        threads/page_fault_handler.c
        threads/page_fault_handler.h
        threads/writer.c
        threads/writer.h
        threads/simulator.c
        threads/simulator.h
        threads/ager.c
        threads/ager.h
        threads/trimmer.c
        threads/trimmer.h
        threads/scheduler.c
        threads/scheduler.h
        utils/debug.c
        utils/debug.h
        data_structures/pte.c
        data_structures/pfn.c
        data_structures/page_list.h
        data_structures/page_list.c
        threads/releaser.c
        threads/releaser.h
        data_structures/locks.h
        data_structures/locks.c
        data_structures/disk.c
        data_structures/disk.h
        utils/config.h
        threads/threads.h
        utils/utils.c
        utils/utils.h
        threads/threads.c
        threads/pruner.c
        threads/pruner.h
)
