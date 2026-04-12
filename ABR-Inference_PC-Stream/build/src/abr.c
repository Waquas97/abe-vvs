#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "abr.h"

struct ABR {
    MPDInfo* mpd;
    double threshold;
    int check_interval;
    int current_rep;
    // circular buffer of recent samples
    size_t *sizes;
    double *times_ms;
    int cap;
    int pos;
    int filled;
};

ABR* abr_init(MPDInfo* mpd, double threshold, int check_interval_frames) {
    ABR* a = calloc(1, sizeof(ABR));
    a->mpd = mpd;
    a->threshold = threshold;
    a->check_interval = check_interval_frames;
    a->current_rep = 0; // start at lowest
    a->cap = check_interval_frames * 2 + 10;
    a->sizes = calloc(a->cap, sizeof(size_t));
    a->times_ms = calloc(a->cap, sizeof(double));
    a->pos = 0;
    a->filled = 0;
    return a;
}

void abr_update_stats(ABR* a, size_t bytes, double total_ms) {
    if (!a) return;
    a->sizes[a->pos] = bytes;
    a->times_ms[a->pos] = total_ms;
    a->pos = (a->pos + 1) % a->cap;
    if (a->filled < a->cap) a->filled++;
}

// compute average bandwidth (bytes/ms) over last N samples (or available)
static double compute_avg_bandwidth(ABR* a, int samples) {
    if (!a || a->filled == 0) return 0.0;
    int to_take = samples;
    if (to_take > a->filled) to_take = a->filled;
    double total_bytes = 0.0;
    double total_ms = 0.0;
    int idx = a->pos - 1;
    if (idx < 0) idx += a->cap;
    for (int i = 0; i < to_take; i++) {
        total_bytes += (double)a->sizes[idx];
        total_ms += a->times_ms[idx];
        idx--;
        if (idx < 0) idx += a->cap;
    }
    if (total_ms <= 0.0) return 0.0;
    // bytes per ms -> bytes / ms * 1000 = bytes/sec
    // convert to bits/sec for comparison with MPD bandwidth (which is in bits/sec)
    return (total_bytes / total_ms) * 1000.0 * 8.0;
}

int abr_select_for_frame(ABR* a, int frame_index, int buffer_count) {
    if (!a || !a->mpd) return 0;
    // only re-evaluate every check_interval frames
    if (a->check_interval <= 0) return a->current_rep;
    if ((frame_index % a->check_interval) != 0) return a->current_rep;

    // if not enough samples yet, stay at current_rep
    if (a->filled < a->check_interval) return a->current_rep;

    double avg_bps = compute_avg_bandwidth(a, a->check_interval);
    if (avg_bps <= 0.0) return a->current_rep;

    int cur = a->current_rep;
    int n = a->mpd->n_reps;
    int cur_bitrate = (cur >=0 && cur < n) ? a->mpd->bitrates[cur] : 0;

    // Debug: print ABR decision variables
    // fprintf(stderr, "[abr] frame=%d samples=%d avg_bps=%.0f cur_rep=%d cur_bitrate=%d threshold=%.2f\n",
    //     frame_index, a->filled, avg_bps, cur, cur_bitrate, a->threshold);

    // attempt to go up if avg_bps > threshold * current_bitrate
    if (cur < n-1 && avg_bps > a->threshold * (double)cur_bitrate) {
        a->current_rep = cur + 1;
        return a->current_rep;
    }
    // go down if avg_bps < current_bitrate (no threshold)
    if (cur > 0 && avg_bps < (double)cur_bitrate) {
        a->current_rep = cur - 1;
        return a->current_rep;
    }
    return a->current_rep;
}

void abr_free(ABR* a) {
    if (!a) return;
    free(a->sizes);
    free(a->times_ms);
    free(a);
}
