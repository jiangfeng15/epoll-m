#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <poll.h>
#include <string.h>
#include <sstream>

using namespace std;

template <typename ...Args>
std::string format(Args ...arg){
    std::stringstream ss;
    (ss<<...<<arg);
    return ss.str();
}

int main()
{
    int listen_fd = socket(AF_INET, SOCK_STREAM,0);
    if(listen_fd == -1){
        std::cout<<"create listen socket error."<<std::endl;
        return -1;
    }

    //set socket not blocking
    int old_socket_flag = fcntl(listen_fd, F_GETFL, 0);
    int new_socket_flag = old_socket_flag | O_NONBLOCK;
    if(fcntl(listen_fd, F_SETFL, new_socket_flag) == -1){
        close(listen_fd);
        std::cout<<"set listen_fd to nonblock error"<<std::endl;
        return -2;
    }

    //set server addr
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(3000);
    if(bind(listen_fd, (struct sockaddr*)&server_addr,sizeof(server_addr))==-1){
        std::cout<<"bind listen socket error."<<std::endl;
        return-3;
    }

    if(listen(listen_fd, SOMAXCONN)==-1){
        std::cout<<"listen error."<<std::endl;
        return -4;
    }

    int on = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&on,sizeof(on));
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, (char*)&on,sizeof(on));

    int epollfd = epoll_create(1);
    if(epollfd == -1){
        std::cout<<"create epollfd error."<<std::endl;
        close(listen_fd);
        return -5;
    }

    epoll_event listen_fd_event;
    listen_fd_event.events = POLLIN;
    listen_fd_event.data.fd = listen_fd;

    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_fd, &listen_fd_event) == -1){
        std::cout<<"epoll_ctl error."<<std::endl;
        close(listen_fd);
        return -1;
    }

    int num_of_work_socket = 0;
    while(true){
        epoll_event epoll_events[1024];
        num_of_work_socket = epoll_wait(epollfd, epoll_events,1024,1000);
        if(num_of_work_socket < 0){
            if(errno == EINTR)
                continue;
            break;
        }
        else if(num_of_work_socket == 0){
            continue;
        }

        for(size_t i = 0; i<num_of_work_socket; i++){
            if(epoll_events[i].events &POLLIN){
                if(epoll_events[i].data.fd == listen_fd){
                    //接受连接
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);

                    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len);
                    if(client_fd != -1){
                        int old_socket_flag = fcntl(client_fd, F_GETFL, 0);
                        int new_socket_flag = old_socket_flag | O_NONBLOCK;
                        if(fcntl(client_fd, F_SETFL, new_socket_flag) == -1){
                            close(client_fd);
                            std::cout<<"set client_fd to nonblock error."<<std::endl;
                        }
                        else{
                            epoll_event client_fd_event;
                            client_fd_event.events = POLLIN;
                            client_fd_event.data.fd = client_fd;
                            if(epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &client_fd_event) != -1){
                                std::cout<<"new client accepted, client_fd: "<<client_fd<<std::endl;
                            }
                            else{
                                std::cout<<"add client fd to epollfd error."<<std::endl;
                                close(client_fd);
                            }
                        }

                    }
                }
                else{ //client_fd;
                    //边缘触发方式接收数据
                    while(true){
                        char recv_buf[64]={0};
                        int m = recv(epoll_events[i].data.fd, recv_buf,64,0);
                        if(m == 0){
                            if(epoll_ctl(epollfd, EPOLL_CTL_DEL, epoll_events[i].data.fd,NULL) != -1){
                                std::cout<<"客户端已断开, client_fd: "<<epoll_events[i].data.fd<<std::endl;
                            }
                            close(epoll_events[i].data.fd);
                        }
                        else if(m<0){
                            if(errno != EWOULDBLOCK && errno != EINTR){
                                if(epoll_ctl(epollfd, EPOLL_CTL_DEL, epoll_events[i].data.fd,NULL) == -1){
                                    std::cout<<"client disconnected, client_fd: "<<epoll_events[i].data.fd<<std::endl;
                                }
                                close(epoll_events[i].data.fd);
                                break;
                            }
                            if(errno == EAGAIN){ //读取数据完毕
                                std::cout<<"读取数据完毕"<<std::endl;
                                break;
                            }
                        }
                        else{
                            std::cout<<"接收到客户端："<<recv_buf<<", client_fd: "<<epoll_events[i].data.fd<<std::endl;
                            m = send(epoll_events[i].data.fd, recv_buf, 64, 0);
                            if(m<0){
                                std::cout<<"发送数据失败"<<std::endl;
                            }
                        }
                    }
                }
            }
            else if(epoll_events[i].events & POLLERR){

            }
        }

    }
    close(listen_fd);
    return 0;
}
