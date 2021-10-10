#include "accumulate.h"
#include "find.h"
#include "foreach.h"
#include "partial_sum.h"
#include "quicksort.h"
#include <cassert>

// Test code-correctness
template class sorter_8_1< int >;

template class sorter_8_1< float >;

int main() {
    /*std::list< int > vals_8_1 { 9, 5, 7, 6, 8, 2, 1, 3, 4 };
    auto             res = parallel_quick_sort_8_1(vals_8_1);

    for (size_t i = 0; i < vals_8_1.size(); ++i) {
        assert(res.front() == i + 1);
        res.pop_front();
    }*/

    auto il = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    auto result = parallel_accumulate_8_2(il.begin(), il.end(), 0);
    assert(result == 45);

    result = parallel_accumulate_8_4(il.begin(), il.end(), 0);
    assert(result == 45);

    result = parallel_accumulate_8_5(il.begin(), il.end(), 0);
    assert(result == 45);

    parallel_for_each_8_7(il.begin(), il.end(), [](auto&& val) { return true; });
    parallel_for_each_8_8(il.begin(), il.end(), [](auto&& val) { return true; });

    assert(5 == *parallel_find_8_9(il.begin(), il.end(), 5));
    assert(il.end() == parallel_find_8_9(il.begin(), il.end(), 45));

    assert(5 == *parallel_find_8_10(il.begin(), il.end(), 5));
    assert(il.end() == parallel_find_8_10(il.begin(), il.end(), 45));

    std::vector< int > vals_8_11 { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    parallel_partial_sum_8_11(vals_8_11.begin(), vals_8_11.end());

    std::vector< int > vals_8_13 { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    parallel_partial_sum_8_13(vals_8_13.begin(), vals_8_13.end());
}
