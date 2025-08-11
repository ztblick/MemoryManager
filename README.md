# MemoryManager: a Multithreaded Simulation of Virtual Memory Management
### This project simulates the memory manager in the Windows kernel.
The program faithfully simulates the state machines governing pages of memory
as they are faulted on, made active, modified, written to disk, rescued, and
repurposed. It is designed to be scalable, and there currently is  minimal (+/- 3%)
difference in runtime as user threads increase (until, of course, the number of
user threads exceeds the number of logical cores in the hardware).

### Certain liberties are taken to create this illusion:
- To gain ownership of a group of pages, a call is made to
`AllocateUserPhysicalPages()` at startup. These pages are treated as the
memory allocated to a process with one page table and multiple threads.
- There is no page file on disk -- instead, disk-bound pages are written
to and read from a global array.
---
## Project Development

### Phase One: Single-Threaded Memory Manager
This project began with a thorough education on hardware fundamentals and limitations,
which helped me understand relative speeds---of CPU clock cycles, of different instructions,
of cache hits, of reads and writes to memory, of reads and writes to disk. Understanding
the "laws of physics" in the system grounded my perspective on various strategic
decisions I would make later on in the programming process.

Next, I learned about the different states a page can possess: zeroed, free, active,
modified, and standby. I developed structs to describe my VA regions (page table entries,
or PTEs) and my pages (page frame numbers, or PFNs). Using these structs, I was able
to create a single-threaded program that would allow any number of read and write operations
on a region of virtual address space, swapping in free or standby pages to resolve faults while
trimming active pages and writing them out to the page file.

While not scalable, this version helped me understand the nuances of the foundational
state machines that guide this program. It also helped me understand what information
would be necessary to encode into my data structures -- for example, it became apparent
that my PTEs would requre three formats -- valid, transition, and on disk -- to 
efficiently encode the state of that region of virtual address space.

Once completing this phase of the project, I tested it to be sure it would work with any
number of accesses to the VA region (I ran the program continuously through the night 
to be sure), and I repeated these tests on a variety of configuration of the physical
space (smaller and larger memory, plus different ratios between pages and page file).

### Phase Two: Multithreaded Memory Manager with Fine-Grained Locking 
The next phase of this project began with a fundamental split into three threads: a trimmer
 (who would move pages from active to modified), a writer (who would write modified pages
out to the disk), and any number of user threads (which would access virtual addresses and,
when upon receiving an access violation, would resolve their page fault).

To make this possible, I incorporated fine-grained locks on my global data structures (PFNs,
PTEs, list heads, and disk slots). This required an understanding of lock hierarchy to avoid
deadlock, as well as some creativity with try-locks and backoffs any time locks needed to be
acquired out of order. This also led to my first race condition bugs, which I resolved with
the aid of WinDbg.

In this version, all pages are initially made free, and as the faulting threads use them, they
will alert the trimmer to begin moving pages to the modified list as the number of available pages
begins to dwindle. The trimmer, in turn, will alert the writer once it has trimmed a batch of pages.
Finally, upon writing a batch of pages out to disk, the writer alerts any waiting faulting threads
that pages have been made available.

An important race condition resolved at this point in the project had to do with the situation in
which a standby page (whose contents have been written to disk) is repurposed. In this situation,
the lock on the PTE of the *previous* owner must be acquired out of order (as the page lock has
already been acquired). There were multiple ways to resolve this situation -- I could have attempted
to acquire the PTE lock, and, if unsuccessful, could have simply released all locks and tried again.
I didn't like this idea, though, because it didn't guarantee a fast fault resolution. Instead,
I made a new rule: for a *transition-format* PTE, the PTE lock is not sufficient to guarantee its
data safety. The page lock must *also* be acquired before referring to its data with certainty.
So, when resolving a soft-fault on the same PTE, it would be necessary to check the PTE *after* acquiring
the page lock. With the page lock, it would then be possible for the soft-faulting PTE to be in
either its disk format (if it was written out without its lock) or in its transition format (as
it was when the fault occurred).

### Phase Three: Performance Optimizations, Additional Features, and Scalability
With fine-grained locks debugged and working at scale, I turned my attention to features and
optimizations. I ran traces with xperf and analyzed them with wpa. These helped me identify
the key bottlenecks in my state machine, which helped me address the most important areas
of my program first. These optimizations included:
- <u>Custom locks with Exponential Backoffs:</u> Critical sections proved too expensive, so
  I created my own locks to boost performance. These locks used atomic operations to acquire ownership
  of global data, and when failing, would back off with a wait time that doubled for each failure.
  This value is capped, of course, to prevent starvation. In doing so, waiting threads will burn up
  fewer cycles for highly-used global data. This saved nearly 38 bytes of data for each PTE and PFN.
  **Speedup: 50%!**


- <u>Batched Trims and Writes:</u> Instead of trimming or writing one page at a time, I would
batch together a reasonably large group of pages and change their states together. I also
lock releases during this process (lock a page to add it to a batch, unlock it while batching
together more pages, re-acquire the lock to add the page to its new state). This led to some
tricky race conditions (what if we fault on a page mid-trim or mid-write?), but nothing that
could not be resolved. **Speedup: 75%!**


- <u>Page File Bitmap:</u> Finding and locking regions in the page file was a prohibitively
slow part of my system. To speed it up, I created a bitmap to represent the state of each slot.
I used atomic operations to update the bitmaps without locks, and I added a stack of slots to
stash any reserved but unused disk slots (for easy retrieval during the next write). Furthermore,
I added functionality allowing an *entire* row of 64 slots to be reserved at once.
**Speedup: 85%!**

### Coming Soon...!
- <u>Read/Write locks on Page Lists:</u> My next goal is to reduce contention on the modified and
standby lists by using read/write locks. I intend to provide parallel soft-faulting, as each thread
only needs access to three pages (the target and its Flink and Blink).
- <u>Pte Region Locks:</u> To be more realistic, I intend to create locks on *regions* of PTEs, not individual PTEs.
This will create a more space-efficient system. It will also give me the opportunity to speculatively
trim or read pages associated with neighboring PTEs, which will be helpful when doing non-random
accesses to the VA region.

---
## Future Plans
I could spend a lifetime continuing to develop this project. Here are just SOME of
the ideas I'd like to pursue:
- The biggest miss on this project is the fact that the user threads do not access
the virtual address space in a realistic manner. Most usermode programs will
continuously use the same data, or progress through a region of VA space linearly,
or move on to a new region before returning to the original region again. The
best test of the quality of my project would be to simulate realistic usage more
faithfully. This is certainly a feature worthy of development and attention. To implement
this feature, I would need speculative reads from disk (to provide not just the faulting
page but also memory most likely to be used next). It would also be important to
develop a reasonable algorithm to  select candidates to trim (by counting cumulative usage as well
as time since previous usage).
---
- Read and write access would be another excellent feature to add. This could utilize
a bit in the PTE indicating the level of privilege the process has with the data.
Using this, I could more efficiently trim read-only pages to the standby list,
as there would never be modifications to write to the disk (assuming, of course,
there is a copy of the data on the disk already). A dirty bit could be used in the PTE
to also track pages with write privileges to see if they have actually been written to. 
---
- Disk indices provide an interesting challenge: over time, they can become fragmented, 
reducing the efficiency with which empty disk slots can be batched together for a
modified write. A defragmentation thread could do some work in the background to
swap disk slots around to create a more cohesive region of empty slots. Of course,
to do this, there would need to be some sort of data structure leading to PTEs from
their disk slots (not the other way around, as we typically see).