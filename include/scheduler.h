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
#include "disk.h"
#include "threads.h"

#define SCHEDULER_DELAY_IN_MILLISECONDS         1000

#define NUMBER_OF_CONSUMPTION_SAMPLES       512

typedef struct {
    ULONG64 consumption_rate;
    ULONG64 pages_available;
} consumption_data;

typedef struct {
    consumption_data data[NUMBER_OF_CONSUMPTION_SAMPLES];
    ULONG64 head;
} consumption_buffer;

extern consumption_buffer consumption_rates;

/*
 * Analyzes consumption, then calls ager, trimmer, and writer appropriately.
 */
VOID schedule_tasks(VOID);

/*
 *  Updates the global stats counters
 */
VOID update_statistics(VOID);

/*
 *  Print page consumption statistics.
 */
VOID print_statistics(VOID);

/*
 *  Updates consumption statistics
 */
VOID add_consumption_data(double rate, ULONG64 pages_available);

/*
    Prints out data on the run's page consumption
 */
VOID print_consumption_data(VOID);