#define _POSIX_C_SOURCE 200809L
#include "utils.h"
#include <unistd.h>   // close(), unlink()
#include <fcntl.h>    // mkstemp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <glib.h>
#include <pbc.h>

#include "bswabe.h"
#include "cpabe/common.h"     // parse_pattern, read_cpabe_file, expand_and_restore_stripped_inplace, etc.
#include "cpabe_shim.h"
#include "utils.h"   // for now_ms_mono

/* ----------------------- Static context ----------------------- */
static bswabe_pub_t* g_pub = NULL;
static bswabe_prv_t* g_prv = NULL;
static char*         g_pattern = NULL;
static EncryptPattern g_parsed_pattern;
char* pattern_arg = NULL;   /* defined extern in common.h; some helpers read this */

/* Small helper: strdup safely */
static char* xstrdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

/* ----------------------- API impl ----------------------- */

int cpabe_ctx_init(const char* pub_path, const char* prv_path, const char* pattern)
{   
    if (!pub_path || !prv_path || !pattern) {
        fprintf(stderr, "[cpabe_shim] missing pub/priv/pattern\n");
        return -1;
    }

    /* Free any previous ctx */
    cpabe_ctx_free();
    /* Load keys once */
    GByteArray* pub_bytes = suck_file((char*)pub_path);
    if (!pub_bytes) {
        fprintf(stderr, "[cpabe_shim] suck_file(pub) failed: %s\n", pub_path);
        return -2;
    }
    g_pub = bswabe_pub_unserialize(pub_bytes, 1);
    ///g_byte_array_free(pub_bytes, 1);
    if (!g_pub) {
        fprintf(stderr, "[cpabe_shim] bswabe_pub_unserialize failed\n");
        cpabe_ctx_free();
        return -3;
    }

    GByteArray* prv_bytes = suck_file((char*)prv_path);
    if (!prv_bytes) {
        fprintf(stderr, "[cpabe_shim] suck_file(prv) failed: %s\n", prv_path);
        cpabe_ctx_free();
        return -4;
    }
    g_prv = bswabe_prv_unserialize(g_pub, prv_bytes, 1);
    ///g_byte_array_free(prv_bytes, 1);
    if (!g_prv) {
        fprintf(stderr, "[cpabe_shim] bswabe_prv_unserialize failed (attrs mismatch?)\n");
        cpabe_ctx_free();
        return -5;
    }
    /* Cache pattern and export to global expected by read_cpabe_file() */
    g_pattern  = xstrdup(pattern);
    pattern_arg = g_pattern;

    /* Parse and cache pattern once */
    g_parsed_pattern = parse_pattern(g_pattern);
    if (!(g_parsed_pattern.encrypt_x || g_parsed_pattern.encrypt_y || g_parsed_pattern.encrypt_z)) {
        fprintf(stderr, "[cpabe_shim] Invalid pattern '%s' (use x|y|z|xy|yz|xyz)\n", g_pattern);
        printf("g_pattern = '%s'\n", g_pattern);
        cpabe_ctx_free();
        return -6;
    }

    return 0;
}

void cpabe_ctx_free(void)
{
    if (g_prv) { bswabe_prv_free(g_prv); g_prv = NULL; }
    if (g_pub) { bswabe_pub_free(g_pub); g_pub = NULL; }
    if (g_pattern) { free(g_pattern); g_pattern = NULL; }
    pattern_arg = NULL;
    g_parsed_pattern = (EncryptPattern){0,0,0};
}

int cpabe_decrypt_ply_buffer(GByteArray* buffer, double* time_ms, int write_output_flag, const char* output_ply_filename)
{
    double tx = now_ms_mono();
    if (!g_pub || !g_prv || !g_pattern) {
        fprintf(stderr, "[cpabe_shim] ctx not initialized\n");
        return -1;
    }
    if (!buffer || buffer->len == 0) return -2;

    // double t0 = now_ms_mono();
    // Parse the buffer as a cpabe file trailer
    GByteArray *cph_buf = NULL, *aes_buf = NULL;
    int file_len = 0;
    parse_cpabe_buffer(buffer, &cph_buf, &file_len, &aes_buf);

    // double t1 = now_ms_mono();
    bswabe_cph_t* cph = bswabe_cph_unserialize(g_pub, cph_buf, 1);
    // double t3 = now_ms_mono();
    if (!cph) {
        fprintf(stderr, "[cpabe_shim] cph unserialize failed for buffer\n");
        g_byte_array_free(aes_buf, 1);
        return -4;
    }
    element_t m;
    // double t_dec_start = now_ms_mono();
    int dec_ok = bswabe_dec(g_pub, g_prv, cph, m);
    // double t_dec_end = now_ms_mono();
    bswabe_cph_free(cph);
    if (!dec_ok) {
         const char* err = bswabe_error();
         fprintf(stderr, "[cpabe_shim] bswabe_dec failed: %s\n", err ? err : "(unknown)");
         g_byte_array_free(aes_buf, 1);
         return -5;
     }
    GByteArray* pt_payload = aes_128_cbc_decrypt(aes_buf, m);
    element_clear(m);

    // Use pattern parsed at init
    EncryptPattern pat = g_parsed_pattern;

    // Output filename logic simplified
    const char* output_filename = (write_output_flag && output_ply_filename) ? output_ply_filename : NULL;

    // Rebuild full PLY in memory, optionally write to disk
    GByteArray* rebuilt_ply = restore_stripped_rebuild(
        buffer,
        output_filename,
        pt_payload,
        pat
    );

    g_byte_array_free(rebuilt_ply, 1);
    g_byte_array_free(pt_payload, 1);
    g_byte_array_free(aes_buf, 1);
    if (time_ms) *time_ms = now_ms_mono() - tx;
    return 0;
}