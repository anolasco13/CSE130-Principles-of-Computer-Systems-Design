#include "queue.h"
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>

struct queue {
    int capacity;
    int head;
    int tail;
    void **buffer;
    sem_t available_slots;
    sem_t occupied_slots;
    sem_t access_lock;
};

queue_t *queue_new(int capacity) {
    queue_t *q = (queue_t *) malloc(sizeof(queue_t));
    if (q) {
        q->capacity = capacity;
        q->head = 0;
        q->tail = 0;
        q->buffer = (void **) malloc(capacity * sizeof(void *));
        if (!q->buffer) {
            free(q);
            return NULL;
        }
        int status = 0;
        status = sem_init(&(q->available_slots), 0, capacity);
        assert(status == 0);
        status = sem_init(&(q->occupied_slots), 0, 0);
        assert(status == 0);
        status = sem_init(&(q->access_lock), 0, 1);
        assert(status == 0);
    }
    return q;
}

void queue_delete(queue_t **q) {
    if (q && *q) {
        if ((*q)->buffer) {
            int status = 0;
            status = sem_destroy(&(*q)->available_slots);
            assert(status == 0);
            status = sem_destroy(&(*q)->occupied_slots);
            assert(status == 0);
            status = sem_destroy(&(*q)->access_lock);
            assert(status == 0);
            free((*q)->buffer);
            (*q)->buffer = NULL;
        }
        free(*q);
        *q = NULL;
    }
}

bool queue_push(queue_t *q, void *elem) {
    if (!q) {
        return false;
    }
    sem_wait(&(q->available_slots));
    sem_wait(&(q->access_lock));
    q->buffer[q->head] = elem;
    q->head = (q->head + 1) % q->capacity;
    sem_post(&(q->access_lock));
    sem_post(&(q->occupied_slots));
    return true;
}

bool queue_pop(queue_t *q, void **elem) {
    if (!q) {
        return false;
    }
    sem_wait(&(q->occupied_slots));
    sem_wait(&(q->access_lock));
    *elem = q->buffer[q->tail];
    q->tail = (q->tail + 1) % q->capacity;
    sem_post(&(q->access_lock));
    sem_post(&(q->available_slots));
    return true;
}

