//
// Created by zblickensderfer on 5/6/2025.
//

#include "scheduler.h"

consumption_buffer consumption_rates;

VOID print_statistics(VOID) {

    ULONG64 allocated_frame_count = vm.allocated_frame_count;

    ULONG64 active_page_count = vm.allocated_frame_count -
        (*stats.n_free + *stats.n_modified + *stats.n_standby);

    printf("\n");
    printf("FREE:\t\t%llu\t\t%.2f%%\n",
        *stats.n_free, 100.0 * (double) *stats.n_free / (double) allocated_frame_count);
    printf("ACTIVE:\t\t%llu\t\t%.2f%%\n",
        active_page_count, 100.0 * (double) active_page_count / (double) allocated_frame_count);
    printf("MODIFIED:\t%llu\t\t%.2f%%\n",
        *stats.n_modified, 100.0 * (double) *stats.n_modified / (double) allocated_frame_count);
    printf("STANDBY:\t%llu\t\t%.2f%%\n",
        *stats.n_standby, 100.0 * (double) *stats.n_standby / (double) allocated_frame_count);
    printf("\nEMPTY DISK SLOTS:\t%lld\n", pf.empty_disk_slots);
    printf("\nHARD:\t\t%llu\t\t%.2f%%\n",
        stats.n_hard, 100.0 * (double) stats.n_hard / (double) (stats.n_hard + stats.n_soft));
    printf("SOFT:\t\t%llu\t\t%.2f%%\n",
        stats.n_soft, 100.0 * (double) stats.n_soft / (double) (stats.n_hard + stats.n_soft));
    printf("\nTotal time user threads spent waiting: %.3f s\n",
                    (double) stats.wait_time / (double) stats.timer_frequency);
    printf("\nTotal hard fault misses: %llu\n", stats.hard_faults_missed);
}

VOID add_consumption_data(double rate, ULONG64 pages_available) {
    ULONG64 i = consumption_rates.head % NUMBER_OF_SAMPLES;
    consumption_rates.data[i].consumption_rate = (ULONG64) rate;
    consumption_rates.data[i].pages_available = pages_available;
    consumption_rates.head++;
}

VOID schedule_tasks(VOID) {
    DWORD status;
    LONGLONG previous_timestamp;
    LONGLONG current_timestamp = get_timestamp();
    ULONG64 previous_hard_fault_count = 0;
    ULONG64 current_hard_fault_count = 0;
    double consumption_rate;
    memset(&consumption_rates, 0, sizeof(consumption_data) * NUMBER_OF_SAMPLES);

    WaitForSingleObject(system_start_event, INFINITE);

    while (TRUE) {

        // Update statistics for next iteration
        previous_timestamp = current_timestamp;
        previous_hard_fault_count = current_hard_fault_count;

        // Print the statistics to the log, if necessary
#if LOGGING_MODE
        print_statistics();
#endif

        // Wait for a set amount of time before scheduling again.
        // Of course, if the system exit event is received, we will immediately break out of the thread.
        status = WaitForSingleObject(system_exit_event, SCHEDULER_DELAY_IN_MILLISECONDS);

        // If the system exit event is received, we will return.
        if (status == WAIT_OBJECT_0) break;

        // Get data for this iteration
        current_timestamp = get_timestamp();
        double elapsed = get_time_difference(current_timestamp, previous_timestamp);
        current_hard_fault_count = stats.n_hard;

        // ************************************************
        // * Log the consumption rate.
        // * Consumption rate is measured in faults / sec *
        // ************************************************
        consumption_rate = stats.page_consumption_per_second;
        if (current_hard_fault_count > previous_hard_fault_count)
            consumption_rate = (double) (current_hard_fault_count - previous_hard_fault_count) / elapsed;
        stats.page_consumption_per_second = consumption_rate;

#if STATS_MODE
        add_consumption_data(consumption_rate, current_hard_fault_count);
#endif
    }
}

VOID print_consumption_data(VOID) {
    ULONG64 count = min(NUMBER_OF_CONSUMPTION_SAMPLES, consumption_rates.head);

    ULONG64 total_rate_sum = 0;
    double largest_rate = 0;

    for (int i = 0; i < count; ++i) {
        ULONG64 rate = consumption_rates.data[i].consumption_rate;
        total_rate_sum += rate;
        largest_rate = max(rate, largest_rate);

        printf("Consumption rate for timestep %d:\t%llu MB/s\n", i,
                rate * PAGE_SIZE / MB(1));
    }
    double average_rate = (double) total_rate_sum / (double) count;

    printf("~~~~~~~~~~~~~~~~~~~~~\n");
    printf("\nAverage rate: %.3f MB/s\n", average_rate * PAGE_SIZE / MB(1));
    printf("Highest rate: %.3f MB/s\n", largest_rate * PAGE_SIZE / MB(1));
    printf("~~~~~~~~~~~~~~~~~~~~~\n");

}