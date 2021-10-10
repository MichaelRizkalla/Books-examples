
cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(books_examples LANGUAGES CXX)

set(PROJECT_BINARY_DIR ${CMAKE_CURRENT_LIST_DIR})
set(BASE_INSTALL_DIR "${PROJECT_BINARY_DIR}/bin/")

add_subdirectory(C++_Concurrency_in_Action_2ndEd)