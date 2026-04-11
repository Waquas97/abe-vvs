# Inference integration — change log and notes

This document summarizes the inference-related changes, where they live, how they behave now (including the new buffer-threshold mode vs the time-threshold mode), assumptions made, build/run notes, and suggested next steps.

## Overview

Goal: integrate a Python/PyTorch GPU-based inference model into the streaming client so that inference runs synchronously after decryption for selected frames. Inference is optional and controlled by CLI flags; the client supports two decision modes:

- Time-threshold mode: when `--inference-threshold <ms>` is provided, the client uses a sliding-average of recent inference durations and only runs inference when the average is under the threshold.
- Buffer-threshold mode (default): when `--inference-threshold` is not supplied, the client uses buffer occupancy hysteresis: inference is enabled when the playback buffer has at least `--inference-buffer-threshold` frames and disabled when the buffer empties (count == 0). The default buffer threshold is 24.

Additionally, the implementation now uses a GPU-backed predictor implemented with PyTorch (CUDA). The Python-side feature construction still uses `pykdtree` on CPU.

## Files added / modified (high level)

- `build/src/inference.h` — public C API for the inference subsystem.
- `build/src/inference.c` — C glue that initializes Python/NumPy, imports `rf_sr_api`, calls the Python init routine, converts decrypted PLY memory to a NumPy array, calls Python inference, and returns timing.

Modifications (high level):

- `build/src/logger.h` / `logger.c`
  - Added `double inference_ms` to per-frame logs and CSV output.

- `build/src/main.c`
  - Added CLI flags and runtime variables for inference control:
    - `--inference` (enable inference)
    - `--inference-threshold <ms>` (optional; when provided use time-threshold mode)
    - `--inference-samples <N>` (sliding-window size, default 24)
    - `--inference-buffer-threshold <N>` (buffer occupancy threshold for buffer-based mode, default 24)
  - Behavior:
    - If `--inference-threshold` is supplied, use the original sliding-average time gating: run inference when average < threshold (or when no samples yet), skip otherwise.
    - If `--inference-threshold` is not supplied, use buffer-threshold hysteresis: enable inference once `buffer->count >= inference_buffer_threshold`; disable inference only when `buffer->count == 0` (this prevents flip-flopping and lets inference continue while the buffer stays filled).
    - Skips inference for the highest-representation frames (same as before).
    - Logs `inference_ms` (0.0 if skipped, >0.0 when executed).

## Current Python / GPU wiring

Files involved:
- `build/rf_sr_api.py` — a small adapter the C side imports as `rf_sr_api`. In the current workspace the adapter exposes:
  - `init_predictor(model_path)` — called by `inference.c` to initialize the Python predictor. (The adapter stores the predictor in a module-level `_predictor`.)
  - `run_inference(np_array)` — called by `inference.c` and expected to accept an (N,3) float32 NumPy array and return a NumPy array of dense points. The C side measures timing only and currently discards numeric results.

- `build/minimal_infer_gpu.py` — GPU model implementation using PyTorch and pykdtree. Key points:
  - Implements `load_gpu_model(npz_path)` and `make_predictor(model)` that build GPU tensors and create a batched predictor function (torch.compile may be used if available).
  - Exposes `init_model(model_path)` which loads the .npz and returns a predictor (in some layouts it may also set a module-level predictor).
  - Exposes `inference(sparse, predictor)` as a multi-stage inference/demo helper. The adapter (`rf_sr_api.py`) calls into the predictor via its stored predictor object.
  - CPU-side preprocessing (normalization and feature construction) lives in Python: `normalize_points()` and `build_features()` use `pykdtree` to compute kNN-based features which are then converted to GPU tensors for prediction.

Notes about the current wiring (important for debugging):
- The C code imports `rf_sr_api` and calls `init_predictor(model_path)`. That function delegates to `minimal_infer_gpu.init_model(...)` and stores the returned predictor in `_predictor`.
- During per-frame inference, `inference.c` builds an (N,3) NumPy array from decrypted PLY bytes and calls `rf_sr_api.run_inference(np_array)`. The adapter forwards the call to `minimal_infer_gpu.inference(np_array, _predictor)` (current layout) which returns dense points.

If you prefer a different naming convention (for example `init_model` / `run_inference` on `rf_sr_api`), we can change the adapter or C wrapper; current code uses `init_predictor` to match the adapter.

## PLY parsing assumptions (unchanged)

- `parse_ply_binary_to_floats()` in `inference.c` finds `end_header` and assumes binary vertex data follows immediately.
- It assumes vertex coordinate values are stored as 64-bit `double` triples (x,y,z) and casts them to float32 for the NumPy array.
- The function reads `element vertex <N>` from the header to determine vertex count.

If your frames use ASCII PLY, 32-bit floats, different property ordering (colors/normals), or different endianness, the C parser will not produce correct coordinates. In that case either adapt the C parser to inspect the header more carefully, or fall back to Python `PlyData.read` by passing bytes into Python.

## CLI defaults and behavior summary

- `--inference` : enable inference (default off)
- `--inference-threshold <ms>` : when supplied the client uses time-threshold mode; default (if not supplied) is to use buffer-threshold mode instead. The code default value for the variable is 500.0 ms but the flag is considered "present" only when explicitly provided on the command line.
- `--inference-samples <N>` : sliding window size for time-threshold mode (default 24)
- `--inference-buffer-threshold <N>` : occupancy threshold (frames) for buffer-threshold mode (default 24)

Mode semantics:
- Time-threshold mode (explicit `--inference-threshold`): maintain a sliding window of the last N inference times (N=`--inference-samples`) and compute the average; only run inference when average < threshold (or when window empty).
- Buffer-threshold mode (default when `--inference-threshold` not provided): maintain a hysteresis flag `inf_buffer_mode_active` which becomes true when `buffer->count >= inference_buffer_threshold` and becomes false only when `buffer->count == 0`. While active, inference runs for non-highest frames; when inactive inference is skipped.

## Logging semantics (unchanged)

- `inference_ms` in `logs/stream.csv`: 0.0 when inference skipped, >0.0 when executed; the CSV column is appended to the existing frame log columns.

<!-- ## GPU/runtime notes

- The Python inference implementation uses PyTorch on CUDA. Runtime machine must have a CUDA-capable GPU and matching PyTorch + CUDA driver.
- Feature construction uses `pykdtree` (CPU, OpenMP). The predictor runs on GPU; the code currently converts CPU feature arrays to GPU tensors each frame (this may allocate memory per-frame). If you observe increasing inference times over a long run, consider:
  - using `torch.no_grad()` around inference calls (recommended),
  - pre-allocating GPU input buffers and copying into them (avoid repeated allocations), or
  - disabling `torch.compile` if varying input shapes cause repeated compilation. -->
<!-- 
## Suggested next steps (revised)

1. Confirm the PLY payload format (binary doubles vs float32 / ASCII) for your streamed frames.
2. If you prefer a canonical API, standardize the adapter to expose `init_model(model_path)` and `run_inference(np_array)` and update `inference.c` to call those names; currently `inference.c` calls `init_predictor` to match the adapter.
3. Add small runtime diagnostics (per-frame `torch.cuda.memory_allocated()` and input `N`) if you see slowly-increasing inference durations.
4. Consider adding `torch.no_grad()` / `.eval()` in the Python predictor init and inference path to reduce overhead and memory pressure.
5. If inference latency hurts playback, implement an async worker pool for inference to decouple decryption from model runs.

If you want, I can now apply any of the following (pick one):
- add `init_model` alias to `rf_sr_api.py` (or rename `init_predictor` there),
- add `torch.no_grad()` and `.eval()` usage in `minimal_infer_gpu.py`,
- add a small diagnostic print when buffer-gate hysteresis toggles in `main.c`, or
- implement a safe fallback in `inference.c` to call Python's `load_ply_points` if the header format doesn't match.

Tell me which follow-up you want and I will implement it next. -->