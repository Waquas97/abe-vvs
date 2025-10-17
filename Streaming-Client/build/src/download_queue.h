#ifndef DOWNLOAD_QUEUE_H
#define DOWNLOAD_QUEUE_H

#include <pthread.h>

// Change Frame to your actual frame struct if needed
typedef struct {
    int index; // frame index
    GByteArray* buffer; // downloaded frame data in memory
    double dl_ms; // download time in ms
} Frame;

typedef struct {
    Frame** items;
    int capacity;
    int count;
    int head;
    int tail;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} DownloadQueue;

DownloadQueue* download_queue_init(int capacity);
void download_queue_free(DownloadQueue* q);
int download_queue_push(DownloadQueue* q, Frame* frame); // blocks if full
Frame* download_queue_pop(DownloadQueue* q); // blocks if empty

#endif // DOWNLOAD_QUEUE_H
