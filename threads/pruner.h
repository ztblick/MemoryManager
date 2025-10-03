//
// Created by zachb on 10/1/2025.
//

#pragma once
#include "initializer.h"

/*
    Prunes a batch of pages from standby to the free lists.
 */
VOID prune_pages_thread(void);