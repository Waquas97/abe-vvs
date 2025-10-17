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
    int seconds = 0;
    if (dur) {
        sscanf((char*)dur, "PT%*dM%dS", &seconds);
        xmlFree(dur);
    }

    // Walk down to Representation node to get frameRate
    xmlNode *rep = NULL;
    for (xmlNode *as = root->children; as; as = as->next) {
        if (as->type == XML_ELEMENT_NODE && strcmp((char*)as->name, "AdaptationSet") == 0) {
            for (xmlNode *r = as->children; r; r = r->next) {
                if (r->type == XML_ELEMENT_NODE && strcmp((char*)r->name, "Representation") == 0) {
                    rep = r;
                    break;
                }
            }
        }
    }
    if (rep) {
        xmlChar *fr = xmlGetProp(rep, (const xmlChar*)"frameRate");
        if (fr) {
            info->frame_rate = atoi((char*)fr);
            xmlFree(fr);
        }
    }

    if (info->frame_rate > 0 && seconds > 0) {
        info->total_frames = info->frame_rate * seconds;
    }

    // Collect FrameURLs
    info->frame_urls = malloc(info->total_frames * sizeof(char*));
    if (!info->frame_urls) {
        free(info);
        xmlFreeDoc(doc);
        return NULL;
    }

    char* base = get_base_url(url);
    int idx = 0;

    if (rep) {
        for (xmlNode *fl = rep->children; fl; fl = fl->next) {
            if (fl->type == XML_ELEMENT_NODE && strcmp((char*)fl->name, "FrameList") == 0) {
                for (xmlNode *fu = fl->children; fu; fu = fu->next) {
                    if (fu->type == XML_ELEMENT_NODE && strcmp((char*)fu->name, "FrameURL") == 0) {
                        xmlChar *m = xmlGetProp(fu, (const xmlChar*)"media");
                        if (m && idx < info->total_frames) {
                            char full[1024];
                            snprintf(full, sizeof(full), "%s%s", base, (char*)m);
                            info->frame_urls[idx++] = strdup(full);
                        }
                        xmlFree(m);
                    }
                }
            }
        }
    }

    free(base);
    xmlFreeDoc(doc);

    if (idx != info->total_frames) {
        fprintf(stderr, "[warn] MPD declared %d frames, but found %d FrameURLs\n",
                info->total_frames, idx);
        info->total_frames = idx; // adjust downwards
    }

    return info;
}

void free_mpd(MPDInfo* info) {
    if (!info) return;
    for (int i = 0; i < info->total_frames; i++) {
        free(info->frame_urls[i]);
    }
    free(info->frame_urls);
    free(info);
}
