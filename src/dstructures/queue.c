#include "dstructures/queue.h"
#include <stdlib.h>

queue* queue_init()
{
	queue *q = calloc(1, sizeof(*q));
    q->head = q->tail = NULL;
    q->size = 0;

	return q;
}

void queue_push(queue *q, void *ptr)
{
    qnode *n = malloc(sizeof(*n));
    n->data = ptr;
    n->next = NULL;

    if (!q->tail)
        q->head = q->tail = n;
    else {
        q->tail->next = n;
        q->tail = n;
    }

    q->size++;
}

void *queue_front(queue *q)
{
    return q->head ? q->head->data : NULL;
}

void *queue_pop(queue *q)
{
    if (!q->head)
        return NULL;

    qnode *n = q->head;
    void *ptr = n->data;

    q->head = n->next;
    if (!q->head)
        q->tail = NULL;

    free(n);
    q->size--;
    return ptr;
}

int queue_empty(queue *q)
{
    return q->head == NULL;
}
