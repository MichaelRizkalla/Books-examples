#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

// Listing 7.13 A single-producer, single-consumer lock-free queue
template < class T >
class lock_free_queue_7_13_SPSC {
  private:
    struct node {
        std::shared_ptr< T > data;
        node*                next;

        node() : next(nullptr) {}
    };

    std::atomic< node* > head;
    std::atomic< node* > tail;

    node* pop_head() {
        node* const old_head = head.load();
        if (old_head == tail.load()) { return nullptr; }
        head.store(old_head->next);
        return old_head;
    }

  public:
    lock_free_queue_7_13_SPSC() : head(new node), tail(head.load()) {}

    lock_free_queue_7_13_SPSC(const lock_free_queue_7_13_SPSC&) = delete;
    lock_free_queue_7_13_SPSC operator=(const lock_free_queue_7_13_SPSC&) = delete;

    ~lock_free_queue_7_13_SPSC() {
        while (node* const old_head = head.load()) {
            head.store(old_head->next);
            delete old_head;
        }
    }
    std::shared_ptr< T > pop() {
        node* old_head = pop_head();
        if (!old_head) { return nullptr; }

        std::shared_ptr< T > const res(old_head->data);
        delete old_head;
        return res;
    }
    void push(T new_value) {
        std::shared_ptr< T > new_data(std::make_shared< T >(new_value));
        node*                p        = new node;
        node* const          old_tail = tail.load();
        old_tail->data.swap(new_data);
        old_tail->next = p;
        tail.store(p);
    }
};

// Listing 7.15 Implementing push() for a lock-free queue with a reference-counted tail
// see more about the technique: Atomic Ptr Plus Project, http://atomic-ptr-plus.sourceforge.net/.
template < class T >
class lock_free_queue_RC_tail {
  private:
    struct node;

    struct counted_node_ptr {
        int   external_count;
        node* ptr;
    };

    std::atomic< counted_node_ptr > head;
    std::atomic< counted_node_ptr > tail;

    struct node_counter {
        unsigned internal_count : 30;
        unsigned external_counters : 2;
    };

    struct node {
        std::atomic< T* >           data;
        std::atomic< node_counter > count;
        counted_node_ptr            next;
        node() {
            node_counter new_count {};
            new_count.internal_count    = 0;
            new_count.external_counters = 2;
            count.store(new_count);
            next.ptr            = nullptr;
            next.external_count = 0;
        }

        void release_ref() {
            node_counter old_counter = count.load(std::memory_order::relaxed);
            node_counter new_counter;
            do {
                new_counter = old_counter;
                --new_counter.internal_count;
            } while (!count.compare_exchange_strong(old_counter, new_counter, std::memory_order::acquire, std::memory_order::relaxed));
            if (!new_counter.internal_count && !new_counter.external_counters) { delete this; }
        }
    };

    static void increase_external_count(std::atomic< counted_node_ptr >& counter, counted_node_ptr& old_counter) {
        counted_node_ptr new_counter;
        do {
            new_counter = old_counter;
            ++new_counter.external_count;
        } while (!counter.compare_exchange_strong(old_counter, new_counter, std::memory_order::acquire, std::memory_order::relaxed));
        old_counter.external_count = new_counter.external_count;
    }

    static void free_external_counter(counted_node_ptr& old_node_ptr) {
        node* const  ptr            = old_node_ptr.ptr;
        int const    count_increase = old_node_ptr.external_count - 2;
        node_counter old_counter    = ptr->count.load(std::memory_order::relaxed);
        node_counter new_counter;
        do {
            new_counter = old_counter;
            --new_counter.external_counters;
            new_counter.internal_count += count_increase;
        } while (!ptr->count.compare_exchange_strong(old_counter, new_counter, std::memory_order::acquire, std::memory_order::relaxed));
        if (!new_counter.internal_count && !new_counter.external_counters) { delete ptr; }
    }

  public:
    lock_free_queue_RC_tail() : head(counted_node_ptr{0, new node}), tail(head.load()) {}

    lock_free_queue_RC_tail(const lock_free_queue_RC_tail&) = delete;
    lock_free_queue_RC_tail operator=(const lock_free_queue_RC_tail&) = delete;

    ~lock_free_queue_RC_tail() {
        counted_node_ptr old_head = head.load();

        while (old_head.ptr) {
            head.store(old_head.ptr->next);
            delete old_head.ptr;
            old_head = head.load();
        }
    }

    void push(T new_value) {
        std::unique_ptr< T > new_data(new T(new_value));
        counted_node_ptr     new_next {};
        new_next.ptr              = new node;
        new_next.external_count   = 1;
        counted_node_ptr old_tail = tail.load();
        for (;;) {
            increase_external_count(tail, old_tail);
            T* old_data = nullptr;
            if (old_tail.ptr->data.compare_exchange_strong(old_data, new_data.get())) {
                old_tail.ptr->next = new_next;
                old_tail           = tail.exchange(new_next);
                free_external_counter(old_tail);
                new_data.release();
                break;
            }
            old_tail.ptr->release_ref();
        }
    }

    std::unique_ptr< T > pop() {
        counted_node_ptr old_head = head.load(std::memory_order_relaxed);
        for (;;) {
            increase_external_count(head, old_head);
            node* const ptr = old_head.ptr;
            if (ptr == tail.load().ptr) {
                ptr->release_ref();
                return std::unique_ptr< T >();
            }
            if (head.compare_exchange_strong(old_head, ptr->next)) {
                T* const res = ptr->data.exchange(nullptr);
                free_external_counter(old_head);
                return std::unique_ptr< T >(res);
            }
            ptr->release_ref();
        }
    }
};

// Listing 7.20 pop() modified to allow helping on the push() side
// Listing 7.21 A sample push() with helping for a lock-free queue
template < class T >
class lock_free_queue_RC_tail_modified {
  private:
    struct node;

    struct counted_node_ptr {
        int   external_count;
        node* ptr;
    };

    std::atomic< counted_node_ptr > head;
    std::atomic< counted_node_ptr > tail;

    struct node_counter {
        unsigned internal_count : 30;
        unsigned external_counters : 2;
    };

    struct node {
        std::atomic< T* >               data;
        std::atomic< node_counter >     count;
        std::atomic< counted_node_ptr > next;
        node() {
            node_counter new_count {};
            new_count.internal_count    = 0;
            new_count.external_counters = 2;
            count.store(new_count);
            next.store(counted_node_ptr{0, nullptr});
        }

        void release_ref() {
            node_counter old_counter = count.load(std::memory_order::relaxed);
            node_counter new_counter;
            do {
                new_counter = old_counter;
                --new_counter.internal_count;
            } while (!count.compare_exchange_strong(old_counter, new_counter, std::memory_order::acquire, std::memory_order::relaxed));
            if (!new_counter.internal_count && !new_counter.external_counters) { delete this; }
        }
    };

    static void increase_external_count(std::atomic< counted_node_ptr >& counter, counted_node_ptr& old_counter) {
        counted_node_ptr new_counter;
        do {
            new_counter = old_counter;
            ++new_counter.external_count;
        } while (!counter.compare_exchange_strong(old_counter, new_counter, std::memory_order::acquire, std::memory_order::relaxed));
        old_counter.external_count = new_counter.external_count;
    }

    static void free_external_counter(counted_node_ptr& old_node_ptr) {
        node* const  ptr            = old_node_ptr.ptr;
        int const    count_increase = old_node_ptr.external_count - 2;
        node_counter old_counter    = ptr->count.load(std::memory_order::relaxed);
        node_counter new_counter;
        do {
            new_counter = old_counter;
            --new_counter.external_counters;
            new_counter.internal_count += count_increase;
        } while (!ptr->count.compare_exchange_strong(old_counter, new_counter, std::memory_order::acquire, std::memory_order::relaxed));
        if (!new_counter.internal_count && !new_counter.external_counters) { delete ptr; }
    }

    void set_new_tail(counted_node_ptr& old_tail, counted_node_ptr const& new_tail) {
        node* const current_tail_ptr = old_tail.ptr;
        while (!tail.compare_exchange_weak(old_tail, new_tail) && old_tail.ptr == current_tail_ptr)
            ;
        if (old_tail.ptr == current_tail_ptr)
            free_external_counter(old_tail);
        else
            current_tail_ptr->release_ref();
    }

  public:
    lock_free_queue_RC_tail_modified() : head(counted_node_ptr{0, new node}), tail(head.load()) {}

    lock_free_queue_RC_tail_modified(const lock_free_queue_RC_tail_modified&) = delete;
    lock_free_queue_RC_tail_modified operator=(const lock_free_queue_RC_tail_modified&) = delete;

    ~lock_free_queue_RC_tail_modified() {
        counted_node_ptr old_head = head.load();

        while (old_head.ptr) {
            head.store(old_head.ptr->next);
            delete old_head.ptr;
            old_head = head.load();
        }
    }

    void push(const T& new_value) {
        std::unique_ptr< T > new_data(new T(new_value));
        counted_node_ptr     new_next {};
        new_next.ptr              = new node;
        new_next.external_count   = 1;
        counted_node_ptr old_tail = tail.load();
        for (;;) {
            increase_external_count(tail, old_tail);
            T* old_data = nullptr;
            if (old_tail.ptr->data.compare_exchange_strong(old_data, new_data.get())) {
                counted_node_ptr old_next = { 0 };
                if (!old_tail.ptr->next.compare_exchange_strong(old_next, new_next)) {
                    delete new_next.ptr;
                    new_next = old_next;
                }
                set_new_tail(old_tail, new_next);
                new_data.release();
                break;
            } else {
                counted_node_ptr old_next = { 0 };
                if (old_tail.ptr->next.compare_exchange_strong(old_next, new_next)) {
                    old_next     = new_next;
                    new_next.ptr = new node;
                }
                set_new_tail(old_tail, old_next);
            }
        }
    }

    void push(T&& new_value) {
        std::unique_ptr< T > new_data(new T(std::move(new_value)));
        counted_node_ptr     new_next {};
        new_next.ptr              = new node;
        new_next.external_count   = 1;
        counted_node_ptr old_tail = tail.load();
        for (;;) {
            increase_external_count(tail, old_tail);
            T* old_data = nullptr;
            if (old_tail.ptr->data.compare_exchange_strong(old_data, new_data.get())) {
                counted_node_ptr old_next = { 0 };
                if (!old_tail.ptr->next.compare_exchange_strong(old_next, new_next)) {
                    delete new_next.ptr;
                    new_next = old_next;
                }
                set_new_tail(old_tail, new_next);
                new_data.release();
                break;
            } else {
                counted_node_ptr old_next = { 0 };
                if (old_tail.ptr->next.compare_exchange_strong(old_next, new_next)) {
                    old_next     = new_next;
                    new_next.ptr = new node;
                }
                set_new_tail(old_tail, old_next);
            }
        }
    }

    std::unique_ptr< T > pop() {
        counted_node_ptr old_head = head.load(std::memory_order_relaxed);
        for (;;) {
            increase_external_count(head, old_head);
            node* const ptr = old_head.ptr;
            if (ptr == tail.load().ptr) { return nullptr; }
            counted_node_ptr next = ptr->next.load();
            if (head.compare_exchange_strong(old_head, next)) {
                T* const res = ptr->data.exchange(nullptr);
                free_external_counter(old_head);
                return std::unique_ptr< T >(res);
            }
            ptr->release_ref();
        }
    }
};
