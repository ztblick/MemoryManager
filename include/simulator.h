//
// Created by zblickensderfer on 5/5/2025.
//

#pragma once

// This switch is used to determine if statistics will be logged in the console or not.
#define LOGGING_MODE                    0
#define RUN_FOREVER                     0

// This is the number of times the simulator will access a VA.
#define ITERATIONS                      (MB(1))

// In run forever mode, this is the initial number of threads.
#define DEFAULT_USER_THREAD_COUNT       8

/*
 *  Provides an arbitrary VA from the application's VA space.
 */
PULONG_PTR get_arbitrary_va(PULONG_PTR p);

void run_user_app_simulation(void);

void free_all_data_and_shut_down(void);
