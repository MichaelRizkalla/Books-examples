#pragma once

#include "../Ch.7/lockfree_queue.h"
#include "../Ch.8/jointhreads.h"
#include "function_wrapper.h"
#include "work_stealing_queue.h"
#include <atomic>
#include <future>
#include <queue>
#include <thread>
#include <vector>

#define MEMBERS(WorkItem)                                    \
    std::atomic_bool                             done;       \
    lock_free_queue_RC_tail_modified< WorkItem > work_queue; \
    std::vector< std::thread >                   threads;    \
    join_threads                                 joiner;

#define CTOR_DTOR(class_name)                                                                                                 \
    class_name() : done(false), joiner(threads) {                                                                             \
        unsigned const thread_count = std::thread::hardware_concurrency();                                                    \
                                                                                                                              \
        try {                                                                                                                 \
                                                                                                                              \
            for (unsigned i = 0; i < thread_count; ++i) { threads.push_back(std::thread(&class_name::worker_thread, this)); } \
        } catch (...) {                                                                                                       \
            done = true;                                                                                                      \
            throw;                                                                                                            \
        }                                                                                                                     \
    }                                                                                                                         \
                                                                                                                              \
    ~class_name() { done = true; }

// Listing 9.1 Simple thread pool
class thread_pool_9_1 final {
    MEMBERS(std::function< void() >)

    void worker_thread() {
        while (!done) {
            auto task = work_queue.pop();
            if (task) {
                (*task)();
            } else {
                std::this_thread::yield();
            }
        }
    }

  public:
    CTOR_DTOR(thread_pool_9_1)

    template < class FunctionType >
    void submit(FunctionType f) {
        work_queue.push(std::function< void() >(f));
    }
};

// Listing 9.2 A thread pool with waitable tasks
class thread_pool_9_2 final {
    MEMBERS(function_wrapper)

    void worker_thread() {
        while (!done) { run_pending_task(); }
    }

  public:
    CTOR_DTOR(thread_pool_9_2)

    void run_pending_task() {
        auto task = work_queue.pop();
        if (task) {
            (*task)();
        } else {
            std::this_thread::yield();
        }
    }

    template < class FunctionType >
    std::future< std::invoke_result_t< FunctionType > > submit(FunctionType f) {
        using result_type = std::invoke_result_t< FunctionType >;

        std::packaged_task< result_type() > task(std::move(f));
        std::future< result_type >          res(task.get_future());
        work_queue.push(std::move(task));

        return res;
    }
};

// Listing 9.6 A thread pool with thread-local work queues
class thread_pool_9_6 final {
    MEMBERS(function_wrapper)

    using local_queue_type = std::queue< function_wrapper >;
    static thread_local std::unique_ptr< local_queue_type > local_work_queue;

    void worker_thread() {
        local_work_queue.reset(new local_queue_type);

        while (!done) { run_pending_task(); }
    }

  public:
    CTOR_DTOR(thread_pool_9_6)

    template < class FunctionType >
    std::future< std::invoke_result_t< FunctionType > > submit(FunctionType f) {
        using result_type = typename std::invoke_result_t< FunctionType >;
        std::packaged_task< result_type() > task(f);
        std::future< result_type >          res(task.get_future());
        if (local_work_queue) {
            local_work_queue->push(std::move(task));
        } else {
            work_queue.push(std::move(task));
        }
        return res;
    }

    void run_pending_task() {
        function_wrapper                    task;
        std::unique_ptr< function_wrapper > task_ptr { nullptr };
        if (local_work_queue && !local_work_queue->empty()) {
            task = std::move(local_work_queue->front());
            local_work_queue->pop();
            task();
        } else if (task_ptr = work_queue.pop()) {
            (*task_ptr)();
        } else {
            std::this_thread::yield();
        }
    }
};

// Listing 9.8 A thread pool that uses work stealing
class thread_pool_9_8 final {

    using task_type = function_wrapper;

    std::atomic_bool                                          done;
    lock_free_queue_RC_tail_modified< task_type >             work_queue;
    std::vector< std::unique_ptr< work_stealing_queue_9_7 > > queues;
    std::vector< std::thread >                                threads;
    join_threads                                              joiner;

    static thread_local work_stealing_queue_9_7* local_work_queue;
    static thread_local unsigned                 my_index;

    void worker_thread(unsigned my_index_) {
        my_index         = my_index_;
        local_work_queue = queues[my_index].get();
        while (!done) { run_pending_task(); }
    }
    bool pop_task_from_local_queue(task_type& task) { return local_work_queue && local_work_queue->try_pop(task); }
    bool pop_task_from_pool_queue(task_type& task) {
        auto task_ptr = work_queue.pop();
        if (task_ptr) {
            task = std::move(*task_ptr.get());
            return true;
        }

        return false;
    }
    bool pop_task_from_other_thread_queue(task_type& task) {
        for (unsigned i = 0; i < queues.size(); ++i) {
            unsigned const index = (my_index + i + 1) % queues.size();
            if (queues[index]->try_steal(task)) { return true; }
        }
        return false;
    }

  public:
    thread_pool_9_8() : done(false), joiner(threads) {
        unsigned const thread_count = std::thread::hardware_concurrency();
        try {
            for (unsigned i = 0; i < thread_count; ++i) {
                queues.push_back(std::unique_ptr< work_stealing_queue_9_7 >(new work_stealing_queue_9_7));
                threads.push_back(std::thread(&thread_pool_9_8::worker_thread, this, i));
            }
        } catch (...) {
            done = true;
            throw;
        }
    }
    ~thread_pool_9_8() { done = true; }

    template < class FunctionType >
    std::future< std::invoke_result_t< FunctionType > > submit(FunctionType f) {
        using result_type = std::invoke_result_t< FunctionType >;

        std::packaged_task< result_type() > task(f);
        std::future< result_type >          res(task.get_future());
        if (local_work_queue) {
            local_work_queue->push(std::move(task));
        } else {
            work_queue.push(std::move(task));
        }
        return res;
    }
    void run_pending_task() {
        task_type task;
        if (pop_task_from_local_queue(task) || pop_task_from_pool_queue(task) || pop_task_from_other_thread_queue(task)) {
            task();
        } else {
            std::this_thread::yield();
        }
    }
};
