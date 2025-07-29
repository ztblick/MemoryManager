/*
 * Created by Zach Blick on 5/6/2025.
 * Scheduler collects statistics on page consumption.
 * It then calls ager, trimmer, and writer to provide just enough pages
 * to keep simulator running smoothly.
 *
 * In the future, it will ideally analyze consumption to look for patterns,
 * such as sequential page access vs. random page access.
 *
 * For now, it simply calls ager, trimmer, and writer sequentially.
*/

#pragma once
#include "initializer.h"

#define SCHEDULER_DELAY_IN_MILLISECONDS         1
#define PRINT_FREQUENCY_IN_MILLISECONDS         (60 / SCHEDULER_DELAY_IN_MILLISECONDS)

/*
 * Analyzes consumption, then calls ager, trimmer, and writer appropriately.
 */
VOID schedule_tasks(VOID);

/*
 *  Print page consumption statistics.
 */
VOID print_statistics(VOID);