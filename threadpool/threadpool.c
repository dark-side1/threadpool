#include "threadpool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const int NUMBER = 2;

//任务结构体
typedef struct Task{
	void (*funciotn)(void* arg);//函数指针
	void* arg;
}Task;

//线程池结构体
struct ThreadPool {
	//任务队列
	Task* taskQ;
	int queueCapacity;			//容量
	int queueSize;				//当前任务个数
	int queueFront;				//队头 -> 取元素
	int queueRear;				//队尾 -> 放元素
	//管理者
	pthread_t managerID;		//管理者线程ID
	//工作者
	pthread_t* threadIDs;		//工作者线程ID
	int minNum;					//最小线程数
	int maxNum;					//最大线程数
	int busyNum;				//忙（工作）线程数
	int liveNum;				//存活线程数
	int exitNum;				//要销毁的线程数

	//确保线程安全
	pthread_mutex_t mutexPool;	//锁整个线程池（操作 任务队列）
	pthread_mutex_t mutexBusy;	//锁busyNum变量

	//阻塞生产者，消费者
	pthread_cond_t notFull;		//任务队列是否满了
	pthread_cond_t notEmpty;	//任务队列是否空了

	int shutdown;				//是否要销毁线程池，销毁为1，不销毁为0



};

ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));

	do
	{
		if (pool == NULL) {
			printf("malloc threadpool fail...\n");
			break;
		}

		//任务队列
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		//工作者
		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->threadIDs == NULL) {
			printf("malloc threadIDs fail...\n");
			break;
		}
		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);
		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;
		pool->exitNum = 0;

		//线程安全
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0) {
			printf("mutex or cond fail...\n");
			break;
		}

		pool->shutdown = 0;

		//创建线程
		pthread_create(&pool->managerID, NULL, manager, pool);
		for (int i = 0; i < min; ++i) {
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}

		return pool;
	} while (0);

	//创建失败，释放资源
	if (pool && pool->threadIDs) free(pool->threadIDs);
	if (pool && pool->taskQ)free(pool->taskQ);
	if (pool) free(pool);

	return NULL;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg)
{
	//上锁，往线程池添加任务
	pthread_mutex_lock(&pool->mutexPool);
	//判断当前任务队列是否为满
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
	{
		//阻塞生产者线程
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	if (pool->shutdown) {
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//添加任务
	pool->taskQ[pool->queueRear].funciotn = func;
	pool->taskQ[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;

	pthread_cond_signal(&pool->notEmpty);
	pthread_mutex_unlock(&pool->mutexPool);
}

int threadPoolDestroy(ThreadPool* pool)
{
	if (pool == NULL) {
		return -1;
	}

	//关闭线程池
	pool->shutdown = 1;
	//阻塞回收管理者进程
	pthread_join(pool->managerID, NULL);
	//唤醒阻塞的消费者进程
	for (int i = 0; i < pool->liveNum; i++)
	{
		pthread_cond_signal(&pool->notEmpty);
	}
	//释放内存
	if (pool->taskQ != NULL) {
		free(pool->taskQ);
	}
	if (pool->threadIDs)
	{
		free(pool->threadIDs);
	}

	//释放锁，互斥变量
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_mutex_destroy(&pool->mutexPool);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);

	free(pool);
	pool = NULL;
	return 0;
}

int threadPoolBusyNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

int threadPoolLiveNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexPool);
	int liveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);
	return liveNum;
}

void* worker(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;

	while (1) {
		//上锁，取任务队列中的任务给工作者线程
		pthread_mutex_lock(&pool->mutexPool);
		//判断当前任务队列是否为空
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			//阻塞工作线程
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			//判断是否销毁线程
			if (pool->exitNum) {
				//不管线程是否销毁成功，销毁线程数都要减一
				pool->exitNum--;
				if (pool->liveNum > pool->minNum) {
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);
				}
			}
		}
		//判断线程池是否被关闭
		if (pool->shutdown) {
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}
		//从任务队列中取任务
		Task task;
		task.funciotn = pool->taskQ[pool->queueFront].funciotn;
		task.arg = pool->taskQ[pool->queueFront].arg;
		//移动头节点
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;
		//解锁
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->mutexPool);

		//工作者线程执行任务
		printf("thread %ld is start working\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		task.funciotn(task.arg); //(*task.funciotn)(task.arg);
		free(task.arg);
		task.arg = NULL;
		printf("thread %ld is end working\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);

	}
	return NULL;
}

void* manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;

	while (!pool->shutdown)
	{
		sleep(3);

		//取出线程池中任务的数量和当前线程的数量
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		//取出忙线程的数量
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//创建线程
		//任务个数 > 存活的线程个数 && 存活的线程个数 < 最大线程数
		if (queueSize > liveNum && liveNum < pool->maxNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			int counter = 0;
			for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; i++)
			{
				if (pool->threadIDs[i] == 0)
				{
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					counter++;
					pool->liveNum++;
				}
			}
			pthread_mutex_unlock(&pool->mutexPool);
		}

		//销毁线程
		//忙线程数量*2 < 存活的线程个数 && 存活的线程个数 > 最小线程数
		if (busyNum * 2 < liveNum && liveNum > pool->minNum) {
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			for (int i = 0; i < NUMBER; i++)
			{
				pthread_cond_signal(&pool->notEmpty);
			}
		}

	}

	return NULL;
}


void threadExit(ThreadPool* pool)
{
	pthread_t tid = pthread_self();
	for (int i = 0; i < pool->maxNum; i++)
	{
		if (pool->threadIDs[i] == tid) {
			pool->threadIDs[i] = 0;
			printf("threadExit() called, %ld exit\n", tid);
			break;
		}
	}
	pthread_exit(NULL);
}
