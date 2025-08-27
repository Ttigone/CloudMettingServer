#include <iostream>

#include "unp.h"
#include "unpthread.h"
using namespace std;

Thread *tptr;
socklen_t addrlen;
int listenfd;
int navail, nprocesses;

Room *room;

void sig_chld(int signo);
int i, maxfd;
void thread_make(int);
void process_make(int, int);

int main(int argc, char **argv) {
  // 注册信号处理函数
  Signal(SIGCHLD, sig_chld);

  // 文件描述符集合
  fd_set rset, masterset;
  FD_ZERO(&masterset);  // 清空集合

  if (argc == 4) {
    listenfd = Tcp_listen(NULL, argv[1], &addrlen);
  } else if (argc == 5) {
    listenfd = Tcp_listen(argv[1], argv[2], &addrlen);
  } else {
    err_quit("usage: ./app [host] <port #> <#threads> <#processes>");
  }
  maxfd = listenfd;

  int nthreads = atoi(argv[argc - 2]);  // 线程数量
  nprocesses = atoi(argv[argc - 1]);    // 进程数量

  // 30 个线程 10 个进程
  // 初始化房间
  room = new Room(nprocesses);  // 进程数量

  printf("total threads: %d  total process: %d\n", nthreads, nprocesses);

  tptr = (Thread *)Calloc(nthreads, sizeof(Thread));

  // 进程池
  // i 是房间序号
  for (i = 0; i < nprocesses; i++) {
    // 创建进程, 适合设置进程的属性
    process_make(i, listenfd);
    // 将所有子进程的通信管道加入监听集合
    // root 中的通信管道是读端, 加入到监听集合中, 监听是否房间空闲,
    // 用户退出房间等信息
    FD_SET(room->pptr[i].child_pipefd, &masterset);
    maxfd = max(maxfd, room->pptr[i].child_pipefd);
  }

  // 线程池
  // 负责接收客户链接请求, 处理创建会议和加入会议的逻辑
  for (i = 0; i < nthreads; i++) {
    // 创建工作线程
    thread_make(i);
  }

  // 主事件循环
  for (;;) {
    // listen
    rset = masterset;

    // 循环调用, 监听与每个子进程的读端
    int nsel = Select(maxfd + 1, &rset, NULL, NULL, NULL);
    if (nsel == 0) {
      continue;
    }

    // set room status to 0(empty)
    //  检查那个子进程发送了消息
    for (i = 0; i < nprocesses; i++) {
      // 判断子进程的通信管道是否在监听集合中
      if (FD_ISSET(room->pptr[i].child_pipefd, &rset)) {
        char rc;
        int n;
        // 读取状态字符
        if ((n = Readn(room->pptr[i].child_pipefd, &rc, 1)) <= 0) {
          err_quit("child %d terminated unexpectedly", i);
        }
        // printf("c = %c\n", rc);
        if (rc == 'E') {
          // 空房间
          pthread_mutex_lock(&room->lock);
          room->pptr[i].child_status = 0;
          room->navail++;
          printf("room %d is now free\n", room->pptr[i].child_pid);
          pthread_mutex_unlock(&room->lock);

        } else if (rc == 'Q') {
          // partner quit
          Pthread_mutex_lock(&room->lock);
          room->pptr[i].total--;
          Pthread_mutex_unlock(&room->lock);
        } else {
          // trash data
          err_msg("read from %d error", room->pptr[i].child_pipefd);
          continue;
        }

        if (--nsel == 0) {
          break; /*all done with select results*/
        }
      }
    }
  }
  return 0;
}

/// @brief 创建线程, 每个线程执行 thread_main 函数
/// @param i 线程编号
void thread_make(int i) {
  // 函数指针
  void *thread_main(void *);
  int *arg = (int *)Calloc(1, sizeof(int));
  *arg = i;
  // 主进程中的创建线程, 执行 thread_main 函数
  Pthread_create(&tptr[i].thread_tid, NULL, thread_main, arg);
}

/// @brief 创建进程
/// @param i 进程号
/// @param listenfd
/// @return
int process_make(int i, int listenfd) {
  pid_t pid;
  void process_main(int, int);

  // 创建进程间通信通道
  // 一对 Unix 域套接字, 用于父子进程双向通信
  int sockfd[2];
  Socketpair(AF_LOCAL, SOCK_STREAM, 0, sockfd);

  // 复制父进程的内存控件, 包括代码区, 数据段, 堆栈.
  if ((pid = fork()) > 0) {
    // 父进程逻辑
    Close(sockfd[1]);  // 关闭写端
    // 外面先一步执行了对 room 进程的内存分配
    room->pptr[i].child_pid = pid;           // 存储子进程 pid
    room->pptr[i].child_pipefd = sockfd[0];  // 保存读端, 接收子进程消息
    room->pptr[i].child_status = 0;          // 子进程状态, 0 表示空闲
    room->pptr[i].total = 0;                 // 用户数
    return pid;                              // father
  } else {
    // 监听套接字
    // 子进程逻辑
    Close(listenfd);             // child not need this open
    Close(sockfd[0]);            // 关闭读端
    process_main(i, sockfd[1]);  // 执行子进程的主逻辑, 永远不会返回, 执行写端
  }
}

// typedef struct Room_Pool
//{
//     Room *room_pool[100]; //pool
//     int num;

//    Room_Pool()
//    {
//        memset(room_pool, 0, sizeof(room_pool));
//        num = 0;
//    }
//}Room_Pool;