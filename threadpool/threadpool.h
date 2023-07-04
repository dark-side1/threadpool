#pragma once

typedef struct ThreadPool ThreadPool;


/**
 * 创建线程池并初始化.
 * \param min			最小线程数
 * \param max			最大线程数
 * \param queueSize		任务队列长度
 */
ThreadPool* threadPoolCreate(int min, int max, int queueSize);

/**
 * 往线程池添加任务.
 */
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

/**
 * 销毁线程池.
 */
int threadPoolDestroy(ThreadPool* pool);

/**
 * 获取线程池中忙的线程个数.
 */
int threadPoolBusyNum(ThreadPool* pool);

/**
 * 获取线程池中存活线程个数.
 */
int threadPoolLiveNum(ThreadPool* pool);

///////////////////////////////////////////////////////////

/**
 * 工作者/消费者调用任务队列中的任务.
 */
void* worker(void* arg);

/**
 * 管理者管理工作者线程.
 */
void* manager(void* arg);

/**
 * 线程退出的时候将线程id置为0.
 */
void threadExit(ThreadPool* pool);




