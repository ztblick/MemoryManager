//
// Created by zblickensderfer on 5/5/2025.
//

#pragma once
#include "initializer.h"
#include "disk.h"

#define MAX_DISK_SLOT_ATTEMPTS      (MAX_WRITE_BATCH_SIZE * 2)

/*
 *  This function is called by the CreateThread function in initializer.
 *  This function waits for the system start event, then waits for
 *  either the write pages event (which can be called multiple times) or the
 *  system exit event (which terminates it).
 */
VOID write_pages_thread(VOID);