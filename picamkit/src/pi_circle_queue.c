/*
 * pi_circle_queue.c
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 *
 * used by channel run threads to manage buffers to be output
 *
 */

#include <stdlib.h>
#include <pthread.h>

#include "pi_errno.h"
#include "pi_circle_queue.h"


int pi_circle_queue_create(struct pi_circle_queue *queue)
{
	struct pi_list *list = NULL;
	int i;

	list = (struct pi_list *)calloc(queue->size, sizeof(struct pi_list));
	if (list == NULL)
		return -PI_E_NO_MEMORY;

	for (i = 0; i < queue->size; i++) {
		if (i < queue->size -1)
			list[i].next = &list[i+1];
		else
			list[i].next = &list[0];
	}

	queue->head = &list[0];
	queue->tail = &list[0];
	queue->list = list;
	queue->cur_size = 0;
	pthread_mutex_init(&queue->mutex, NULL);

	return PI_OK;
}

void pi_circle_queue_destroy(struct pi_circle_queue *queue)
{
	pthread_mutex_lock(&queue->mutex);

	free(queue->list);

	queue->head = NULL;
	queue->tail = NULL;
	queue->list = NULL;
	queue->size = 0;
	queue->cur_size = 0;
	pthread_mutex_unlock(&queue->mutex);

	pthread_mutex_destroy(&queue->mutex);

}

static int __queue_check_is_full(struct pi_circle_queue *queue)
{
	return (queue->cur_size == queue->size) ? (1) : (0);
}

static int __queue_check_is_empty(struct pi_circle_queue *queue)
{
	return (queue->cur_size == 0) ? (1) : (0);
}

int pi_circle_queue_push(struct pi_circle_queue *queue, void *priv)
{
	pthread_mutex_lock(&queue->mutex);

	if (__queue_check_is_full(queue)) {
		pthread_mutex_unlock(&queue->mutex);
		return -PI_E_FULL;
	}

	queue->tail->priv = priv;
	queue->cur_size++;
	queue->tail = queue->tail->next;

	pthread_mutex_unlock(&queue->mutex);

	return PI_OK;
}

/* just make this list node empty,
 * so new private data could be writen in queue main loop
 * only called when private's user_count decrease to 0*/
int pi_circle_queue_pop(struct pi_circle_queue *queue, void **priv)
{
	pthread_mutex_lock(&queue->mutex);

	if (__queue_check_is_empty(queue)) {
		pthread_mutex_unlock(&queue->mutex);
		return -PI_E_EMPTY;
	}

	*priv = queue->head->priv;
	queue->head = queue->head->next;
	queue->cur_size--;

	pthread_mutex_unlock(&queue->mutex);

	return PI_OK;
}

/* fake pop just return the list node(queue head) private data */
int pi_circle_queue_fake_pop(struct pi_circle_queue *queue, void **priv)
{
	pthread_mutex_lock(&queue->mutex);

	if (__queue_check_is_empty(queue)) {
		pthread_mutex_unlock(&queue->mutex);
		return -PI_E_EMPTY;
	}

	*priv = queue->head->priv;

	pthread_mutex_unlock(&queue->mutex);

	return PI_OK;
}

/* fake push just return the list node(queue tail) private data */
int pi_circle_queue_fake_push(struct pi_circle_queue *queue, void **priv)
{
	pthread_mutex_lock(&queue->mutex);

	if (__queue_check_is_full(queue)) {
		pthread_mutex_unlock(&queue->mutex);
		return -PI_E_FULL;
	}

	*priv = queue->tail->priv;

	pthread_mutex_unlock(&queue->mutex);

	return PI_OK;
}

void pi_circle_queue_empty(struct pi_circle_queue *queue)
{
	pthread_mutex_lock(&queue->mutex);

	queue->head = queue->list;
	queue->tail = queue->list;
	queue->cur_size = 0;

	pthread_mutex_unlock(&queue->mutex);
}

void pi_circle_queue_full(struct pi_circle_queue *queue)
{
	pthread_mutex_lock(&queue->mutex);

	queue->head = queue->list;
	queue->tail = queue->list;
	queue->cur_size = queue->size;

	pthread_mutex_unlock(&queue->mutex);
}
