#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

template < class T >
class stack_data {
  public:
    struct node {
        std::shared_ptr< T > data;
        node*                next;

        node(T const& data_) : data(std::make_shared< T >(data_)), next(nullptr) {}
        node(T&& data_) : data(std::make_shared< T >(std::move(data_))), next(nullptr) {}
    };
};

template < class T >
class stack_push : public stack_data< T > {
  protected:
    using stack_data< T >::node;

    std::atomic< node* > head;
    stack_push() = default;

  public:
    void push(T const& data) {
        node* const new_node = new node(data);
        new_node->next       = head.load();
        while (!head.compare_exchange_weak(new_node->next, new_node))
            ; // if head was changed by another thread, update new_node->next with new head value
    }
    void push(T&& data) {
        node* const new_node = new node(std::move(data));
        new_node->next       = head.load();
        while (!head.compare_exchange_weak(new_node->next, new_node))
            ; // if head was changed by another thread, update new_node->next with new head value
    }
};

// Listing 7.2 Implementing push() without locks - leaking nodes
template < class T >
class lock_free_stack_7_2 : public stack_push< T > {
  private:
    using stack_data< T >::node;
    using stack_push< T >::head;

  public:
    std::shared_ptr< T > pop() {
        node* old_head = head.load();
        while (old_head && !head.compare_exchange_weak(old_head, old_head->next))
            ;
        return old_head ? old_head->data : nullptr;
    }

    ~lock_free_stack_7_2() {
        while (!pop()) {}
    }
};

// Listing 7.5 The reference-counted reclamation machinery
template < class T >
class ref_count_delete_machinery : protected stack_data< T > {
  public:
    using stack_data< T >::node;

    std::atomic< node* >    to_delete;
    std::atomic< unsigned > threads_in_pop;

    static void delete_nodes(node* nodes) {
        while (nodes) {
            node* next = nodes->next;
            delete nodes;
            nodes = next;
        }
    }

    void try_reclaim(node* old_head) {
        if (threads_in_pop == 1) {
            node* nodes_to_delete = to_delete.exchange(nullptr);
            if (!--threads_in_pop) {
                delete_nodes(nodes_to_delete);
            } else if (nodes_to_delete) {
                chain_pending_nodes(nodes_to_delete);
            }
            delete old_head;
        } else {
            chain_pending_node(old_head);
            --threads_in_pop;
        }
    }
    void chain_pending_nodes(node* nodes) {
        node* last = nodes;
        while (node* const next = last->next) { last = next; }
        chain_pending_nodes(nodes, last);
    }
    void chain_pending_nodes(node* first, node* last) {
        last->next = to_delete;
        while (!to_delete.compare_exchange_weak(last->next, first))
            ;
    }
    void chain_pending_node(node* n) { chain_pending_nodes(n, n); }
};

// Listing 7.4 Reclaiming nodes when no threads are in pop()
template < class T >
class lock_free_stack_7_4 : private ref_count_delete_machinery< T >, public stack_push< T > {
  private:
    using stack_data< T >::node;
    using stack_push< T >::head;
    using ref_count_delete_machinery< T >::threads_in_pop;
    using ref_count_delete_machinery< T >::try_reclaim;

  public:
    std::shared_ptr< T > pop() {
        ++threads_in_pop;
        node* old_head = head.load();
        while (old_head && !head.compare_exchange_weak(old_head, old_head->next))
            ;
        std::shared_ptr< T > res;
        if (old_head) { res.swap(old_head->data); }

        try_reclaim(old_head);
        return res;
    }

    ~lock_free_stack_7_4() {
        while (!pop()) {}
    }
};

// Listing 7.7 A simple implementation of get_hazard_pointer_for_current_thread()
// This technique is patented by IBM, and can only be used under GPL or with a licensing arrangement
template < class T >
class hazardous_pointer_machinery {
  private:
    inline static unsigned const max_hazard_pointers = 100;
    struct hazard_pointer {
        std::atomic< std::thread::id > id;
        std::atomic< void* >           pointer;
    };
    inline static hazard_pointer hazard_pointers[max_hazard_pointers];

    class hp_owner {
        hazard_pointer* hp;

      public:
        hp_owner(hp_owner const&) = delete;
        hp_owner operator=(hp_owner const&) = delete;

        hp_owner() : hp(nullptr) {
            for (unsigned i = 0; i < max_hazard_pointers; ++i) {
                std::thread::id old_id;
                if (hazard_pointers[i].id.compare_exchange_strong(old_id, std::this_thread::get_id())) {
                    hp = &hazard_pointers[i];
                    break;
                }
            }
            if (!hp) { throw std::runtime_error("No hazard pointers available"); }
        }

        ~hp_owner() {
            hp->pointer.store(nullptr);
            hp->id.store(std::thread::id());
        }

        std::atomic< void* >& get_pointer() { return hp->pointer; }
    };

    template < class T >
    static void do_delete(void* p) {
        delete static_cast< T* >(p);
    }
    struct data_to_reclaim {
        void*                        data;
        std::function< void(void*) > deleter;
        data_to_reclaim*             next;

        template < class T >
        data_to_reclaim(T* p) : data(p), deleter(&do_delete< T >), next(nullptr) {}

        ~data_to_reclaim() { deleter(data); }
    };
    std::atomic< data_to_reclaim* > nodes_to_reclaim;
    void                            add_to_reclaim_list(data_to_reclaim* node) {
        node->next = nodes_to_reclaim.load();
        while (!nodes_to_reclaim.compare_exchange_weak(node->next, node))
            ;
    }

  public:
    std::atomic< void* >& get_hazard_pointer_for_current_thread() {
        thread_local static hp_owner hazard {};
        return hazard.get_pointer();
    }
    bool outstanding_hazard_pointers_for(void* p) {
        for (unsigned i = 0; i < max_hazard_pointers; ++i) {
            if (hazard_pointers[i].pointer.load() == p) { return true; }
        }
        return false;
    }
    template < class T >
    void reclaim_later(T* data) {
        add_to_reclaim_list(new data_to_reclaim(data));
    }
    void delete_nodes_with_no_hazards() {
        data_to_reclaim* current = nodes_to_reclaim.exchange(nullptr);
        while (current) {
            data_to_reclaim* const next = current->next;
            if (!outstanding_hazard_pointers_for(current->data)) {
                delete current;
            } else {
                add_to_reclaim_list(current);
            }
            current = next;
        }
    }
};

// Listing 7.6 An implementation of pop() using hazard pointers
template < class T >
class lock_free_stack_7_6 : private hazardous_pointer_machinery< T >, public stack_push< T > {
  private:
    using stack_data< T >::node;
    using stack_push< T >::head;
    using hazardous_pointer_machinery< T >::get_hazard_pointer_for_current_thread;
    using hazardous_pointer_machinery< T >::outstanding_hazard_pointers_for;
    using hazardous_pointer_machinery< T >::reclaim_later;
    using hazardous_pointer_machinery< T >::delete_nodes_with_no_hazards;

  public:
    std::shared_ptr< T > pop() {
        std::atomic< void* >& hp = get_hazard_pointer_for_current_thread();

        node* old_head = head.load();
        do {
            node* temp;
            do {
                temp = old_head;
                hp.store(old_head);
                old_head = head.load();
            } while (old_head != temp);
        } while (old_head && !head.compare_exchange_strong(old_head, old_head->next));

        hp.store(nullptr);
        std::shared_ptr< T > res;
        if (old_head) {
            res.swap(old_head->data);
            if (outstanding_hazard_pointers_for(old_head)) {
                reclaim_later(old_head);
            } else {
                delete old_head;
            }
            delete_nodes_with_no_hazards();
        }
        return res;
    }

    ~lock_free_stack_7_6() {
        while (!pop()) {}
    }
};

template < class T >
class stack_ref_counted_data {
  public:
    struct node;

    struct counted_node_ptr {
        int   external_count;
        node* ptr;
    };

    struct node {
        std::shared_ptr< T > data;
        std::atomic< int >   internal_count;
        counted_node_ptr     next;

        node(T const& data_) : data(std::make_shared< T >(data_)), internal_count(0), next(nullptr) {}
    };
};

// Listing 7.10 Pushing a node on a lock-free stack using split reference counts
template < class T >
class stack_ref_counted_push : public stack_ref_counted_data< T > {
  protected:
    using stack_ref_counted_data< T >::counted_node_ptr;
    using stack_ref_counted_data< T >::node;

    stack_ref_counted_push() = default;

  public:
    std::atomic< counted_node_ptr > head;

    void push(T const& data) {
        counted_node_ptr new_node;
        new_node.ptr            = new node(data);
        new_node.external_count = 1;
        new_node.ptr->next      = head.load(std::memory_order::relaxed);
        while (!head.compare_exchange_weak(new_node.ptr->next, new_node, std::memory_order::release, std::memory_order::relaxed))
            ;
    }
};

// Listing 7.11 Popping a node from a lock-free stack using split reference counts
template < class T >
class lock_free_stack_7_11 : public stack_ref_counted_push< T > {
  private:
    using stack_ref_counted_push< T >::counted_node_ptr;
    using stack_ref_counted_push< T >::node;
    using stack_ref_counted_push< T >::head;

    void increase_head_count(counted_node_ptr& old_counter) {
        counted_node_ptr new_counter;
        do {
            new_counter = old_counter;
            ++new_counter.external_count;
        } while (!head.compare_exchange_strong(old_counter, new_counter, std::memory_order::acquire, std::memory_order::relaxed));
        old_counter.external_count = new_counter.external_count;
    }

  public:
    lock_free_stack_7_11() : stack_ref_counted_push< T >() {}

    std::shared_ptr< T > pop() {
        counted_node_ptr old_head = head.load(std::memory_order::relaxed);
        for (;;) {
            increase_head_count(old_head);
            node* const ptr = old_head.ptr;
            if (!ptr) { return nullptr; }
            if (head.compare_exchange_strong(old_head, ptr->next, std::memory_order::relaxed)) {
                std::shared_ptr< T > res;
                res.swap(ptr->data);

                int const count_increase = old_head.external_count - 2;

                if (ptr->internal_count.fetch_add(count_increase, std::memory_order::release) == -count_increase) { delete ptr; }

                return res;
            } else if (ptr->internal_count.fetch_sub(1, std::memory_order::relaxed) == 1) {
                delete ptr;
            }
        }
    }

    ~lock_free_stack_7_11() {
        while (!pop())
            ;
    }
};
