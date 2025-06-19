#include "rwlock.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

typedef struct rwlock {
    pthread_mutex_t mutex;
    pthread_cond_t reader_done;
    pthread_cond_t writer_ready;
    int active_readers;
    int active_writers;
    int waiting_readers;
    int waiting_writers;
    int completed_reads;
    int threshold;
    PRIORITY mode;
} rwlock_t;

rwlock_t *rwlock_new(PRIORITY p, uint32_t limit) {
    rwlock_t *lock = (rwlock_t *)malloc(sizeof(rwlock_t));
    if (!lock) {
        exit(EXIT_FAILURE);
    }
    if (p == N_WAY && limit <= 0) {
        return NULL;
    }

    pthread_mutex_init(&lock->mutex, NULL);
    pthread_cond_init(&lock->reader_done, NULL);
    pthread_cond_init(&lock->writer_ready, NULL);
    lock->active_readers = 0;
    lock->active_writers = 0;
    lock->waiting_readers = 0;
    lock->waiting_writers = 0;
    lock->completed_reads = 0;
    lock->threshold = limit;
    lock->mode = p;

    return lock;
}

void rwlock_delete(rwlock_t **lock_ptr) {
    if (!lock_ptr || !(*lock_ptr)) {
        return;
    }

    rwlock_t *lock = *lock_ptr;
    pthread_mutex_lock(&lock->mutex);
    pthread_mutex_unlock(&lock->mutex);
    pthread_mutex_destroy(&lock->mutex);
    pthread_cond_destroy(&lock->reader_done);
    pthread_cond_destroy(&lock->writer_ready);
    free(lock);
    *lock_ptr = NULL;
}

void reader_lock(rwlock_t *lock) {
    pthread_mutex_lock(&lock->mutex);
    lock->waiting_readers++;

    while (lock->active_writers > 0 || 
           (lock->mode == WRITERS && lock->waiting_writers > 0) || 
           (lock->mode == N_WAY && lock->waiting_writers > 0 && lock->completed_reads >= lock->threshold) ||
           (lock->mode == N_WAY && lock->active_writers > 0 && lock->threshold == 1)) {
        pthread_cond_wait(&lock->reader_done, &lock->mutex);
    }

    lock->waiting_readers--;
    lock->active_readers++;
    lock->completed_reads++;

    pthread_mutex_unlock(&lock->mutex);
}

void reader_unlock(rwlock_t *lock) {
    pthread_mutex_lock(&lock->mutex);
    lock->active_readers--;

    if (lock->mode == N_WAY) {
        if (lock->waiting_writers > 0) {
            if (lock->completed_reads >= lock->threshold && lock->active_readers == 0) {
                pthread_cond_signal(&lock->writer_ready);
            } else if (lock->waiting_readers > 0) {
                pthread_cond_signal(&lock->reader_done);
            } else if (lock->active_readers == 0) {
                pthread_cond_signal(&lock->writer_ready);
            }
        } else {
            pthread_cond_broadcast(&lock->reader_done);
        }
    } else if (lock->mode == WRITERS && lock->waiting_writers == 0) {
        pthread_cond_broadcast(&lock->reader_done);
    } else if (lock->mode == WRITERS && lock->active_readers == 0) {
        pthread_cond_signal(&lock->writer_ready);
    } else if (lock->mode == READERS && lock->waiting_readers > 0) {
        pthread_cond_broadcast(&lock->reader_done);
    } else if (lock->mode == READERS && lock->active_readers == 0) {
        pthread_cond_signal(&lock->writer_ready);
    }

    pthread_mutex_unlock(&lock->mutex);
}

void writer_lock(rwlock_t *lock) {
    pthread_mutex_lock(&lock->mutex);
    lock->waiting_writers++;

    while (lock->active_readers > 0 || lock->active_writers > 0 ||
           (lock->mode == READERS && lock->waiting_readers > 0) ||
           (lock->mode == N_WAY && lock->waiting_readers > 0 && lock->completed_reads < lock->threshold)) {
        pthread_cond_wait(&lock->writer_ready, &lock->mutex);
    }

    lock->waiting_writers--;
    lock->active_writers = 1;
    lock->completed_reads = 0;

    pthread_mutex_unlock(&lock->mutex);
}

void writer_unlock(rwlock_t *lock) {
    pthread_mutex_lock(&lock->mutex);
    lock->active_writers = 0;

    if (lock->mode == N_WAY) {
        if (lock->waiting_writers > 0) {
            if (lock->completed_reads >= lock->threshold && lock->active_readers == 0) {
                pthread_cond_signal(&lock->writer_ready);
            } else if (lock->waiting_readers > 0) {
                pthread_cond_signal(&lock->reader_done);
            } else if (lock->active_readers == 0) {
                pthread_cond_signal(&lock->writer_ready);
            }
        } else {
            pthread_cond_broadcast(&lock->reader_done);
        }
    } else if (lock->mode == WRITERS && lock->waiting_writers == 0) {
        pthread_cond_broadcast(&lock->reader_done);
    } else if (lock->mode == WRITERS && lock->active_readers == 0) {
        pthread_cond_signal(&lock->writer_ready);
    } else if (lock->mode == READERS && lock->waiting_readers > 0) {
        pthread_cond_broadcast(&lock->reader_done);
    } else if (lock->mode == READERS && lock->active_readers == 0) {
        pthread_cond_signal(&lock->writer_ready);
    }

    pthread_mutex_unlock(&lock->mutex);
}


