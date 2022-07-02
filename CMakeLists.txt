cmake_minimum_required(VERSION 3.21)
project(return_test)

set(CMAKE_CXX_STANDARD 17)
SET (CMAKE_CXX_FLAGS "-g -Wall -march=native")

add_executable(return_test main.cpp)
