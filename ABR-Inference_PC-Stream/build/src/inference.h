#ifndef INFERENCE_H
#define INFERENCE_H

#include <glib.h>

// Initialize inference subsystem. model_path may be NULL for defaults. Returns 0 on success.
int inference_init(const char* model_path);

// Run inference on an in-memory PLY buffer. Returns 0 on success. inference_ms (ms) and label are output.
int inference_run_buffer(GByteArray* ply_buf, double* inference_ms);

// Shutdown inference subsystem and free Python state.
void inference_shutdown(void);

#endif
