
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include "downloader.h"
#include "utils.h"

typedef struct {
    GByteArray* buf;
} MemDownloadCtx;

static size_t write_data_mem(void *ptr, size_t size, size_t nmemb, void *userdata) {
    MemDownloadCtx* ctx = (MemDownloadCtx*)userdata;
    size_t total = size * nmemb;
    g_byte_array_append(ctx->buf, (guint8*)ptr, (guint)total);
    return total;
}

int download_file_mem(const char* url, GByteArray** out_buf, double* time_ms) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    MemDownloadCtx ctx;
    ctx.buf = g_byte_array_new();

    double start = now_ms_mono();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data_mem);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    int res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    *time_ms = now_ms_mono() - start;
    if (res == 0) {
        *out_buf = ctx.buf;
    } else {
        g_byte_array_free(ctx.buf, 1);
        *out_buf = NULL;
    }
    return res;
}

// For write-out to disk in case of HTTPS and HTTP-only (no decryption)


static size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

int download_file(const char* url, const char* outpath, double* time_ms) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    FILE *fp = fopen(outpath, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return -1;
    }

    double start = now_ms_mono();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

    int res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    *time_ms = now_ms_mono() - start;
    return res;
}
