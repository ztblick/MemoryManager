# MemoryManager: a Simulation of Virtual Memory Management with the Microsoft's APIs
### This project simulates the memory manager in the Windows kernel.
The program faithfully simulates the state machines governing pages of memory as they are made active,
modified, written to disk, faulted on, and repurposed. It is designed to be scalable, and there currently is
minimal (+/- 3%) difference in runtime as user threads increase (until, of course, the number of user threads exceeds
the number of logical cores in the system).

### Certain liberties are taken to create this illusion:
- To gain ownership of a group of pages, a call is made to `AllocateUserPhysicalPages()` at startup. These
pages are then treated as the memory allocated to a processes with one page table and multiple threads.
- There is no page file on disk -- instead, disk-bound pages are written to and read from a global array.
---
## Project Development

### Phase One: Single-Threaded Memory Manager
Coming soon!
### Phase Two: Multithreaded Memory Manager with Fine-Grained Locking
Coming soon!
### Phase Three: Performance Optimizations
Coming soon!
### Phase Four: Additional Features and Scalability
Coming soon!

---
## Future Plans
Coming soon!