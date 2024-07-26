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
#include <csignal>
#include <thread>
#include "epoll_define.h"

using namespace std;


template <typename ...Args>
std::string format(Args ...arg){
    std::stringstream ss;
    (ss<<...<<arg);
    return ss.str();
}

inline std::string get_cur_time(){
    char ctime[100]={0};
    std::string strtemp;
    time_t timep;
    struct tm *p;
    time(&timep);
    p = gmtime(&timep);
    sprintf(ctime, "%d-%02d-%02d %02d:%02d:%02d", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, (8+p->tm_hour)%24,p->tm_min,p->tm_sec);
    strtemp = ctime;
    return strtemp;
}

void sig_handle([[maybe_unused]] int signo){
    std::cout<<format("程序接收到退出信号",signo)<<std::endl;

    //释放资源
    epoll_ctl(epoll_instance, EPOLL_CTL_DEL,epoll_lisen_fd,NULL);
    shutdown(epoll_lisen_fd, SHUT_RDWR);
    close(epoll_lisen_fd);

    is_stop = true;
}

void epoll_sv_exit_catch(){
   signal(SIGTERM, sig_handle);
   signal(SIGINT, sig_handle);
}

bool create_server_linstener(const char * ip, int port){
    epoll_lisen_fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);

    int on = 1;
    setsockopt(epoll_lisen_fd, SOL_SOCKET, SO_REUSEADDR,(char*)&on, sizeof (on));
    setsockopt(epoll_lisen_fd, SOL_SOCKET, SO_REUSEPORT,(char*)&on, sizeof (on));

    struct sockaddr_in srv_addr;
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = inet_addr(ip);
    srv_addr.sin_port = htons(port);

    if(::bind(epoll_lisen_fd, (sockaddr*)&srv_addr, sizeof(srv_addr)) == -1){
        std::cout<<"bind failed."<<std::endl;
        return false;
    }
    if(::listen(epoll_lisen_fd, SOMAXCONN) == -1){
        std::cout<<"listen failed."<<std::endl;
        return false;
    }
    epoll_instance = epoll_create(1);
    if(epoll_instance == -1)
        return false;

    struct epoll_event e;
    memset(&e, 0, sizeof (e));
    e.events = EPOLLIN | EPOLLRDHUP;
    e.data.fd = epoll_lisen_fd;

    if(::epoll_ctl(epoll_instance, EPOLL_CTL_ADD, epoll_lisen_fd, &e) == -1){
        return false;
    }
    return true;
}

void release_client(int client_fd){
    if(epoll_ctl(epoll_instance, EPOLL_CTL_DEL, client_fd,nullptr) == -1){
        std::cout<<"epoll del monitor fd "<<client_fd<<" failed!"<<std::endl;
    }
    else
        std::cout<<"客户端已断开, client_fd: "<<client_fd<<std::endl;
    close(client_fd);
}

void accept_client(){
    while(!is_stop){
        std::unique_lock<std::mutex> ul(accept_mutex);
        accept_cv.wait(ul, [=](){return accept_ready;});
        accept_ready = false;
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof (client_addr);
        int client_fd = accept(epoll_lisen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1)
            continue;
        std::cout<<format(get_cur_time()," -- 新客户端连接: ",inet_ntoa(client_addr.sin_addr)," 端口号： ",ntohs(client_addr.sin_port),", client_fd is ",client_fd)<<std::endl;

        int old_flag = fcntl(client_fd, F_GETFL, 0);
        int new_flag = old_flag | O_NONBLOCK;
        if(fcntl(client_fd, F_SETFL, new_flag) == -1){
            std::cout<<"accept fcnt set flag nonblock failed. errno "<<errno<<" "<<strerror(errno)<<std::endl;
        }

        //设置监听
        struct epoll_event e;
        memset(&e, 0, sizeof (e));
        e.events = EPOLLIN | EPOLLET;
        e.data.fd = client_fd;
        if(epoll_ctl(epoll_instance, EPOLL_CTL_ADD, client_fd, &e) == -1){
            std::cout<<"epoll add monitor fd "<<client_fd<<" failed! errno "<<errno<<" "<<strerror(errno)<<std::endl;
            return ;
        }
    }
}

static void work_func(int thread_no){
    std::cout<<format("工作线程编号: ",thread_no," 已启动")<<std::endl;
    while(!is_stop){
        std::unique_lock<std::mutex> ul (work_mutex);
        work_cv.wait(ul, [=](){return !client_sockets.empty();});

        auto client_fd = client_sockets.front();
        client_sockets.pop();

        bool is_disconnect = false;
        //接收
        std::string recv_string{};
        while(true){ //边缘触发，一次读完
            char recv_buf[64]={0};
            int m = recv(client_fd, recv_buf,64,0);
            if(m == 0){
                release_client(client_fd);
                is_disconnect = true;
                break;
            }
            else if(m<0){
                if(errno != EWOULDBLOCK && errno != EINTR){
                    release_client(client_fd);
                    is_disconnect = true;
                    break;
                }
                if(errno == EAGAIN){ //读取数据完毕
                    std::cout<<"读取数据完毕"<<std::endl;
                    break;
                }
            }
            else{
                recv_string += recv_buf;
            }
        }
        if(is_disconnect)
            continue ;
        //std::cout<<"客户端接收："<<recv_string<<", client_fd: "<<client_fd<<std::endl;
        std::cout<<format(get_cur_time(),"-","线程no",thread_no,"--接收客户端：",client_fd, "，数据：",recv_string)<<std::endl;
        //发送
        while(true){
            int sendn = send(client_fd, recv_string.c_str(),recv_string.length(),0);
            if(sendn == -1){
                if(errno == EWOULDBLOCK){
                    ::usleep(10);
                    continue;
                }
                else{
                    std::cout<<"send error,fd "<<client_fd<<std::endl;
                    release_client(client_fd);
                    break;
                }
            }

            std::cout<<format(get_cur_time(),"-","线程no",thread_no,"发送客户端：",client_fd, "，数据：",recv_string)<<std::endl;
            recv_string.erase(0,sendn);
            if(recv_string.empty())
                break;
        }
    }
}


int main(int argc, char*argv[]){
    if(argc<3){
        std::cout<<"输入参数 argv1 argv2"<<std::endl;
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);

    if(!create_server_linstener(ip, port)){
        return 2;
    }

    epoll_sv_exit_catch();

    std::thread accept_thread([=](){accept_client();});
    std::cout<<"accept线程已启动"<<std::endl;

    for(auto i=0; i<WORKER_THREAD_NUM; i++){

        work_threads[i] = (std::make_unique<std::thread>([=]()->void{work_func(i+1);}));
        std::thread::id thread_id = work_threads[i]->get_id();
        std::ostringstream oss;
        oss<<thread_id;
        std::cout<<"新建线程id: "<<oss.str()<<std::endl;
    }

    while(!is_stop){
        struct epoll_event ev[1024];
        int e_n = epoll_wait(epoll_instance, ev, 1024,10);
        if (e_n == 0){
            continue;
        }
        else if(e_n < 0 ){
            std::cout<<"epoll_wait error."<<std::endl;
            std::cout<<"errno is "<<errno<<" 错误信息： "<<strerror(errno)<<std::endl;
            continue;
        }
        int m = min(e_n, 1024);
        for(auto j=0; j < m; j++){
            if(ev[j].data.fd == epoll_lisen_fd){
                accept_ready = true;
                accept_cv.notify_one();
            }
            else{
                client_sockets.push(ev->data.fd);
                work_cv.notify_all();
            }
        }
    }
    return 0;
}











