//
// Created by zachb on 8/7/2025.
//

#include "threads.h"

// Events
HANDLE system_start_event;
HANDLE initiate_aging_event;
HANDLE initiate_trimming_event;
HANDLE initiate_writing_event;
HANDLE initiate_pruning_event;
HANDLE standby_pages_ready_event;
HANDLE system_exit_event;

// Thread handles
PHANDLE user_threads;
HANDLE scheduling_thread;
HANDLE aging_thread;
HANDLE trimming_thread;
HANDLE writing_thread;
HANDLE pruning_thread;
HANDLE debug_thread;

// Thread IDs
PULONG user_thread_ids;
PULONG worker_thread_ids;
ULONG aging_thread_id = 3;
ULONG scheduling_thread_id = 4;

VOID update_estimated_job_time(USHORT thread_id, double most_recent_job_time) {
    double previous_estimate = stats.worker_runtimes[thread_id];
    double new_estimate = (1 - EWMA_SMOOTHING_FACTOR) * previous_estimate +
                            EWMA_SMOOTHING_FACTOR * most_recent_job_time;
    stats.worker_runtimes[thread_id] = new_estimate;
}

#if STATS_MODE
sample_buffer trim_samples;
sample_buffer write_samples;

VOID record_batch_size_and_time(double time_in_fractional_seconds,
                                ULONG64 batch_size,
                                ULONG thread_id) {

    batch_sample sample = {0};
    sample.time_in_seconds = time_in_fractional_seconds;
    sample.batch_size = batch_size;

    push_to_samples(&sample, thread_id);
}

VOID push_to_samples(batch_sample *sample, ULONG thread_id) {
    sample_buffer *buffer = &trim_samples;
    if (thread_id == WRITING_THREAD_ID) buffer = &write_samples;

    SHORT i = buffer->head % NUMBER_OF_SAMPLES;

    buffer->data[i].batch_size = sample->batch_size;
    buffer->data[i].time_in_seconds = sample->time_in_seconds;
    buffer->head++;
}

VOID analyze_and_print_statistics(ULONG thread_id) {

    sample_buffer *buffer = &trim_samples;
    if (thread_id == WRITING_THREAD_ID) {
        buffer = &write_samples;
        printf("Write sample data!\n");
    }
    else {
        printf("Trim sample data!\n");
    }
    ULONG64 tcount = min(NUMBER_OF_SAMPLES, buffer->head);
    double total_time_in_seconds = 0;
    double longest_run = 0;
    ULONG64 total_size = 0;
    ULONG64 largest_size = 0;

    for (int i = 0; i < tcount; ++i) {
        total_time_in_seconds += buffer->data[i].time_in_seconds;
        longest_run = max(buffer->data[i].time_in_seconds, longest_run);
        total_size += buffer->data[i].batch_size;
        largest_size = max(buffer->data[i].batch_size, largest_size);
    }
    double average_time_in_seconds = total_time_in_seconds / tcount;
    double average_batch_size = 1.0 * total_size / tcount;

    printf("Average batch time: %.3f s\n", average_time_in_seconds);
    printf("Longest batch time: %.3f s\n\n", longest_run);
    printf("Average batch size: %.2f pages\n", average_batch_size);
    printf("Longest batch size: %llu pages\n", largest_size);
    printf("~~~~~~~~~~~~~~~~~~~~~\n");
}

#endif