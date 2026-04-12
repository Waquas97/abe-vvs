
/* Decrypts/restores a single PLY from a file path (legacy API).
 * Returns 0 on success; non-zero on failure.
 */
#ifndef CPABE_SHIM_H
#define CPABE_SHIM_H

/* Initialize CPABE context once; load pub/priv and remember the pattern.
 * Returns 0 on success; non-zero on failure.
 */
int cpabe_ctx_init(const char* pub_path, const char* prv_path, const char* pattern);


/* Decrypts/restores a single PLY from a memory buffer (GByteArray).
 * Returns 0 on success; non-zero on failure.
 */
int cpabe_decrypt_ply_buffer(GByteArray* buffer, double* time_ms, int write_output_flag, const char* output_ply_filename);

/* Free any global/heap state (keys, pattern, buffers). */
void cpabe_ctx_free(void);

#endif /* CPABE_SHIM_H */
