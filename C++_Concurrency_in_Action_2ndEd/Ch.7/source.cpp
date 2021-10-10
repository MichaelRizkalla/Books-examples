#include "lockfree_stack.h"
#include "lockfree_queue.h"

// Test code-correctness
template class lock_free_stack_7_2< int >;
template class lock_free_stack_7_4< int >;
template class lock_free_stack_7_6< int >;
template class lock_free_stack_7_11< int >;

template class lock_free_stack_7_2< float >;
template class lock_free_stack_7_4< float >;
template class lock_free_stack_7_6< float >;
template class lock_free_stack_7_11< float >;

template class lock_free_queue_7_13_SPSC< int >;
template class lock_free_queue_RC_tail< int >;

template class lock_free_queue_7_13_SPSC< float >;
template class lock_free_queue_RC_tail< float >;


int main() {
    // no-op
}