## Lockfree data structures guidelines:
- Use std::memory_order::seq_cst for prototyping
- Use a lock-free memory reclamation scheme (wait until no threads are operating, hazard pointer, ref count, etc...)
- Watch out for the ABA problem. Most common way to avoid ABA is to include a counter with the variable.
- Identify busy-wait loops and help the other thread