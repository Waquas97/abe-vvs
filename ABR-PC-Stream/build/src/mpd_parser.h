#ifndef MPD_PARSER_H
#define MPD_PARSER_H

typedef struct {
    int frame_rate;
    int total_frames;
    int n_reps;         // number of representations/qualities
    int *bitrates;      // bitrate (bits per second) for each representation
    char ***frame_urls; // frame_urls[rep][frame_index]
} MPDInfo;

MPDInfo* parse_mpd(const char* url);
void free_mpd(MPDInfo* info);

#endif
