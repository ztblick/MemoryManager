//
// Created by zblickensderfer on 5/5/2025.
//

#pragma once



/*
 *  Provides an arbitrary VA from the application's VA space.
 */
PULONG_PTR get_arbitrary_va(PULONG_PTR p);

void run_user_app_simulation(void);

void free_all_data_and_shut_down(void);
