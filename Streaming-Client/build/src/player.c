#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include "player.h"
#include "utils.h"
#include "logger.h"

void* simulate_player(void* arg) {
    struct PlayerArgs* pa = (struct PlayerArgs*)arg;
    if (!pa) {
        fprintf(stderr, "[error] simulate_player: PlayerArgs pointer is NULL\n");
        return NULL;
    }
    Buffer* b     = pa->buffer;
    int fps       = pa->fps;
    Logger* log   = pa->logger;
    int total     = pa->total_frames;
    if (!b) {
        fprintf(stderr, "[error] simulate_player: Buffer pointer is NULL\n");
        return NULL;
    }
    if (!log) {
        fprintf(stderr, "[error] simulate_player: Logger pointer is NULL\n");
        return NULL;
    }

    int played_frames = 0;
    double stall_start = 0.0;
    int in_stall = 0;

    // Initial buffer fill threshold = max_frames
    int threshold = b->max_frames;
    while (b->count < threshold) {
        struct timespec ts = {0, 1000000}; // 1 ms
        nanosleep(&ts, NULL);
        logger_add_player_event(log, "waiting_for_initial_buffer",0, b->count);
    }

    const double start_ms = now_ms_mono();
    logger_add_player_event(log, "playback_start",0, b->count);

    // Playback loop
    while (played_frames < total) {
        if (b->count == 0) {
            // Stall
            if (!in_stall) {
                stall_start = now_ms_mono();
                in_stall = 1;
                logger_add_player_event(log, "stall_start", 0, b->count);
            }
            struct timespec ts = {0, 1000000}; // 1 ms
            nanosleep(&ts, NULL);
            continue;
        }

        // Recover from stall
        if (in_stall) {
            double dur = now_ms_mono() - stall_start;
            logger_add_stall(log, stall_start, dur);
            in_stall = 0;
            logger_add_player_event(log, "stall_end", 0, b->count);
        }

        // Consume a frame
        int consume_rc = buffer_consume(b);
        if (consume_rc == 0) {
            double abs_deadline = frame_deadline_ms(start_ms, played_frames, fps);
            played_frames++;

            // Log player-specific event
            logger_add_player_event(log, "consume_frame", played_frames, b->count);
            //fprintf(stderr, "[pl] consumed frame %d, buffer_count=%d\n", played_frames, b->count);

            // Keep steady playback pacing
            sleep_until_deadline_ms(abs_deadline);
        } else {
            if (consume_rc == -2) {
                fprintf(stderr, "[error] simulate_player: buffer_consume returned -2 (NULL buffer) at frame %d\n", played_frames);
            }
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
            logger_add_player_event(log, "consume_failed", played_frames, b->count);
        }
    }

    logger_add_player_event(log, "playback_end", 0, b->count);
    return NULL;
}
