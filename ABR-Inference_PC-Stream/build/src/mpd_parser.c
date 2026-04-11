#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <curl/curl.h>
#include "mpd_parser.h"

// Helper for curl memory buffer
struct mem_buf {
    char *data;
    size_t size;
};

static size_t curl_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct mem_buf *mb = (struct mem_buf*)userdata;
    size_t add = size * nmemb;
    char *newp = realloc(mb->data, mb->size + add + 1);
    if (!newp) return 0; // OOM
    mb->data = newp;
    memcpy(mb->data + mb->size, ptr, add);
    mb->size += add;
    mb->data[mb->size] = '\0';
    return add;
}

// Fetch MPD into memory via curl
static int fetch_mpd(const char* url, char **out_data, size_t *out_size) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    struct mem_buf mb = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // ignore cert
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mb);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(mb.data);
        return -1;
    }

    *out_data = mb.data;
    *out_size = mb.size;
    return 0;
}

// Helper: extract base directory of MPD URL (keep trailing slash)
static char* get_base_url(const char* mpd_url) {
    const char* slash = strrchr(mpd_url, '/');
    if (!slash) {
        return strdup("./");
    }
    size_t len = slash - mpd_url + 1;
    char* base = malloc(len + 1);
    if (!base) return NULL;
    strncpy(base, mpd_url, len);
    base[len] = '\0';
    return base;
}

// Parse MPD into MPDInfo struct
MPDInfo* parse_mpd(const char* url) {
    char *mpd_data = NULL;
    size_t mpd_size = 0;
    if (fetch_mpd(url, &mpd_data, &mpd_size) != 0) {
        fprintf(stderr, "[error] Failed to fetch MPD: %s\n", url);
        return NULL;
    }

    xmlDoc *doc = xmlReadMemory(mpd_data, mpd_size, url, NULL, 0);
    free(mpd_data);
    if (!doc) {
        fprintf(stderr, "[error] Failed to parse MPD XML: %s\n", url);
        return NULL;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        return NULL;
    }

    MPDInfo *info = calloc(1, sizeof(MPDInfo));
    if (!info) {
        xmlFreeDoc(doc);
        return NULL;
    }

    // Extract duration + compute total_frames from frameRate
    xmlChar *dur = xmlGetProp(root, (const xmlChar*)"mediaPresentationDuration");
    int minutes = 0, seconds = 0, total_seconds = 0;
    if (dur) {
        // Handles PT1M00S, PT0M30S, PT90S, etc.
        if (strstr((char*)dur, "M")) {
            sscanf((char*)dur, "PT%dM%dS", &minutes, &seconds);
            total_seconds = minutes * 60 + seconds;
        } else {
            sscanf((char*)dur, "PT%dS", &seconds);
            total_seconds = seconds;
        }
        xmlFree(dur);
    }

    // Walk down AdaptationSet -> Representation nodes to get frameRate and bitrates
    // Collect all Representation nodes
    xmlNode *adapt = NULL;
    for (xmlNode *as = root->children; as; as = as->next) {
        if (as->type == XML_ELEMENT_NODE && strcmp((char*)as->name, "AdaptationSet") == 0) {
            adapt = as;
            break;
        }
    }

    // First pass: find frameRate (from any representation) and count representations
    int rep_count = 0;
    if (adapt) {
        for (xmlNode *r = adapt->children; r; r = r->next) {
            if (r->type == XML_ELEMENT_NODE && strcmp((char*)r->name, "Representation") == 0) {
                rep_count++;
                if (info->frame_rate == 0) {
                    xmlChar *fr = xmlGetProp(r, (const xmlChar*)"frameRate");
                    if (fr) {
                        info->frame_rate = atoi((char*)fr);
                        xmlFree(fr);
                    }
                }
            }
        }
    }

    info->n_reps = (rep_count > 0) ? rep_count : 1;
    info->bitrates = calloc(info->n_reps, sizeof(int));
    info->frame_urls = calloc(info->n_reps, sizeof(char**));

    if (info->frame_rate > 0 && total_seconds > 0) {
        info->total_frames = info->frame_rate * total_seconds;
    }

    // Collect FrameURLs per representation
    char* base = get_base_url(url);
    int found = 0;

    if (adapt) {
        int rep_idx = 0;
        for (xmlNode *r = adapt->children; r; r = r->next) {
            if (r->type == XML_ELEMENT_NODE && strcmp((char*)r->name, "Representation") == 0) {
                // bitrate/bandwidth
                xmlChar *bw = xmlGetProp(r, (const xmlChar*)"bandwidth");
                if (bw) {
                    info->bitrates[rep_idx] = atoi((char*)bw);
                    xmlFree(bw);
                } else {
                    info->bitrates[rep_idx] = 0;
                }

                // allocate array for frames
                info->frame_urls[rep_idx] = calloc(info->total_frames, sizeof(char*));
                int idx = 0;
                for (xmlNode *fl = r->children; fl; fl = fl->next) {
                    if (fl->type == XML_ELEMENT_NODE && strcmp((char*)fl->name, "FrameList") == 0) {
                        for (xmlNode *fu = fl->children; fu; fu = fu->next) {
                            if (fu->type == XML_ELEMENT_NODE && strcmp((char*)fu->name, "FrameURL") == 0) {
                                xmlChar *m = xmlGetProp(fu, (const xmlChar*)"media");
                                if (m && idx < info->total_frames) {
                                    char full[1024];
                                    snprintf(full, sizeof(full), "%s%s", base, (char*)m);
                                    info->frame_urls[rep_idx][idx++] = strdup(full);
                                }
                                xmlFree(m);
                            }
                        }
                    }
                }
                if (idx > 0) found += idx;
                rep_idx++;
            }
        }
    }

    free(base);
    xmlFreeDoc(doc);

    if (found == 0) {
        // fallback: no FrameURLs found
        fprintf(stderr, "[warn] MPD: no FrameURLs found in any Representation\n");
        info->total_frames = 0;
    } else if (found / info->n_reps != info->total_frames) {
        // Inconsistent counts: adjust total_frames to min found per-representation
        int min_frames = info->total_frames;
        for (int r = 0; r < info->n_reps; r++) {
            int cnt = 0;
            if (info->frame_urls[r]) {
                for (int f = 0; f < info->total_frames; f++) if (info->frame_urls[r][f]) cnt++;
            }
            if (cnt < min_frames) min_frames = cnt;
        }
        fprintf(stderr, "[warn] MPD declared %d frames, adjusting to %d frames found per representation\n",
                info->total_frames, min_frames);
        info->total_frames = min_frames;
    }
    printf("[info] MPD parsed: frame_rate=%d, total_frames=%d, n_reps=%d\n",
           info->frame_rate, info->total_frames, info->n_reps);
    return info;
}

void free_mpd(MPDInfo* info) {
    if (!info) return;
    if (info->frame_urls) {
        for (int r = 0; r < info->n_reps; r++) {
            if (!info->frame_urls[r]) continue;
            for (int i = 0; i < info->total_frames; i++) {
                if (info->frame_urls[r][i]) free(info->frame_urls[r][i]);
            }
            free(info->frame_urls[r]);
        }
        free(info->frame_urls);
    }
    if (info->bitrates) free(info->bitrates);
    free(info);
}
