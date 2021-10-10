#pragma once

#include "../Ch.8/accumulate.h" // for accumulate_block
#include "threadpool.h"
#include <numeric>

// Listing 9.3 parallel_accumulate using a thread pool with waitable tasks
template < typename Iter, typename T >
T parallel_accumulate_9_3(Iter first, Iter last, T init) {
    unsigned long const length = std::distance(first, last);

    if (!length) return init;

    unsigned long const             block_size = 25;
    unsigned long const             num_blocks = (length + block_size - 1) / block_size;
    std::vector< std::future< T > > futures(num_blocks - 1);
    thread_pool_9_2                 pool;
    Iter                            block_start = first;

    for (unsigned long i = 0; i < (num_blocks - 1); ++i) {
        Iter block_end = block_start;
        std::advance(block_end, block_size);
        futures[i]  = pool.submit(std::bind(accumulate_block< Iter, T > {}, block_start, block_end));
        block_start = block_end;
    }
    T last_result = accumulate_block< Iter, T > {}(block_start, last);
    T result      = init;
    for (unsigned long i = 0; i < (num_blocks - 1); ++i) { result += futures[i].get(); }
    result += last_result;
    return result;
}
