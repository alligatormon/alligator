#pragma once

#include <stddef.h>

typedef struct qnode {
    struct qnode *next;
    void *data;
} qnode;

typedef struct {
    qnode *head;
    qnode *tail;
    size_t size;
} queue;

queue* queue_init();
void  queue_push(queue *q, void *ptr);
void *queue_front(queue *q);
void *queue_pop(queue *q);
int   queue_empty(queue *q);
void queue_free(queue *q, void (*free_fn)(void *));
