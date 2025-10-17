#ifndef DECRYPTOR_H
#define DECRYPTOR_H

// Initialize once per run. Returns 0 on success.
int decryptor_init(const char* pub, const char* priv, const char* pattern, int enabled);

// Decrypt a single frame from memory buffer. If disabled, returns 0 and sets *time_ms = 0.0.
int decrypt_file_buffer(GByteArray* buffer, double* time_ms, int write_output_flag, const char* output_ply_filename);

// Cleanup any allocated state.
void decryptor_shutdown(void);

#endif
