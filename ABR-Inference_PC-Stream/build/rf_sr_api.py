"""Small adapter module exposing a stable API for the C-side inference wrapper.

Provides:
  - init_model(npz_path, k_sparse=16, m_dense=2, warmup_iters=3)
  - run_inference(np_array) -> numpy.ndarray (float32)

This module delegates to `minimal_infer_gpu` where available, and provides
fallbacks when the exact helper names differ across versions. It is import-safe
(i.e. it does not run heavy work on import).
"""

# module-level predictor placeholder used by init_model / run_inference
_predictor = None

import numpy as np
import minimal_infer_gpu as mg 

def init_predictor(model_path):
    global _predictor
    _predictor = mg.init_model(model_path)
    return _predictor



def run_inference(sparse):
    global _predictor
    if _predictor is None:
        raise RuntimeError("Model not initialized")

    sparse = sparse.astype(np.float32)


    # Inference (2×)
    dense_n = mg.inference(sparse, _predictor)

    return dense_n

