#ifndef DOWNLOADER_H
#define DOWNLOADER_H
#include <glib.h>

// Download file to memory buffer (GByteArray)
int download_file_mem(const char* url, GByteArray** out_buf, double* time_ms);


// Download file to disk at outpath for HTTPS and HTTP-only (no decryption)
int download_file(const char* url, const char* outpath, double* time_ms);

#endif



