#pragma once

#include "function_wrapper.h"
#include <deque>
#include <mutex>

// Listing 9.7 Lock-based queue for work stealing
class work_stealing_queue_9_7 {
  private:
    using data_type = function_wrapper;

    std::deque< data_type > the_queue;
    mutable std::mutex      the_mutex;

  public:
    work_stealing_queue_9_7() {}

    work_stealing_queue_9_7(const work_stealing_queue_9_7&) = delete;
    work_stealing_queue_9_7& operator=(const work_stealing_queue_9_7&) = delete;

    void push(data_type&& data) {
        std::scoped_lock lock(the_mutex);
        the_queue.push_front(std::move(data));
    }
    bool empty() const {
        std::scoped_lock lock(the_mutex);
        return the_queue.empty();
    }
    bool try_pop(data_type& res) {
        std::scoped_lock lock(the_mutex);

        if (the_queue.empty()) { return false; }

        res = std::move(the_queue.front());

        the_queue.pop_front();

        return true;
    }
    bool try_steal(data_type& res) {
        std::scoped_lock lock(the_mutex);

        if (the_queue.empty()) { return false; }

        res = std::move(the_queue.back());

        the_queue.pop_back();

        return true;
    }
};