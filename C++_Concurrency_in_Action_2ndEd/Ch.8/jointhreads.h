#pragma once

#include <thread>
#include <vector>

// Listing 8.4 An exception-safe parallel version of std::accumulate
class join_threads {
    std::vector< std::thread >& threads;

  public:
    explicit join_threads(std::vector< std::thread >& threads_) : threads(threads_) {}
    ~join_threads() {
        for (unsigned long i = 0; i < threads.size(); ++i) {
            try {
                if (threads[i].joinable()) { threads[i].join(); }
            } catch (std::exception& e) { int x = 4; }
        }
    }
};