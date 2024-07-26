# epoll-m
Linux环境，epoll实现高性能网络服务架构
1、结构图

![epoll框架](./pics/epoll框架.png)


2、工作原理

​	采用同步io的epoll模型，涉及主线程和handle线程：

​	1、主线程监听socket可读事件，并加入到客户端socket队列中

​	2、accept线程负责接收客户端连接，并添加新的监听socket

​	3、handle线程负责处理socket可读队列
