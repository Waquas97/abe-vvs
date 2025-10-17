#ifndef UTILS_H
#define UTILS_H

#include <time.h>

// Monotonic ms since an arbitrary start (immune to NTP/timezone changes).
double now_ms_mono(void);

// Sleep until an absolute monotonic deadline (in ms).
void sleep_until_deadline_ms(double abs_deadline_ms);

// Compute absolute deadline for frame index at fps, given a playback start time (ms).
double frame_deadline_ms(double playback_start_ms, int frame_idx, int fps);

#endif
