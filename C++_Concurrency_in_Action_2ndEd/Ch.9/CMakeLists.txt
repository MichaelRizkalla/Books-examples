set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Ch9 "source.cpp" "threadpool.h" "accumulate.h" "function_wrapper.h" "quicksort.h" "work_stealing_queue.h" "interruptible_thread.h")

install(TARGETS Ch9 RUNTIME DESTINATION ${INSTALL_DIR})
