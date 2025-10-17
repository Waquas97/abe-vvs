#define _POSIX_C_SOURCE 200809L
#include <glib.h>
#include "download_queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

DownloadQueue* download_queue_init(int capacity) {
    DownloadQueue* q = malloc(sizeof(DownloadQueue));
    if (!q) return NULL;
    q->items = malloc(sizeof(Frame*) * capacity);
    if (!q->items) { free(q); return NULL; }
    q->capacity = capacity;
    q->count = 0;
    q->head = 0;
    q->tail = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return q;
}

void download_queue_free(DownloadQueue* q) {
    if (!q) return;
    free(q->items);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    free(q);
}

int download_queue_push(DownloadQueue* q, Frame* frame) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pthread_mutex_lock(&q->mutex);
    while (q->count == q->capacity) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double wait_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6;
    // if (wait_ms > 1.0) {
    //     printf("[queue push] waited %.2f ms for space\n", wait_ms);
    // }
    q->items[q->tail] = frame;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

Frame* download_queue_pop(DownloadQueue* q) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double wait_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6;
    // if (wait_ms > 1.0) {
    //     printf("[queue pop] waited %.2f ms for item\n\n\n\n", wait_ms);
    // }
    Frame* frame = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return frame;
}
