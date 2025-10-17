#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "decryptor.h"
#include "utils.h"

static int g_enabled = 0;
static const char* g_pub = NULL;
static const char* g_priv = NULL;
static const char* g_pat = NULL;

#ifdef USE_CPABE_LIB
// === In-process path ===
// Provide a thin shim header to your toolkit.
// You can adapt these signatures to your actual functions:
#include "cpabe_shim.h"
// Expected in cpabe_shim.h/c:
//   int cpabe_ctx_init(const char* pub, const char* priv, const char* pattern);
//   int cpabe_decrypt_ply(const char* filepath);   // uses preloaded ctx
//   void cpabe_ctx_free(void);
#endif

int decryptor_init(const char* pub, const char* priv, const char* pattern, int enabled) {
    g_enabled = enabled;
    g_pub = pub;
    g_priv = priv;
    g_pat = pattern;
    

    if (!g_enabled) return 0;
    

#ifdef USE_CPABE_LIB
    // Load keys/policy once, reuse for all frames
    
    int rc = cpabe_ctx_init(g_pub, g_priv, g_pat);
    
    if (rc != 0) {
        fprintf(stderr, "[decryptor] cpabe_ctx_init failed (%d)\n", rc);
        return rc;
    }
#endif
    return 0;
}


void decryptor_shutdown(void) {
#ifdef USE_CPABE_LIB
    if (g_enabled) {
        cpabe_ctx_free();
    }
#endif
}

int decrypt_file_buffer(GByteArray* buffer, double* time_ms, int write_output_flag, const char* output_ply_filename) {
    if (time_ms) *time_ms = 0.0;
    if (!g_enabled) return 0;
#ifdef USE_CPABE_LIB
    //double t0 = now_ms_mono();
    int ret = cpabe_decrypt_ply_buffer(buffer, time_ms, write_output_flag, output_ply_filename);
    //double t1 = now_ms_mono();
    //if (time_ms) *time_ms = t1 - t0;
    //printf("[decrypt_file_buffer] cpabe_decrypt_ply_buffer: %.2f ms\n", t1-t0);
    return ret;
#else
    // If not using CP-ABE, just return success
    return 0;
#endif
}