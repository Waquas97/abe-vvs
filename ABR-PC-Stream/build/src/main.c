#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <glib.h>
#include "mpd_parser.h"
#include "downloader.h"
#include "decryptor.h"
#include "buffer.h"
#include "player.h"
#include <pthread.h>
#include "logger.h"
#include "download_queue.h"
#include "abr.h"

static void ensure_dir(const char* path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s --url <mpd_path_or_url> --buffer <seconds>"
        " [--decrypt --pub <pub_key> --priv <priv_key> --pattern <pattern>]\n"
        " [--abr] [--abr-threshold <value>] [--abr-interval <seconds>]\n"
        " [--write-output]\n"
        "Notes:\n"
        "  • MPD is parsed before timing begins (excluded from measurements).\n"
        "  • Frames are saved to ./stream-download and logs to ./logs.\n"
        "  • If --decrypt is omitted, frames enter buffer immediately after download.\n"
        "  • --write-output is optional; saves frames to disk if specified.\n"
        "  [--download-queue <size>]  (max frames in download queue for pipelined mode)\n"
        "  [--abr]                    (enable simple ABR algorithm, default is off)\n"
        "  [--abr-threshold <value>]  (set ABR quality threshold, default is 1.2)\n"
        "  [--abr-interval <seconds>] (set ABR check interval, default is 24, (NOTE:need to fix this to be equal to FPS))\n",
        prog);
}


// --- Thread argument structs ---
typedef struct {
    MPDInfo* mpd;
    DownloadQueue* queue;
    struct ABR* abr;
} DownloaderArgs;

typedef struct {
    DownloadQueue* queue;
    Buffer* buffer;
    Logger* logger;
    int frame_rate;
    const char* pub_key;
    const char* priv_key;
    const char* pattern;
    int total_frames;
} DecryptorArgs;

// --- Downloader thread function ---
void* downloader_thread_func(void* arg) {
    DownloaderArgs* dargs = (DownloaderArgs*)arg;
    for (int i = 0; i < dargs->mpd->total_frames; i++) {
        Frame* frame = malloc(sizeof(Frame));
        frame->index = i;
        frame->dl_ms = 0.0;
        int rep = 0;
        if (dargs->abr) {
            rep = abr_select_for_frame(dargs->abr, i, 0);
        }
        const char* frame_url = dargs->mpd->frame_urls[rep][i];
        frame->buffer = NULL;
        frame->rep = rep;
        frame->size_bytes = 0;
        int rc = download_file_mem(frame_url, &frame->buffer, &frame->dl_ms);
        if (rc != 0 || !frame->buffer) {
            fprintf(stderr, "[warn] download failed (rc=%d) for %s\n", rc, frame_url);
        }
        if (frame->buffer) frame->size_bytes = frame->buffer->len;
        download_queue_push(dargs->queue, frame);
    }
    return NULL;
}


int main(int argc, char* argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    const char* mpd_url = NULL;
    int buffer_sec = -1;

    int decrypt_enabled = 0;
    int write_output = 0;
    const char *pub_key = NULL, *priv_key = NULL, *pattern = NULL;
    int download_queue_size = 1; // Default: 1 (no pipelining)
    int abr_enabled = 1;
    double abr_threshold = 1.2;
    int abr_check_interval = 24;


    // --- Parse CLI args ---
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--url") && i + 1 < argc) {
            mpd_url = argv[++i];
        } else if (!strcmp(argv[i], "--buffer") && i + 1 < argc) {
            buffer_sec = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--decrypt")) {
            decrypt_enabled = 1;
        } else if (!strcmp(argv[i], "--pub") && i + 1 < argc) {
            pub_key = argv[++i];
        } else if (!strcmp(argv[i], "--priv") && i + 1 < argc) {
            priv_key = argv[++i];
        } else if (!strcmp(argv[i], "--pattern") && i + 1 < argc) {
            pattern = argv[++i];
        } else if (!strcmp(argv[i], "--download-queue") && i + 1 < argc) {
            download_queue_size = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--abr")) {
            abr_enabled = 1;
        } else if (!strcmp(argv[i], "--abr-threshold") && i + 1 < argc) {
            abr_threshold = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--abr-interval") && i + 1 < argc) {
            abr_check_interval = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--write-output")) {
            write_output = 1;
        } else {
            // Unknown or malformed
        }
    }

    if (!mpd_url || buffer_sec < 0) {
        usage(argv[0]);
        return 1;
    }
    if (decrypt_enabled && (!pub_key || !priv_key || !pattern)) {
        fprintf(stderr, "[error] --decrypt requires --pub, --priv, and --pattern.\n");
        return 1;
    }
    
    // --- Parse MPD (excluded from timing) ---
    MPDInfo* mpd = parse_mpd(mpd_url);
    if (!mpd) {
        fprintf(stderr, "[error] Failed to parse MPD: %s\n", mpd_url);
        return 1;
    }
    if (mpd->frame_rate <= 0 || mpd->total_frames <= 0) {
        fprintf(stderr, "[error] MPD missing/invalid frameRate or duration-derived total_frames.\n");
        free_mpd(mpd);
        return 1;
    }
    
    // --- Prepare output dirs ---
    ensure_dir("stream-download");
    ensure_dir("logs");

    // --- Initialize modules ---
    Buffer* buffer = buffer_init(buffer_sec, mpd->frame_rate);
    if (!buffer) {
        fprintf(stderr, "[error] buffer_init failed.\n");
        free_mpd(mpd);
        return 1;
    }
    

    Logger* logger = logger_init(mpd->total_frames, /*stall_cap*/ 10000);
    if (!logger) {
        fprintf(stderr, "[error] logger_init failed.\n");
        buffer_free(buffer);
        free_mpd(mpd);
        return 1;
    }
    if (decryptor_init(pub_key, priv_key, pattern, decrypt_enabled) != 0) {
        fprintf(stderr, "[error] decryptor_init failed.\n");
        logger_free(logger);
        buffer_free(buffer);
        free_mpd(mpd);
        return 2;
    }

    // initialize Virtual Player thread 
    pthread_t player_thread;
    struct PlayerArgs args = { buffer, mpd->frame_rate, logger, mpd->total_frames };
    pthread_create(&player_thread, NULL, simulate_player, &args);

    // Initialize ABR
    ABR* abr = NULL;
    if (abr_enabled) {
        abr = abr_init(mpd, abr_threshold, abr_check_interval);
    }

    // --- Pipelined Download/Decrypt ---
    if (decrypt_enabled) {
        DownloadQueue* queue = download_queue_init(download_queue_size);
        if (!queue) {
            fprintf(stderr, "[error] download_queue_init failed.\n");
            logger_free(logger);
            buffer_free(buffer);
            free_mpd(mpd);
            return 2;
        }

        pthread_t downloader_thread;
    DownloaderArgs dargs = { mpd, queue, abr };
    pthread_create(&downloader_thread, NULL, downloader_thread_func, &dargs);

        // Main thread: pop from queue, decrypt, buffer, log
        for (int i = 0; i < mpd->total_frames; i++) {
            Frame* frame = download_queue_pop(queue);
            if (!frame) {
                fprintf(stderr, "[error] download_queue_pop returned NULL at frame %d\n", i);
                continue;
            }
            double dec_ms = 0.0;
            if (!frame->buffer) {
                fprintf(stderr, "[error] frame->buffer is NULL at frame %d\n", frame->index);
            } else {
                char outpath[512] = {0};
                if (write_output) {
                    snprintf(outpath, sizeof(outpath), "stream-download/frame_%d.ply", frame->index);
                }
                int rc = decrypt_file_buffer(frame->buffer, &dec_ms, write_output, write_output ? outpath : NULL);
                if (rc != 0) {
                    fprintf(stderr, "[warn] decrypt failed (rc=%d) for frame %d\n", rc, frame->index);
                }
                // ensure size recorded
                frame->size_bytes = frame->buffer ? frame->buffer->len : 0;
            }
            // Wait if buffer full
            while (buffer->count >= buffer->max_frames) {
                struct timespec ts = {0, 1000000}; // 1ms
                nanosleep(&ts, NULL);
            }

            if (buffer_add(buffer) != 0) {
               printf("[warn] buffer full, frame %d dropped.\n", frame->index);
                }
            //printf("Buffer initialized: count=%d\n", buffer->count);
            logger_add_frame(logger, frame->index, frame->dl_ms, dec_ms, buffer->count);
            if (logger->frame_size > 0) {
                int idx = logger->frame_size - 1;
                logger->frame_logs[idx].rep = frame->rep;
                if (mpd && frame->rep >= 0 && frame->rep < mpd->n_reps) {
                    logger->frame_logs[idx].bitrate = mpd->bitrates[frame->rep];
                }
                logger->frame_logs[idx].size_bytes = frame->size_bytes;
            }
            if (abr) {
                double total_ms = frame->dl_ms + dec_ms;
                abr_update_stats(abr, frame->size_bytes, total_ms);
            }
            if (frame->buffer) {
                g_byte_array_free(frame->buffer, 1);
            }
            free(frame);
        }

        pthread_join(downloader_thread, NULL);
        download_queue_free(queue);
    } else {
        // --- sequential Download, then buffer for no decryption (HTTPS or HTTP only) ---
        for (int i = 0; i < mpd->total_frames; i++) {
            int rep = 0;
            if (abr) rep = abr_select_for_frame(abr, i, buffer->count);
            const char* frame_url = mpd->frame_urls[rep][i];
            double dl_ms = 0.0, dec_ms = 0.0;
            size_t size_bytes = 0;
            if (!write_output) {
                GByteArray* buffer_mem = NULL;
                int rc = download_file_mem(frame_url, &buffer_mem, &dl_ms);
                if (rc != 0 || !buffer_mem) {
                    fprintf(stderr, "[warn] download failed (rc=%d) for %s\n", rc, frame_url);
                }
                if (!buffer_mem) {
                    fprintf(stderr, "[error] buffer_mem is NULL at frame %d\n", i);
                    continue;
                }
                size_bytes = buffer_mem->len;
                g_byte_array_free(buffer_mem, 1);
            }
            else {
                char outpath[512];
                snprintf(outpath, sizeof(outpath), "stream-download/frame_%d.ply", i);
                int rc = download_file(frame_url, outpath, &dl_ms);
                if (rc != 0) {
                    fprintf(stderr, "[warn] download failed (rc=%d) for %s\n", rc, frame_url);
                }
                // cannot easily get size when written to disk; leave as 0
            }
            while (buffer->count >= buffer->max_frames) {
                struct timespec ts = {0, 1000000}; // 1ms
                nanosleep(&ts, NULL);
            }
            if (buffer_add(buffer) != 0) {
                printf("[warn] buffer full, frame dropped.\n");
            }
            logger_add_frame(logger, i, dl_ms, dec_ms, buffer->count);
            if (logger->frame_size > 0) {
                int idx = logger->frame_size - 1;
                logger->frame_logs[idx].rep = rep;
                if (mpd && rep >= 0 && rep < mpd->n_reps) logger->frame_logs[idx].bitrate = mpd->bitrates[rep];
                logger->frame_logs[idx].size_bytes = size_bytes;
            }
            if (abr) {
                double total_ms = dl_ms + dec_ms;
                abr_update_stats(abr, size_bytes, total_ms);
            }
        }
    }

    // --- Join thread of virtual thread with main since download decrypts finished ---
    pthread_join(player_thread, NULL);

    // --- Finalize ---
    logger_flush(logger, "logs/stream.csv");
    logger_flush_player(logger, "logs/player.csv");

    decryptor_shutdown();
    logger_free(logger);
    buffer_free(buffer);
    free_mpd(mpd);
    if (abr) abr_free(abr);

    return 0;
}
