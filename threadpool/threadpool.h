#pragma once

typedef struct ThreadPool ThreadPool;


/**
 * �����̳߳ز���ʼ��.
 * \param min			��С�߳���
 * \param max			����߳���
 * \param queueSize		������г���
 */
ThreadPool* threadPoolCreate(int min, int max, int queueSize);

/**
 * ���̳߳��������.
 */
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

/**
 * �����̳߳�.
 */
int threadPoolDestroy(ThreadPool* pool);

/**
 * ��ȡ�̳߳���æ���̸߳���.
 */
int threadPoolBusyNum(ThreadPool* pool);

/**
 * ��ȡ�̳߳��д���̸߳���.
 */
int threadPoolLiveNum(ThreadPool* pool);

///////////////////////////////////////////////////////////

/**
 * ������/�����ߵ�����������е�����.
 */
void* worker(void* arg);

/**
 * �����߹��������߳�.
 */
void* manager(void* arg);

/**
 * �߳��˳���ʱ���߳�id��Ϊ0.
 */
void threadExit(ThreadPool* pool);




