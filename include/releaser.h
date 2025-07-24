//
// Created by zachb on 7/24/2025.
//

#include "../include/initializer.h"

#pragma once

/**
 * @brief Performs global shutdown operations.
 * Frees all dynamically allocated data structures and system resources associated with the virtual memory manager.
 */
void free_all_data_and_shut_down(void);

/**
 * @brief Releases all synchronization event objects.
 * Frees handles or memory associated with event signaling used for thread coordination or page management.
 */
void free_events(void);

/**
 * @brief Frees linked list-based management structures.
 * Used for cleanup of queues, page lists, or tracking lists implemented via dynamic nodes.
 */
void free_list_data(void);

/**
 * @brief Deletes a critical section and releases its system resources.
 * Must be called before freeing memory allocated for a lock to prevent leaks.
 */
void free_lock(PCRITICAL_SECTION lock);

/**
 * @brief Frees all page file tracking structures.
 * Releases metadata and buffers used to simulate or manage a backing page file.
 */
void free_page_file_data(void);

/**
 * @brief Frees data structures associated with physical frame numbers (PFNs).
 * Deinitializes PFN arrays and releases memory used for frame tracking in the memory manager.
 */
void free_PFN_data(void);

/**
 * @brief Frees all page table entry (PTE) data.
 * Releases memory associated with virtual-to-physical page mappings.
 */
void free_PTE_data(void);

/**
 * @brief Frees data associated with virtual address space mappings.
 * Cleans up structures used to track or manage VA allocation and usage.
 */
void free_VA_space_data(void);

/**
 * @brief Unmaps all pages in the virtual memory manager.
 * Ensures that mapped virtual memory regions are released and invalidated before shutdown.
 */
void unmap_all_pages(void);