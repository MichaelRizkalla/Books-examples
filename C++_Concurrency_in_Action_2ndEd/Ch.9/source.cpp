#include "accumulate.h"
#include "interruptible_thread.h"
#include "quicksort.h"
#include "threadpool.h"
#include <cassert>

#include <random>

// Listing 9.13 Monitoring the filesystem in the background
std::mutex                              config_mutex;
std::vector< interruptible_thread_9_9 > background_threads;

struct fs_change {
    bool has_changes() const { return has_changes_; }

    bool has_changes_ { true };
};
fs_change get_fs_changes(int id) {
    if ((std::rand() % 2) == 0) { return { false }; }

    return {};
}
void update_index(fs_change) {
    return;
}
void process_gui_until_exit() {
    std::this_thread::sleep_for(std::chrono::seconds(5));
}
void background_thread(int disk_id) {
    while (true) {
        interruption_point();
        fs_change fsc = get_fs_changes(disk_id);
        if (fsc.has_changes()) { update_index(fsc); }
    }
}
void start_background_processing() {
    background_threads.push_back(interruptible_thread_9_9(std::bind(background_thread, 1)));
    background_threads.push_back(interruptible_thread_9_9(std::bind(background_thread, 2)));
}

void run_9_13() {
    start_background_processing();
    process_gui_until_exit();
    std::unique_lock< std::mutex > lk(config_mutex);
    for (unsigned i = 0; i < background_threads.size(); ++i) { background_threads[i].interrupt(); }
    for (unsigned i = 0; i < background_threads.size(); ++i) { background_threads[i].join(); }
}

int main() {
    std::srand(1);

    auto il = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27 };

    auto result   = parallel_accumulate_9_3(il.begin(), il.end(), 0);
    auto n        = (*(il.end() - 1));
    auto expected = n * (n + 1) / 2;
    assert(result == expected);

    std::list< int > vals_8_1 { 9, 5, 7, 6, 8, 2, 1, 3, 4 };
    auto             res = parallel_quick_sort_9_5(vals_8_1);

    for (size_t i = 0; i < vals_8_1.size(); ++i) {
        assert(res.front() == i + 1);
        res.pop_front();
    }

    run_9_13();
}