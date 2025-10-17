#ifndef PLAYER_H
#define PLAYER_H

#include "buffer.h"
#include "logger.h"

struct PlayerArgs {
    Buffer* buffer;
    int fps;
    Logger* logger;
    int total_frames;
};

// Thread entrypoint
void* simulate_player(void* arg);

#endif


