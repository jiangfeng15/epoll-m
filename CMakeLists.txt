cmake_minimum_required(VERSION 3.5)

project(epoll_sv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(epoll_sv main.cpp)

add_executable(epoll_nwsv
    epoll_define.h
    epoll_main.cc)

target_link_libraries(epoll_nwsv -fPIC -rdynamic

    pthread)
