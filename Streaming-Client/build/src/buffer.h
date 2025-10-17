#ifndef BUFFER_H
#define BUFFER_H

typedef struct {
    int max_frames;
    int count;
} Buffer;

Buffer* buffer_init(int seconds, int fps);
int buffer_add(Buffer* b);
int buffer_consume(Buffer* b);
void buffer_free(Buffer* b);

#endif
