/*
 * pi_circle_queue.h
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 *
 */

#ifndef _PI_CIRCLE_QUEUE_H
#define _PI_CIRCLE_QUEUE_H

struct pi_list {
	struct pi_list *next;
	void *priv;
};

struct pi_circle_queue {
	struct pi_list *head;
	struct pi_list *tail;
	struct pi_list *list; /*alway point to the origin 1st list node*/
	pthread_mutex_t mutex;
	int size;
	int cur_size;
	int run;
};


int pi_circle_queue_create(struct pi_circle_queue *queue);
void pi_circle_queue_destroy(struct pi_circle_queue *queue);
void pi_circle_queue_empty(struct pi_circle_queue *queue);
void pi_circle_queue_full(struct pi_circle_queue *queue);
int pi_circle_queue_fake_pop(struct pi_circle_queue *queue, void **priv);
int pi_circle_queue_fake_push(struct pi_circle_queue *queue, void **priv);
int pi_circle_queue_pop(struct pi_circle_queue *queue);
int pi_circle_queue_push(struct pi_circle_queue *queue, void *priv);

#endif
