set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Ch8 "source.cpp" "accumulate.h" "foreach.h" "find.h" "partial_sum.h" "jointhreads.h")

install(TARGETS Ch8 RUNTIME DESTINATION ${INSTALL_DIR})
