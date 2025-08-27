#ifndef UNPTHREAD_H
#define UNPTHREAD_H

/* Our own header for the programs that use threads.
   Include this file, instead of "unp.h". */
#include <pthread.h>
#include "unp.h"
typedef void * (THREAD_FUNC) (void *);

typedef struct
{
    pthread_t thread_tid;
}Thread;

// 每个进程对应一个房间
typedef struct
{
    pid_t child_pid; // 子进程 id
    int child_pipefd; // 与子进程的通信管道 
    int child_status; // 子进程状态, 0 表示空闲, 1 表示正在使用
    int total; // 房间的用户数
}Process;

typedef struct Room // single
{
    int navail; // 可用房间数
    Process *pptr; // 进程数组, 存储所有子进程信息
    pthread_mutex_t lock; // 互斥锁, 用于保护房间状态
    Room (int n)
    {
        navail = n; // 房间数量
        pptr = (Process *)Calloc(n, sizeof(Process)); // 预先分配 n 个进程空间
        lock = PTHREAD_MUTEX_INITIALIZER;
    }
} Room;

void Pthread_create(pthread_t *, const pthread_attr_t *,
                      void * (*)(void *), void *);
void Pthread_join(pthread_t, void **);
void Pthread_detach(pthread_t);
void Pthread_kill(pthread_t, int);

void Pthread_mutexattr_init(pthread_mutexattr_t *);
void Pthread_mutexattr_setpshared(pthread_mutexattr_t *, int);
void Pthread_mutex_init(pthread_mutex_t *, pthread_mutexattr_t *);
void Pthread_mutex_lock(pthread_mutex_t *);
void Pthread_mutex_unlock(pthread_mutex_t *);

void Pthread_cond_broadcast(pthread_cond_t *);
void Pthread_cond_signal(pthread_cond_t *);
void Pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
void Pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *,
                              const struct timespec *);

void Pthread_key_create(pthread_key_t *, void (*)(void *));
void Pthread_setspecific(pthread_key_t, const void *);
void Pthread_once(pthread_once_t *, void (*)(void));

void Pthread_detach(pthread_t);


#endif // UNPTHREAD_H
