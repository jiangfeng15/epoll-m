#ifndef EPOLL_DEFINE_H
#define EPOLL_DEFINE_H
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>

#define WORKER_THREAD_NUM 5


#define min(a,b) ((a)>(b)?(b):(a))

int epoll_instance = 0;
int epoll_lisen_fd = 0;

std::condition_variable work_cv;
std::mutex work_mutex;

std::condition_variable accept_cv;
std::mutex accept_mutex;

std::unique_ptr<std::thread> work_threads[WORKER_THREAD_NUM];
//socket队列
std::queue<int> client_sockets;

bool is_stop{false};

bool accept_ready = {false};
#endif // EPOLL_DEFINE_H
