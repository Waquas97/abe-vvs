#ifndef LOGGER_H
#define LOGGER_H

typedef struct {
    int frame_no;
    double download_ms;
    double decrypt_ms;
    int buffer_count;
    double timestamp_ms;   // NEW: absolute timestamp when logged
} FrameLog;

typedef struct {
    double start_ms;
    double duration_ms;
} StallLog;

typedef struct {
    int frame_capacity;
    int stall_capacity;

    FrameLog* frame_logs;
    int frame_size;

    StallLog* stall_logs;
    int stall_size;

    // --- Player events ---
    char** player_events;
    int player_event_count;
    int player_event_cap;
} Logger;

Logger* logger_init(int frame_cap, int stall_cap);

void logger_add_frame(Logger* l, int f, double d, double dec, int buf);
void logger_add_stall(Logger* l, double start, double dur);

// --- Player events ---
void logger_add_player_event(Logger* l, const char* event, int frame, int buf_count);

// Flush logs to disk
void logger_flush(Logger* l, const char* filename);
void logger_flush_player(Logger* l, const char* filename);

void logger_free(Logger* l);

#endif
