//
// Created by zachb on 7/25/2025.
//

#include "../include/locks.h"

VOID initialize_byte_lock(PBYTE_LOCK lock) {
    WriteULong64NoFence(&lock->semaphore, UNLOCKED);
}

VOID lock(PBYTE_LOCK lock) {

    int backoff = 1;
    do {
        if (lock->semaphore == LOCKED) {
            // In this situation, the lock is almost certainly still acquired. No need to try.
            // We will wait (and if we were here before, we will wait twice as long).
            for (int i = 0; i < backoff; i++) {
                YieldProcessor();
            }
            backoff = min(backoff << 1, MAX_WAIT_TIME_BEFORE_RETRY);
        }
        // Here we will try to acquire the lock. If we cannot, we will wrap around
        // and try again, with a doubled delay to prevent constant attempts.

        // TODO Bump the version to prevent the ABA problem.

    } while (InterlockedCompareExchange64((volatile LONG64 *) &lock->semaphore,
                                            LOCKED,
                                            UNLOCKED) != UNLOCKED);
}

BOOL try_lock(PBYTE_LOCK lock) {

    // If the lock is already acquired, no need to try the interlocked operation.
    if (lock->semaphore == LOCKED)
        return FALSE;

    return(InterlockedCompareExchange64((volatile LONG64 *) &lock->semaphore,
                                            LOCKED,
                                            UNLOCKED) == UNLOCKED);
}

VOID unlock(PBYTE_LOCK lock) {

    // Ensure that we are unlocking something already locked.
    ASSERT(lock->semaphore == LOCKED);
    InterlockedDecrement64((volatile LONG64*) &lock->semaphore);
}