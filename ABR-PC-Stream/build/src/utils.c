#define _POSIX_C_SOURCE 200809L
#include "utils.h"

static inline struct timespec ms_to_timespec(double ms) {
    struct timespec ts;
    if (ms < 0) ms = 0;
    ts.tv_sec  = (time_t)(ms / 1000.0);
    ts.tv_nsec = (long)((ms - (ts.tv_sec * 1000.0)) * 1e6);
    return ts;
}

double now_ms_mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1e6);
}

void sleep_until_deadline_ms(double abs_deadline_ms) {
    // TIMER_ABSTIME with CLOCK_MONOTONIC for steady cadence
    struct timespec abst = ms_to_timespec(abs_deadline_ms);
    // If the target time is in the past, return immediately
    double now = now_ms_mono();
    if (now >= abs_deadline_ms) return;

    // Use clock_nanosleep with absolute time to avoid drift
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &abst, NULL);
}

double frame_deadline_ms(double playback_start_ms, int frame_idx, int fps) {
    const double frame_interval_ms = 1000.0 / (double)fps;
    return playback_start_ms + (frame_idx + 1) * frame_interval_ms;
}
