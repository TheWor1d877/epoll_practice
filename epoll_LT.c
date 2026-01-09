#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<errno.h>
#include <time.h>
#include "process_data.h"

#define MAX_EVENTS 10
#define PORT 8080
#define BUFFER_SIZE 1024
static long total_reads = 0;
static time_t last_read = 0;

/*
 * 设置fd为非阻塞模式
 * */
void set_nonblocking(int fd){
    int flags = fcntl(fd,F_GETFL,0); //获取文件描述符 fd 的文件状态标志
    fcntl(fd,F_SETFL,flags | O_NONBLOCK); // 设置非阻塞模式
}
/*
 * 创建TCP服务器
 * */
int create_server_socket(int port){
    int server_fd;
    struct sockaddr_in address;

    // 创建 TCP 服务器套接字
    if((server_fd = socket(AF_INET,SOCK_STREAM,0)) == 0){
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    // 设置 SO_REUSEADDR 选项
    // 当 TCP 服务器关闭后，其监听的端口会进入 TIME_WAIT 状态（通常持续 30~120 秒）。
    // 如果没有 SO_REUSEADDR，再次启动服务器绑定同一端口会失败：bind: Address already in use
    if(setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))){
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    //bind
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if(bind(server_fd,(struct sockaddr *)&address,sizeof(address)) < 0){
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    //listen
    if(listen(server_fd,SOMAXCONN) < 0){
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n",port);
    return server_fd;
}

int main(){
    int server_fd,epoll_fd;
    struct epoll_event event,events[MAX_EVENTS];
    //创建监听描述符
    server_fd = create_server_socket(PORT);
    set_nonblocking(server_fd);

    if((epoll_fd = epoll_create1(0)) < 0){
        perror("epoll_create1 failed");
        exit(EXIT_FAILURE);
    }
    event.events = EPOLLIN; // server 水平触发
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl: server_fd");
        exit(EXIT_FAILURE);
    }
    printf("Epoll server started...\n");

    while(1){
        int nfds = epoll_wait(epoll_fd,events,MAX_EVENTS,-1);
        if(nfds == -1){
            if(errno == EINTR){
                // 被信号中断，属于正常情况，继续等待  
                // 或直接进入下一轮循环
                continue;
            }
            perror("epoll_wait");
            break;
        }
        for(int i=0;i<nfds;i++){
            // connect
            if(events[i].data.fd == server_fd){  //重点：就按照顺序处理所有的server_fd 就可以
                // 场景：瞬间1000个客户端同时连接
                // LT模式：epoll_wait会返回1000次，每次处理1个连接
                // ET模式：epoll_wait返回1次，循环accept处理1000个连接
                struct sockaddr_in client_addr;
                socklen_t addrlen = sizeof(client_addr);
                int client_fd = accept(server_fd,
                                (struct sockaddr *)&client_addr,
                                &addrlen);

                if(client_fd == -1){
                    perror("accept error");
                    break; 
                }
                set_nonblocking(client_fd);
                

                // 获取客户端信息
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, 
                            client_ip, INET_ADDRSTRLEN);
                printf("New connection from %s:%d\n", 
                        client_ip, ntohs(client_addr.sin_port));
                
                // 这里添加客户端socket，但用LT模式：
                // 场景：客户端发送10MB文件
                // ET模式：只通知一次，必须一次读完，容易丢数据
                // LT模式：多次通知，可以慢慢读，安全可靠
                event.events = EPOLLIN;   // ⭐ 注意：不加EPOLLET！这是LT模式
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("epoll_ctl: client_fd");
                    close(client_fd);
                }
            }


            //process data
            else{
                int client_fd = events[i].data.fd;
                if(events[i].events & (EPOLLRDHUP | EPOLLHUP)){
                    printf("Client %d disconnected\n", client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    continue;
                }
                if(events[i].events & EPOLLIN){
                    char buffer[BUFFER_SIZE];
                    ssize_t bytes_read;
                    bytes_read = read(client_fd,buffer,BUFFER_SIZE-1); 
                    //这里没有while循环，不用一次将缓冲区的数据都读取出来
                    total_reads++;
                    if(bytes_read == -1){
                        perror("read buffer failed");
                        epoll_ctl(epoll_fd,EPOLL_CTL_DEL,client_fd,NULL);
                        close(client_fd);
                        break;
                    }
                    else if(bytes_read == 0){
                        // connect close
                        printf("Client %d closed connection\n",client_fd);
                        epoll_ctl(epoll_fd,EPOLL_CTL_DEL,client_fd,NULL);
                        close(client_fd);
                        break;
                    }
                    else{
                        buffer[bytes_read] = '\0';
                        printf("Receive fromm client %d : %s\n",client_fd,buffer);
                        process_buffer_data(buffer);
                        write(client_fd,buffer,bytes_read);
                    }
                }
            }
        }
        time_t now = time(NULL);
        if(now - last_read >= 1) {
            FILE *f = fopen("./epoll_server.log", "w");
            if (f) {
            fprintf(f, "%ld\n", total_reads);
            fclose(f);
            }
            last_read = now;  // 👈 注意：这里应该是 last_log = now;
        }
    }
    close(server_fd);
    close(epoll_fd);
    printf("Total read() calls: %ld\n", total_reads);
    return 0;
}


