#ifndef ABR_H
#define ABR_H

#include "mpd_parser.h"

typedef struct ABR ABR;

// Initialize ABR with MPD info, start at lowest representation (index 0)
ABR* abr_init(MPDInfo* mpd, double threshold, int check_interval_frames);

// Select representation index for a given frame index and current buffer occupancy
int abr_select_for_frame(ABR* a, int frame_index, int buffer_count);

// Update estimator with observed bytes downloaded and total time (download+decrypt) in ms
void abr_update_stats(ABR* a, size_t bytes, double total_ms);

void abr_free(ABR* a);

#endif
