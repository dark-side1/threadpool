#include "threadpool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const int NUMBER = 2;

//����ṹ��
typedef struct Task{
	void (*funciotn)(void* arg);//����ָ��
	void* arg;
}Task;

//�̳߳ؽṹ��
struct ThreadPool {
	//�������
	Task* taskQ;
	int queueCapacity;			//����
	int queueSize;				//��ǰ�������
	int queueFront;				//��ͷ -> ȡԪ��
	int queueRear;				//��β -> ��Ԫ��
	//������
	pthread_t managerID;		//�������߳�ID
	//������
	pthread_t* threadIDs;		//�������߳�ID
	int minNum;					//��С�߳���
	int maxNum;					//����߳���
	int busyNum;				//æ���������߳���
	int liveNum;				//����߳���
	int exitNum;				//Ҫ���ٵ��߳���

	//ȷ���̰߳�ȫ
	pthread_mutex_t mutexPool;	//�������̳߳أ����� ������У�
	pthread_mutex_t mutexBusy;	//��busyNum����

	//���������ߣ�������
	pthread_cond_t notFull;		//��������Ƿ�����
	pthread_cond_t notEmpty;	//��������Ƿ����

	int shutdown;				//�Ƿ�Ҫ�����̳߳أ�����Ϊ1��������Ϊ0



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

		//�������
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		//������
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

		//�̰߳�ȫ
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0) {
			printf("mutex or cond fail...\n");
			break;
		}

		pool->shutdown = 0;

		//�����߳�
		pthread_create(&pool->managerID, NULL, manager, pool);
		for (int i = 0; i < min; ++i) {
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}

		return pool;
	} while (0);

	//����ʧ�ܣ��ͷ���Դ
	if (pool && pool->threadIDs) free(pool->threadIDs);
	if (pool && pool->taskQ)free(pool->taskQ);
	if (pool) free(pool);

	return NULL;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg)
{
	//���������̳߳��������
	pthread_mutex_lock(&pool->mutexPool);
	//�жϵ�ǰ��������Ƿ�Ϊ��
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
	{
		//�����������߳�
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	if (pool->shutdown) {
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//�������
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

	//�ر��̳߳�
	pool->shutdown = 1;
	//�������չ����߽���
	pthread_join(pool->managerID, NULL);
	//���������������߽���
	for (int i = 0; i < pool->liveNum; i++)
	{
		pthread_cond_signal(&pool->notEmpty);
	}
	//�ͷ��ڴ�
	if (pool->taskQ != NULL) {
		free(pool->taskQ);
	}
	if (pool->threadIDs)
	{
		free(pool->threadIDs);
	}

	//�ͷ������������
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
		//������ȡ��������е�������������߳�
		pthread_mutex_lock(&pool->mutexPool);
		//�жϵ�ǰ��������Ƿ�Ϊ��
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			//���������߳�
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			//�ж��Ƿ������߳�
			if (pool->exitNum) {
				//�����߳��Ƿ����ٳɹ��������߳�����Ҫ��һ
				pool->exitNum--;
				if (pool->liveNum > pool->minNum) {
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);
				}
			}
		}
		//�ж��̳߳��Ƿ񱻹ر�
		if (pool->shutdown) {
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}
		//�����������ȡ����
		Task task;
		task.funciotn = pool->taskQ[pool->queueFront].funciotn;
		task.arg = pool->taskQ[pool->queueFront].arg;
		//�ƶ�ͷ�ڵ�
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;
		//����
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->mutexPool);

		//�������߳�ִ������
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

		//ȡ���̳߳�������������͵�ǰ�̵߳�����
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		//ȡ��æ�̵߳�����
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//�����߳�
		//������� > �����̸߳��� && �����̸߳��� < ����߳���
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

		//�����߳�
		//æ�߳�����*2 < �����̸߳��� && �����̸߳��� > ��С�߳���
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
