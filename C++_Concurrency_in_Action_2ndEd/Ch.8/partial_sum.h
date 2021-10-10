#pragma once

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <list>
#include <numeric>
#include <thread>
#include <vector>

// Listing 8.11 Calculating partial sums in parallel by dividing the problem
template < class Iter >
Iter parallel_partial_sum_8_11(Iter first, Iter last) {
    using value_type = typename Iter::value_type;
    struct process_chunk {
        void operator()(Iter begin, Iter last, std::future< value_type >* previous_end_value, std::promise< value_type >* end_value) {
            try {
                Iter end = last;
                ++end;
                std::partial_sum(begin, end, begin);
                if (previous_end_value) {
                    value_type addend = previous_end_value->get();
                    *last += addend;
                    if (end_value) { end_value->set_value(*last); }
                    std::for_each(begin, last, [addend](value_type& item) { item += addend; });
                } else if (end_value) {
                    end_value->set_value(*last);
                }
            } catch (...) {
                if (end_value) {
                    end_value->set_exception(std::current_exception());
                } else {
                    throw;
                }
            }
        }
    };
    unsigned long const length = std::distance(first, last);

    if (!length) return last;

    unsigned long const min_per_thread   = 25;
    unsigned long const max_threads      = (length + min_per_thread - 1) / min_per_thread;
    unsigned long const hardware_threads = std::thread::hardware_concurrency();
    unsigned long const num_threads      = std::min(hardware_threads != 0 ? hardware_threads : 2, max_threads);
    unsigned long const block_size       = length / num_threads;

    typedef typename Iter::value_type value_type;

    std::vector< std::thread >                threads(num_threads - 1);
    std::vector< std::promise< value_type > > end_values(num_threads - 1);
    std::vector< std::future< value_type > >  previous_end_values;

    previous_end_values.reserve(num_threads - 1);
    join_threads joiner(threads);
    Iter         block_start = first;

    for (unsigned long i = 0; i < (num_threads - 1); ++i) {
        Iter block_last = block_start;
        std::advance(block_last, block_size - 1);
        threads[i]  = std::thread(process_chunk(), block_start, block_last, (i != 0) ? &previous_end_values[i - 1] : 0, &end_values[i]);
        block_start = block_last;
        ++block_start;
        previous_end_values.push_back(end_values[i].get_future());
    }
    Iter final_element = block_start;
    std::advance(final_element, std::distance(block_start, last) - 1);
    process_chunk()(block_start, final_element, (num_threads > 1) ? &previous_end_values.back() : 0, 0);

    return first;
}

// Listing 8.13 A parallel implementation of partial_sum by pairwise updates
// I will use std:barrier (since C++20)
template < class Iter >
void parallel_partial_sum_8_13(Iter first, Iter last) {

    struct on_barrier_completion {
        void operator()() noexcept {}
    };

    typedef typename Iter::value_type value_type;
    struct process_element {
        void operator()(Iter first, Iter last, std::vector< value_type >& buffer, unsigned i, std::barrier< on_barrier_completion >& b) {
            value_type& ith_element   = *(first + i);
            bool        update_source = false;

            for (unsigned step = 0, stride = 1; stride <= i; ++step, stride *= 2) {
                value_type const& source = (step % 2) ? buffer[i] : ith_element;
                value_type&       dest   = (step % 2) ? ith_element : buffer[i];
                value_type const& addend = (step % 2) ? buffer[i - stride] : *(first + i - stride);

                dest          = source + addend;
                update_source = !(step % 2);
                b.arrive_and_wait();
            }
            if (update_source) { ith_element = buffer[i]; }
            b.arrive_and_drop();
        }
    };

    unsigned long const length = std::distance(first, last);

    if (length <= 1) return;

    std::vector< value_type >       buffer(length);
    std::barrier< on_barrier_completion > b { length };
    std::vector< std::thread >      threads(length - 1);
    join_threads                    joiner(threads);
    Iter                            block_start = first;

    for (unsigned long i = 0; i < (length - 1); ++i) { threads[i] = std::thread(process_element {}, first, last, std::ref(buffer), i, std::ref(b)); }

    process_element {}(first, last, buffer, length - 1, b);
}