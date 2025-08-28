//
// Created by zblickensderfer on 5/5/2025.
//

#pragma once

#include "releaser.h"
#include "initializer.h"

// This switch is used to determine if statistics will be logged in the console or not.
#define LOGGING_MODE                    0
#define RUN_FOREVER                     0

void run_user_app_simulation(PTHREAD_INFO user_thread_info);