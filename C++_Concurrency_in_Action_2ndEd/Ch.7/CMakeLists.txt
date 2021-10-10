set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Ch7 "source.cpp" "lockfree_stack.h" "lockfree_queue.h")

install(TARGETS Ch7 RUNTIME DESTINATION ${INSTALL_DIR})
