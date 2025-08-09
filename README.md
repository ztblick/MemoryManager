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
of cache hits, of reads and writes to memory, of reads and writes to disk. Understnading
the "laws of physics" in the system grounded my perspective on various strategic
decisions I would make later on in the programming process.

Next, I learned about the different states page can possess: zeroed, free, active,
modified, standby. I developed structs to describe my VA regions (page table entries,
or PTEs) and my pages (page frame numbers, or PFNs). Using these structs, I was able
to create single-threaded program that would allow any number of read and write operations
on a region of virtual address space, swapping in free or standby pages to resolve faults while
trimming active pages and writing them out to the page file.

While not scalable, this version helped me understand the nuances of the foundational
state machines that guide this program. It also helped me understand what information
would be necessary to encode into my data structures -- for example, it became apparent
that my PTEs would requre three formats -- valid, transition, and on disk -- to 
efficiently encode the state of that region of virtual address space.

### Phase Two: Multithreaded Memory Manager with Fine-Grained Locking
Coming soon!
- fine-grained locks (pte, page, lists, disk slots)
- xperf

### Phase Three: Performance Optimizations, Additional Features, and Scalability
Coming soon!
- batched trims, writes
- bitmap for page file
- custom locks with exponential backoffs
- releasing locks then re-acquiring, snapshot then verify later
- preventing tearing with WriteUlong64NoFence
- kernel transfer va spaces for threads, batched unmaps
- read/write locks on page lists
- pte region locks

---
## Future Plans
I could spend a lifetime continuing to develop this project. Here are just SOME of
the ideas I'd like to pursue:
- The biggest miss on this project is the fact that the user threads do not access
the virtual address space in a realistic manner. Most usermode programs will
continuously use the same data, or progress through a region of VA space linearly,
or move on to a new region before returning to the original region again. The
best test of the quality of my project would be to simulate realistic usage more
faithfully. This is certainly a feature worthy of development and attention.
- To implement this feature, I would need speculative reads from disk (to provide not just the faulting
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