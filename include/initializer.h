//
// Created by zblickensderfer on 4/22/2025.
//

#pragma once

/*
 *  This includes libraries for the linker which are used to support multple VAs concurrently mapped to the same
 *  allocated physical page for the process.
 */
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "onecore.lib")

#include "config.h"
#include "pfn.h"
#include "pte.h"
#include "page_list.h"
#include "disk.h"
#include "threads.h"
#include "trimmer.h"
#include "writer.h"
#include "page_fault_handler.h"
#include "ager.h"
#include "scheduler.h"
#include "simulator.h"

/*
 *  Initialize all global data structures. Called at startup.
 */
void initialize_system(void);