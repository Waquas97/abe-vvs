#ifndef MPD_PARSER_H
#define MPD_PARSER_H

typedef struct {
    int frame_rate;
    int total_frames;
    char **frame_urls;
} MPDInfo;

MPDInfo* parse_mpd(const char* url);
void free_mpd(MPDInfo* info);

#endif
