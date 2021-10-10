#pragma once

#include "jointhreads.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <list>
#include <numeric>
#include <thread>
#include <vector>

template < class Iter, class T >
struct accumulate_block {
    void operator()(Iter first, Iter last, T& result) { result = std::accumulate(first, last, result); }

    [[nodiscard]] T operator()(Iter first, Iter last) { 
        return std::accumulate(first, last, T {}); 
    }
};

// Listing 8.2 A naïve parallel version of std::accumulate (from listing 2.8)
// Not an exception-safe code
template < class Iter, class T >
T parallel_accumulate_8_2(Iter first, Iter last, T init) {
    unsigned long const length = std::distance(first, last);

    if (!length) return init;

    unsigned long const min_per_thread   = 25;
    unsigned long const max_threads      = (length + min_per_thread - 1) / min_per_thread;
    unsigned long const hardware_threads = std::thread::hardware_concurrency();
    unsigned long const num_threads      = std::min(hardware_threads != 0 ? hardware_threads : 2, max_threads);
    unsigned long const block_size       = length / num_threads;

    std::vector< T >           results(num_threads);
    std::vector< std::thread > threads(num_threads - 1);
    Iter                       block_start = first;

    for (unsigned long i = 0; i < (num_threads - 1); ++i) {
        Iter block_end = block_start;
        std::advance(block_end, block_size);
        threads[i]  = std::thread(accumulate_block< Iter, T > {}, block_start, block_end, std::ref(results[i]));
        block_start = block_end;
    }

    accumulate_block< Iter, T > {}(block_start, last, results[num_threads - 1]);

    std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
    return std::accumulate(results.begin(), results.end(), init);
}

template < typename Iter, typename T >
T parallel_accumulate_8_4(Iter first, Iter last, T init) {
    unsigned long const length = std::distance(first, last);

    if (!length) return init;

    unsigned long const min_per_thread   = 25;
    unsigned long const max_threads      = (length + min_per_thread - 1) / min_per_thread;
    unsigned long const hardware_threads = std::thread::hardware_concurrency();
    unsigned long const num_threads      = std::min(hardware_threads != 0 ? hardware_threads : 2, max_threads);
    unsigned long const block_size       = length / num_threads;

    std::vector< std::future< T > > futures(num_threads - 1);
    std::vector< std::thread >      threads(num_threads - 1);
    join_threads                    joiner(threads);

    Iter block_start = first;

    for (unsigned long i = 0; i < (num_threads - 1); ++i) {
        Iter block_end = block_start;
        std::advance(block_end, block_size);
        std::packaged_task< T(Iter, Iter) > task { accumulate_block< Iter, T > {} };
        futures[i]  = task.get_future();
        threads[i]  = std::thread(std::move(task), block_start, block_end);
        block_start = block_end;
    }

    T last_result = accumulate_block< Iter, T > {}(block_start, last);

    T result = init;

    for (unsigned long i = 0; i < (num_threads - 1); ++i) { result += futures[i].get(); }

    result += last_result;
    return result;
}

// Listing 8.5 An exception-safe parallel version of std::accumulate using std::async
template < typename Iter, typename T >
T parallel_accumulate_8_5(Iter first, Iter last, T init) {
    unsigned long const length         = std::distance(first, last);
    unsigned long const max_chunk_size = 25;

    if (length <= max_chunk_size) {
        return std::accumulate(first, last, init);
    } else {
        Iter mid_point = first;
        std::advance(mid_point, length / 2);

        std::future< T > first_half_result  = std::async(parallel_accumulate_8_5< Iter, T >, first, mid_point, init);
        T                second_half_result = parallel_accumulate_8_5(mid_point, last, T {});

        return first_half_result.get() + second_half_result;
    }
}
