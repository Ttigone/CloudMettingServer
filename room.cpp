#include <iomanip>
#include <iostream>
#include <map>

#include "msg.h"
#include "unp.h"
#include "unpthread.h"

#define SENDTHREADSIZE 5
SEND_QUEUE sendqueue;  // save data

enum USER_TYPE { GUEST = 2, OWNER };
static volatile int maxfd;
STATUS volatile roomstatus = ON;

typedef struct pool {
  fd_set fdset;
  pthread_mutex_t lock;
  int owner;
  int num;
  int status[1024 + 10];
  std::map<int, uint32_t> fdToIp;
  pool() {
    memset(status, 0, sizeof(status));
    owner = 0;
    FD_ZERO(&fdset);
    lock = PTHREAD_MUTEX_INITIALIZER;
    num = 0;
  }

  void clear_room() {
    Pthread_mutex_lock(&lock);
    roomstatus = CLOSE;
    for (int i = 0; i <= maxfd; i++) {
      if (status[i] == ON) {
        Close(i);
      }
    }
    memset(status, 0, sizeof(status));
    num = 0;
    owner = 0;
    FD_ZERO(&fdset);
    fdToIp.clear();
    sendqueue.clear();
    Pthread_mutex_unlock(&lock);
  }
} Pool;

Pool *user_pool = new Pool();

// 每个房间是一个子进程
// 在子进程中执行

/// @brief 子进程执行逻辑, 每个子进程代表一个会议房间
/// @param i 会议编号
/// @param fd 对应的写端文件描述符
void process_main(int i, int fd) {
  // create accept fd thread
  printf("room %d starting \n", getpid());
  Signal(SIGPIPE, SIG_IGN);  // 忽略管道破裂的信号
  pthread_t pfd1;
  void *accept_fd(void *);
  void *send_func(void *);
  void fdclose(int, int);

  // 指向写端文件描述符的指针
  int *ptr = (int *)malloc(4);
  *ptr = fd;

  // 创建 接收文件描述的线程
  Pthread_create(&pfd1, NULL, accept_fd, ptr);  // accept fd
  // 创建 5 个消息发送线程
  for (int i = 0; i < SENDTHREADSIZE; i++) {
    // 发送线程函数
    Pthread_create(&pfd1, NULL, send_func, NULL);
  }

  // listen read data from fds
  // 数据接收处理
  for (;;) {
    // 复制文件描述符集合
    fd_set rset = user_pool->fdset;
    int nsel;
    struct timeval time;
    // 时间结构体
    memset(&time, 0, sizeof(struct timeval));
    // 使用 select 监听所有客户端链接
    while ((nsel = Select(maxfd + 1, &rset, NULL, NULL, &time)) == 0) {
      // 确保 rset 更新
      rset = user_pool->fdset;
    }
    for (int i = 0; i <= maxfd; i++) {
      // 检查每个文件描述符是否有数据到达
      if (FD_ISSET(i, &rset)) {
        // 处理客户端数据
        char head[15] = {0};           // 头部存储 11 字节
        int ret = Readn(i, head, 11);  // 消息头 11 字节
        if (ret <= 0) {
          printf("peer close or read error\n");
          fdclose(i, fd);
        } else if (ret != 11) {
          err_msg("incomplete message header, expected 11 bytes, got %d", ret);
          //   fdclose(i, fd);
        } else {
          // 验证消息开始标记
          if (head[0] != '$') {
            err_msg("invalid message start marker: 0x%02x",
                    (unsigned char)head[0]);
            // printf("head error %d\n", head[0]);
            for (size_t i = 0; i < 15; i++) {
              std::cout << "Byte[" << i << "]: 0x" << std::hex << std::setw(2)
                        << std::setfill('0') << static_cast<int>(head[i])
                        << " ";
            }
            fdclose(i, fd);  // 关闭连接
            break;
          }
          MSG_TYPE msgtype;
          // 解析消息类型(2字节) 最大支持 255 中消息类型
          memcpy(&msgtype, head + 1, 2);
          msgtype = (MSG_TYPE)ntohs(msgtype);
          if (msgtype < IMG_SEND || msgtype > PARTNER_JOIN2) {
            err_msg("invalid message type: %d", msgtype);
            fdclose(i, fd);
            break;
          }
          MSG msg;
          // 拷贝消息
          memset(&msg, 0, sizeof(MSG));
          msg.targetfd = i;
          // 拷贝 ip(4字节)
          memcpy(&msg.ip, head + 3, 4);
          int msglen;
          // 解析数据长度(4字节)
          memcpy(&msglen, head + 7, 4);
          //
          msg.len = ntohl(msglen);

          // 处理发送类型
          if (msgtype == IMG_SEND || msgtype == AUDIO_SEND ||
              msgtype == TEXT_SEND) {
            // 图片发送转化成图片接收
            // 音频发送转化成音频接收
            // 文本发送转化成文本接收
            msg.msgType =
                (msgtype == IMG_SEND)
                    ? IMG_RECV
                    : ((msgtype == AUDIO_SEND) ? AUDIO_RECV : TEXT_RECV);
            msg.ptr = (char *)malloc(msg.len);  // 分配内存读取数据
            msg.ip = user_pool->fdToIp[i];      // 获取发送者 ip

            // 读取实际数据
            if ((ret = Readn(i, msg.ptr, msg.len)) < msg.len) {
              err_msg("3 msg format error");
            } else {
              int tail;
              Readn(i, &tail, 1);
              if (tail != '#') {
                err_msg("4 msg format error");
              } else {
                // 加入发送队列
                sendqueue.push_msg(msg);
              }
            }
          } else if (msgtype == CLOSE_CAMERA) {
            // 关闭摄像头
            char tail;
            Readn(i, &tail, 1);
            if (tail == '#' && msg.len == 0) {
              msg.msgType = CLOSE_CAMERA;
              sendqueue.push_msg(msg);
            } else {
              err_msg("camera data error ");
            }
          }
        }
        if (--nsel <= 0) break;
      }
    }
  }
}

void fdclose(int fd, int pipefd) {
  if (user_pool->owner == fd) {
    // room close
    user_pool->clear_room();
    printf("clear room\n");
    // write to father process
    char cmd = 'E';
    if (writen(pipefd, &cmd, 1) < 1) {
      err_msg("writen error");
    }
  } else {
    uint32_t getpeerip(int);
    uint32_t ip;
    // delete fd from pool
    Pthread_mutex_lock(&user_pool->lock);
    ip = user_pool->fdToIp[fd];
    FD_CLR(fd, &user_pool->fdset);
    user_pool->num--;
    user_pool->status[fd] = CLOSE;
    if (fd == maxfd) maxfd--;
    Pthread_mutex_unlock(&user_pool->lock);

    char cmd = 'Q';
    if (writen(pipefd, &cmd, 1) < 1) {
      err_msg("write error");
    }

    // msg ipv4

    MSG msg;
    memset(&msg, 0, sizeof(MSG));
    msg.msgType = PARTNER_EXIT;
    msg.targetfd = -1;
    msg.ip = ip;  // network order
    Close(fd);
    sendqueue.push_msg(msg);
  }
}

void *accept_fd(void *arg)  // accept fd from father
{
  uint32_t getpeerip(int);
  Pthread_detach(pthread_self());
  int fd = *(int *)arg, tfd = -1;
  free(arg);
  while (1) {
    int n, c;
    if ((n = read_fd(fd, &c, 1, &tfd)) <= 0) {
      err_quit("read_fd error");
    }
    if (tfd < 0) {
      printf("c = %c\n", c);
      err_quit("no descriptor from read_fd");
    }

    // add to poll

    if (c == 'C')  // create
    {
      Pthread_mutex_lock(&user_pool->lock);  // lock

      FD_SET(tfd, &user_pool->fdset);
      user_pool->owner = tfd;
      user_pool->fdToIp[tfd] = getpeerip(tfd);
      user_pool->num++;
      //            user_pool->fds[user_pool->num++] = tfd;
      user_pool->status[tfd] = ON;
      maxfd = MAX(maxfd, tfd);
      // printf("c %d\n", maxfd);
      // write room No to  tfd
      roomstatus = ON;  // set on

      Pthread_mutex_unlock(&user_pool->lock);  // unlock

      MSG msg;
      msg.msgType = CREATE_MEETING_RESPONSE;
      msg.targetfd = tfd;
      int roomNo = htonl(getpid());
      msg.ptr = (char *)malloc(sizeof(int));
      memcpy(msg.ptr, &roomNo, sizeof(int));
      msg.len = sizeof(int);
      sendqueue.push_msg(msg);

      //            printf("create meeting: %d\n", tfd);

    } else if (c == 'J')  // join
    {
      Pthread_mutex_lock(&user_pool->lock);  // lock
      if (roomstatus == CLOSE)               // meeting close (owner close)
      {
        close(tfd);
        Pthread_mutex_unlock(&user_pool->lock);  // unlock
        continue;
      } else {
        FD_SET(tfd, &user_pool->fdset);
        user_pool->num++;
        //                user_pool->fds[user_pool->num++] = tfd;
        user_pool->status[tfd] = ON;
        maxfd = MAX(maxfd, tfd);
        user_pool->fdToIp[tfd] = getpeerip(tfd);
        Pthread_mutex_unlock(&user_pool->lock);  // unlock

        // broadcast to others
        MSG msg;
        memset(&msg, 0, sizeof(MSG));
        msg.msgType = PARTNER_JOIN;
        msg.ptr = NULL;
        msg.len = 0;
        msg.targetfd = tfd;
        msg.ip = user_pool->fdToIp[tfd];
        sendqueue.push_msg(msg);

        // broadcast to others
        MSG msg1;
        memset(&msg1, 0, sizeof(MSG));
        msg1.msgType = PARTNER_JOIN2;
        msg1.targetfd = tfd;
        int size = user_pool->num * sizeof(uint32_t);

        msg1.ptr = (char *)malloc(size);
        int pos = 0;

        for (int i = 0; i <= maxfd; i++) {
          if (user_pool->status[i] == ON && i != tfd) {
            uint32_t ip = user_pool->fdToIp[i];
            memcpy(msg1.ptr + pos, &ip, sizeof(uint32_t));
            pos += sizeof(uint32_t);
            msg1.len += sizeof(uint32_t);
          }
        }
        sendqueue.push_msg(msg1);

        printf("join meeting: %d\n", msg.ip);
      }
    }
  }
  return NULL;
}

void *send_func(void *arg) {
  Pthread_detach(pthread_self());
  char *sendbuf = (char *)malloc(4 * MB);
  /*
   * $_msgType_ip_size_data_#
   */
  // 处理消息转发
  for (;;) {
    memset(sendbuf, 0, 4 * MB);
    MSG msg = sendqueue.pop_msg();
    int len = 0;

    sendbuf[len++] = '$';
    short type = htons((short)msg.msgType);
    memcpy(sendbuf + len, &type, sizeof(short));  // msgtype
    len += 2;

    if (msg.msgType == CREATE_MEETING_RESPONSE ||
        msg.msgType == PARTNER_JOIN2) {
      len += 4;
    } else if (msg.msgType == TEXT_RECV || msg.msgType == PARTNER_EXIT ||
               msg.msgType == PARTNER_JOIN || msg.msgType == IMG_RECV ||
               msg.msgType == AUDIO_RECV || msg.msgType == CLOSE_CAMERA) {
      memcpy(sendbuf + len, &msg.ip, sizeof(uint32_t));
      len += 4;
    }

    int msglen = htonl(msg.len);
    memcpy(sendbuf + len, &msglen, sizeof(int));
    len += 4;
    memcpy(sendbuf + len, msg.ptr, msg.len);
    len += msg.len;
    sendbuf[len++] = '#';

    Pthread_mutex_lock(&user_pool->lock);

    if (msg.msgType == CREATE_MEETING_RESPONSE) {
      // send buf to target
      if (writen(msg.targetfd, sendbuf, len) < 0) {
        err_msg("writen error");
      }
    } else if (msg.msgType == PARTNER_EXIT || msg.msgType == IMG_RECV ||
               msg.msgType == AUDIO_RECV || msg.msgType == TEXT_RECV ||
               msg.msgType == CLOSE_CAMERA) {
      for (int i = 0; i <= maxfd; i++) {
        if (user_pool->status[i] == ON && msg.targetfd != i) {
          if (writen(i, sendbuf, len) < 0) {
            err_msg("writen error");
          }
        }
      }
    } else if (msg.msgType == PARTNER_JOIN) {
      for (int i = 0; i <= maxfd; i++) {
        if (user_pool->status[i] == ON && i != msg.targetfd) {
          if (writen(i, sendbuf, len) < 0) {
            err_msg("writen error");
          }
        }
      }
    } else if (msg.msgType == PARTNER_JOIN2) {
      for (int i = 0; i <= maxfd; i++) {
        if (user_pool->status[i] == ON && i == msg.targetfd) {
          if (writen(i, sendbuf, len) < 0) {
            err_msg("writen error");
          }
        }
      }
    }

    Pthread_mutex_unlock(&user_pool->lock);

    // free
    if (msg.ptr) {
      free(msg.ptr);
      msg.ptr = NULL;
    }
  }
  free(sendbuf);

  return NULL;
}
