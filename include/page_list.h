#pragma once

#include "../include/macros.h"
#include "../include/PFN.h"
#include "../include/debug.h"

/*
 * A page list is the head of a doubly-linked list of pages.
 * When the list is empty, the head's entry's flink and blink point to the entry itself.
 * The head also keeps track of the size of the list. It also has the lock on the page list.
*/

/*
 *  Our guiding rule on locks and page lists: to add/remove an entry from a list, you MUST have the lock
 *  on that page AS WELL AS the lock on the list head. ONLY if you have BOTH locks may you add or remove a page
 *  from a list.
 */

// Total size: 32 bytes. Two will fit in a cache line.
typedef struct __page_list {
    LIST_ENTRY head;            // 16 bytes
    ULONG64 list_size;          // 8 bytes
    SRWLOCK lock;               // 8 bytes
} PAGE_LIST, *PPAGE_LIST;

/*
 *  Initialize a page list. This creates the critical section, initializes the list head,
 *  and sets the size to zero.
 */
VOID initialize_page_list(PPAGE_LIST list);

/*
 *  Returns the length of the list when asked, with no guarantees about future length.
 */
ULONG64 get_size(PPAGE_LIST list);

/*
 *  Check if the list is empty. Returns true or false.
 */
BOOL is_page_list_empty(PPAGE_LIST list);

/*
 *  Adds a page to the head of the list. This assumes the page being added is already locked by the caller!
 */
VOID lock_list_then_insert_to_head(PPAGE_LIST list, PLIST_ENTRY entry);

/*
 *  Returns true if the given PFN is at the head of the given list. Does not lock anything.
 */
BOOL is_at_head_of_list(PPAGE_LIST list, PPFN pfn);

/*
 *  Adds a page to the tail of the list. This assumes the page being added is already locked by the caller!
 */
VOID lock_list_then_insert_to_tail(PPAGE_LIST list, PLIST_ENTRY entry);

/*
 *  Increments the list size.
 */
VOID increment_list_size(PPAGE_LIST list);

/*
 *  Decrements a list size, which is done when removing elements during a soft fault.
 */
VOID decrement_list_size(PPAGE_LIST list);

/*
 *  This pops an entry from the head of the list. It assumes nothing -- the programmer
 *  must first be sure that the list is non-empty before calling this function.
 */
PPFN pop_from_head_list(PPAGE_LIST list);

/*
 *  Locks the list, then removes an entry from that list. If, for some reason, the page is not on that list,
 *  then the function may behave in unexpected ways...
 */
VOID lock_list_and_remove_page(PPAGE_LIST list, PPFN pfn);

/*
 *  Removes a page from its list. Does not acquire any locks. Does not assume ANYTHING -- the caller
 *  must ensure that the entry is, in fact, on the given list!
 */
VOID remove_page_from_list(PPAGE_LIST list, PPFN pfn);


/*
 *  Pops and returns from the head of the list.
 *  Returns NULL is the list is empty.
 */
PPFN lock_list_then_pop_from_head(PPAGE_LIST list);

/*
 *  Returns the PFN at the head of the list, but does not remove it from the list.
 *  Returns NULL is the list is empty.
 */
PPFN peek_from_list_head(PPAGE_LIST list);

/*
 *  Waits until it can acquire the lock on a given page list!
 */
VOID lock_list_exclusive(PPAGE_LIST list);

/*
 *  Attempts to lock a given page list, but does not wait. Returns TRUE if the page list lock is acquired.
 */
BOOL try_lock_list_exclusive(PPAGE_LIST list);


/*
 *  Unlocks a previously locked page list.
 */
VOID unlock_list_exclusive(PPAGE_LIST list);

#if DEBUG
VOID validate_list(PPAGE_LIST list);
#endif