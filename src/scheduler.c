//
// Created by zblickensderfer on 5/6/2025.
//

#include "../include/scheduler.h"

#define WRITE_DURATION_IN_MILLISECONDS      10
#define AVAILABLE_PAGE_THRESHOLD            4096
#define ADDITIONAL_PAGE_BUFFER              128

consumption_buffer consumption_rates;

VOID print_statistics(VOID) {

    ULONG64 allocated_frame_count = vm.allocated_frame_count;

    ULONG64 active_page_count = vm.allocated_frame_count -
        (*stats.n_free + *stats.n_modified + *stats.n_standby);

    printf("\n");
    printf("FREE:\t\t%llu\t\t%.2f%%\n",
        *stats.n_free, 100.0 * (double) *stats.n_free / (double) allocated_frame_count);
    printf("ACTIVE:\t\t%llu\t\t%.2f%%\n",
        active_page_count, 100.0 * active_page_count / allocated_frame_count);
    printf("MODIFIED:\t%llu\t\t%.2f%%\n",
        *stats.n_modified, 100.0 * *stats.n_modified / allocated_frame_count);
    printf("STANDBY:\t%llu\t\t%.2f%%\n",
        *stats.n_standby, 100.0 * *stats.n_standby / allocated_frame_count);
    printf("\nEMPTY DISK SLOTS:\t%lld\n", pf.empty_disk_slots);
    printf("\nHARD:\t\t%llu\t\t%.2f%%\n",
        stats.n_hard, 100.0 * stats.n_hard / (stats.n_hard + stats.n_soft));
    printf("SOFT:\t\t%llu\t\t%.2f%%\n",
        stats.n_soft, 100.0 * stats.n_soft / (stats.n_hard + stats.n_soft));
    printf("\nTotal time user threads spent waiting: %.3f s\n", (double) stats.wait_time / stats.timer_frequency);
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
    ULONG64 previous_available_count;
    ULONG64 current_available_count = stats.n_available;
    double consumption_rate;
    memset(&consumption_rates, 0, sizeof(consumption_data) * NUMBER_OF_SAMPLES);

    WaitForSingleObject(system_start_event, INFINITE);

    while (TRUE) {

        // Update statistics for next iteration
        previous_timestamp = current_timestamp;
        previous_available_count = current_available_count;

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
        current_available_count = stats.n_available;

        // Get the consumption rate of available pages
        consumption_rate = ((double) previous_available_count - (double) current_available_count) / elapsed;
        if (consumption_rate < 0) continue;

#if STATS_MODE
        add_consumption_data(consumption_rate, current_available_count);
#endif

        // Make a projection for the state of our machine after writing pages to disk
        double expected_consumption_during_write = consumption_rate * WRITE_DURATION_IN_MILLISECONDS / 1000.0;
        double expected_pages_after_write = current_available_count - expected_consumption_during_write;

        // If there is no need to write, don't write!
        if (expected_pages_after_write < AVAILABLE_PAGE_THRESHOLD) SetEvent(initiate_trimming_event);

        // If there is a need, figure out how big the need is
        stats.writer_batch_target = *stats.n_modified;
        if (expected_pages_after_write > 0) {
            stats.writer_batch_target = expected_consumption_during_write + ADDITIONAL_PAGE_BUFFER;
        }
        SetEvent(initiate_writing_event);
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
    }
    double average_rate = (double) total_rate_sum / count;

    printf("Average rate: %.3f pages / s\n", average_rate);
    printf("Highest rate: %.3f pages / sec\n\n", largest_rate);
    printf("~~~~~~~~~~~~~~~~~~~~~\n");

}