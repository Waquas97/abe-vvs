#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logger.h"
#include "utils.h"   // for now_ms_mono()

Logger* logger_init(int frame_cap, int stall_cap) {
    Logger* l = calloc(1, sizeof(Logger));
    l->frame_capacity = frame_cap;
    l->stall_capacity = stall_cap;
    l->frame_logs = calloc(frame_cap, sizeof(FrameLog));
    l->stall_logs = calloc(stall_cap, sizeof(StallLog));
    l->player_event_cap = 128;
    l->player_events = calloc(l->player_event_cap, sizeof(char*));
    return l;
}

void logger_add_frame(Logger* l, int f, double d, double dec, int buf) {
    if (l->frame_size >= l->frame_capacity) return;
    FrameLog* fl = &l->frame_logs[l->frame_size++];
    fl->frame_no = f;
    fl->download_ms = d;
    fl->decrypt_ms = dec;
    fl->buffer_count = buf;
    fl->timestamp_ms = now_ms_mono();
}

void logger_add_stall(Logger* l, double start, double dur) {
    if (l->stall_size >= l->stall_capacity) return;
    StallLog* sl = &l->stall_logs[l->stall_size++];
    sl->start_ms = start;
    sl->duration_ms = dur;
}

void logger_add_player_event(Logger* l, const char* event, int frame, int buf_count) {
    if (!l) return;
    if (l->player_event_count >= l->player_event_cap) {
        l->player_event_cap *= 2;
        l->player_events = realloc(l->player_events,
                                   l->player_event_cap * sizeof(char*));
    }
    double ts = now_ms_mono();
    char buf[256];
    snprintf(buf, sizeof(buf), "%.3f,%s,%d,%d", ts, event, frame, buf_count);
    l->player_events[l->player_event_count++] = strdup(buf);
}

void logger_flush(Logger* l, const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) return;

    fprintf(fp, "# Frame Logs\n");
    fprintf(fp, "frame,download_ms,decrypt_ms,buffer_count,timestamp_ms\n");
    for (int i=0; i<l->frame_size; i++) {
        FrameLog* fl = &l->frame_logs[i];
        fprintf(fp, "%d,%.2f,%.2f,%d,%.3f\n", fl->frame_no,
                fl->download_ms, fl->decrypt_ms,
                fl->buffer_count, fl->timestamp_ms);
    }

    fprintf(fp, "\n# Stall Logs\n");
    fprintf(fp, "stall_start_ms,duration_ms\n");
    for (int i=0; i<l->stall_size; i++) {
        StallLog* sl = &l->stall_logs[i];
        fprintf(fp, "%.2f,%.2f\n", sl->start_ms, sl->duration_ms);
    }

    fclose(fp);
}

void logger_flush_player(Logger* l, const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) return;

    fprintf(fp, "timestamp_ms,event,current_frame,buffer_count\n");
    for (int i=0; i<l->player_event_count; i++) {
        fprintf(fp, "%s\n", l->player_events[i]);
        free(l->player_events[i]);
    }
    free(l->player_events);
    l->player_events = NULL;
    l->player_event_count = 0;
    l->player_event_cap = 0;
    fclose(fp);
}

void logger_free(Logger* l) {
    if (!l) return;
    free(l->frame_logs);
    free(l->stall_logs);
    if (l->player_events) {
        for (int i=0; i<l->player_event_count; i++)
            free(l->player_events[i]);
        free(l->player_events);
    }
    free(l);
}
