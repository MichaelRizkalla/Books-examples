#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>
#include <thread>

class thread_interrupted : public std::exception {
    const char* what() const noexcept override { return "thread_interrupted"; }
};

class interrupt_flag_9_8 {
    std::atomic< bool >      flag;
    std::condition_variable* thread_cond;
    std::mutex               set_clear_mutex;

  public:
    interrupt_flag_9_8() : thread_cond(0) {}
    void set() {
        flag.store(true, std::memory_order_relaxed);
        std::lock_guard< std::mutex > lk(set_clear_mutex);
        if (thread_cond) { thread_cond->notify_all(); }
    }
    bool is_set() const { return flag.load(std::memory_order_relaxed); }
    void set_condition_variable(std::condition_variable& cv) {
        std::lock_guard< std::mutex > lk(set_clear_mutex);
        thread_cond = &cv;
    }
    void clear_condition_variable() {
        std::lock_guard< std::mutex > lk(set_clear_mutex);
        thread_cond = 0;
    }

    struct clear_cv_on_destruct;
};

thread_local interrupt_flag_9_8 this_thread_interrupt_flag_9_8;

struct interrupt_flag_9_8::clear_cv_on_destruct {
    ~clear_cv_on_destruct() { this_thread_interrupt_flag_9_8.clear_condition_variable(); }
};

// Listing 9.9 Basic implementation of interruptible_thread
class interruptible_thread_9_9 {
    std::thread     internal_thread;
    interrupt_flag_9_8* flag;

  public:
    template < class FunctionType >
    interruptible_thread_9_9(FunctionType f) {
        std::promise< interrupt_flag_9_8* > p;
        internal_thread = std::thread([f, &p] {
            p.set_value(&this_thread_interrupt_flag_9_8);
            try
            {
                f();
            }
            catch(thread_interrupted const&)
            {}
        });
        flag            = p.get_future().get();
    }
    void join() {
        internal_thread.join();
    }
    void interrupt() {
        if (flag) { flag->set(); }
    }
};

void interruption_point() {
    if (this_thread_interrupt_flag_9_8.is_set()) { throw thread_interrupted {}; }
}

// Listing 9.11 Using a timeout in interruptible_wait for std::condition_variable
void interruptible_wait_9_11(std::condition_variable& cv, std::unique_lock< std::mutex >& lk) {
    interruption_point();
    this_thread_interrupt_flag_9_8.set_condition_variable(cv);
    interrupt_flag_9_8::clear_cv_on_destruct guard;
    interruption_point();
    cv.wait_for(lk, std::chrono::milliseconds(1));
    interruption_point();
}

template < class Predicate >
void interruptible_wait_9_11(std::condition_variable& cv, std::unique_lock< std::mutex >& lk, Predicate pred) {
    interruption_point();
    this_thread_interrupt_flag_9_8.set_condition_variable(cv);
    interrupt_flag_9_8::clear_cv_on_destruct guard;
    while (!this_thread_interrupt_flag_9_8.is_set() && !pred()) { cv.wait_for(lk, std::chrono::milliseconds(1)); }
    interruption_point();
}

// Listing 9.12 interruptible_wait for std::condition_variable_any
class interrupt_flag_9_12 {
    std::atomic< bool >          flag;
    std::condition_variable*     thread_cond;
    std::condition_variable_any* thread_cond_any;
    std::mutex                   set_clear_mutex;

  public:
    interrupt_flag_9_12() : thread_cond(0), thread_cond_any(0) {}
    void set() {
        flag.store(true, std::memory_order_relaxed);
        std::lock_guard< std::mutex > lk(set_clear_mutex);
        if (thread_cond) {
            thread_cond->notify_all();
        } else if (thread_cond_any) {
            thread_cond_any->notify_all();
        }
    }
    template < typename Lockable >
    void wait(std::condition_variable_any& cv, Lockable& lk) {
        struct custom_lock {
            interrupt_flag_9_12* self;
            Lockable&       lk;
            custom_lock(interrupt_flag_9_12* self_, std::condition_variable_any& cond, Lockable& lk_) : self(self_), lk(lk_) {
                self->set_clear_mutex.lock();
                self->thread_cond_any = &cond;
            }
            void unlock() {
                lk.unlock();
                self->set_clear_mutex.unlock();
            }
            void lock() { std::lock(self->set_clear_mutex, lk); }
            ~custom_lock() {
                self->thread_cond_any = 0;
                self->set_clear_mutex.unlock();
            }
        };
        custom_lock cl(this, cv, lk);
        interruption_point();
        cv.wait(cl);
        interruption_point();
    }
    bool is_set() const { return flag.load(std::memory_order_relaxed); }
    void set_condition_variable(std::condition_variable& cv) {
        std::lock_guard< std::mutex > lk(set_clear_mutex);
        thread_cond = &cv;
    }
    void clear_condition_variable() {
        std::lock_guard< std::mutex > lk(set_clear_mutex);
        thread_cond = 0;
    }
    struct clear_cv_on_destruct;
};

thread_local interrupt_flag_9_12 this_thread_interrupt_flag_9_12;

struct interrupt_flag_9_12::clear_cv_on_destruct {
    ~clear_cv_on_destruct() { this_thread_interrupt_flag_9_12.clear_condition_variable(); }
};

template < typename Lockable >
void interruptible_wait_9_12(std::condition_variable_any& cv, Lockable& lk) {
    this_thread_interrupt_flag_9_12.wait(cv, lk);
}