#include <stdlib.h>
#include <stdio.h>
#include "buffer.h"

Buffer* buffer_init(int seconds, int fps) {
    Buffer* b = calloc(1, sizeof(Buffer));
    b->max_frames = seconds * fps;
    b->count = 0;
    return b;
}

int buffer_add(Buffer* b) {
    if (!b) {
        fprintf(stderr, "[error] buffer_add: Buffer pointer is NULL\n");
        return -2;
    }
    if (b->count >= b->max_frames) return -1;
    b->count++;
    return 0;
}

int buffer_consume(Buffer* b) {
    if (!b) {
        fprintf(stderr, "[error] buffer_consume: Buffer pointer is NULL\n");
        return -2;
    }
    if (b->count <= 0) return -1;
    b->count--;
    return 0;
}

void buffer_free(Buffer* b) {
    free(b);
}
