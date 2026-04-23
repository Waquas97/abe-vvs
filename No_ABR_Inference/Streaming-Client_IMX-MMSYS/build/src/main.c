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
        " [--write-output]\n"
        "Notes:\n"
        "  • MPD is parsed before timing begins (excluded from measurements).\n"
        "  • Frames are saved to ./stream-download and logs to ./logs.\n"
        "  • If --decrypt is omitted, frames enter buffer immediately after download.\n"
        "  • --write-output is optional; saves frames to disk if specified.\n"
        "  [--download-queue <size>]  (max frames in download queue for pipelined mode)\n",
        prog);
}


// --- Thread argument structs ---
typedef struct {
    MPDInfo* mpd;
    DownloadQueue* queue;
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
        const char* frame_url = dargs->mpd->frame_urls[i];
        frame->buffer = NULL;
        int rc = download_file_mem(frame_url, &frame->buffer, &frame->dl_ms);
        if (rc != 0 || !frame->buffer) {
            fprintf(stderr, "[warn] download failed (rc=%d) for %s\n", rc, frame_url);
        }
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
        DownloaderArgs dargs = { mpd, queue };
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
            const char* frame_url = mpd->frame_urls[i];
            double dl_ms = 0.0, dec_ms = 0.0;
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
                g_byte_array_free(buffer_mem, 1);
            }
            else {
                char outpath[512];
                snprintf(outpath, sizeof(outpath), "stream-download/frame_%d.ply", i);
                int rc = download_file(frame_url, outpath, &dl_ms);
                if (rc != 0) {
                    fprintf(stderr, "[warn] download failed (rc=%d) for %s\n", rc, frame_url);
                }
            }
            while (buffer->count >= buffer->max_frames) {
                struct timespec ts = {0, 1000000}; // 1ms
                nanosleep(&ts, NULL);
            }
            if (buffer_add(buffer) != 0) {
                printf("[warn] buffer full, frame dropped.\n");
            }
            logger_add_frame(logger, i, dl_ms, dec_ms, buffer->count);
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

    return 0;
}
