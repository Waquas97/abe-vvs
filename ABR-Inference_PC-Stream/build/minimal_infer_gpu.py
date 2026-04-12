#!/usr/bin/env python3
"""
GPU-accelerated batch inference for RF point cloud super-resolution.

Supports both Model A (cross-domain: Living→Office) and Model C (mixed-domain).
Uses PyTorch for GPU tree traversal with torch.compile optimization.
Uses pykdtree (OpenMP) for kNN feature construction on CPU.

Requirements (GPU container):
    pip install scikit-learn joblib trimesh pykdtree plyfile

Usage:
    # Model A (cross-domain, office only)
    python batch_infer_gpu.py --mode A --scale 2

    # Model C (mixed-domain, both living + office)
    python batch_infer_gpu.py --mode C --scale 2

    # With specific CPU core count (for fair hardware comparison)
    OMP_NUM_THREADS=10 python batch_infer_gpu.py --mode A --scale 2

    # Override model file
    python batch_infer_gpu.py --mode A --scale 2 --model-npz custom_model.npz
"""

import os, time, csv, re, argparse
import numpy as np
import torch
from plyfile import PlyData, PlyElement
from pykdtree.kdtree import KDTree

# ------------------------------------------------------------
# Default model paths per mode
# ------------------------------------------------------------



# Constants
# ------------------------------------------------------------
K_SPARSE = 16
M_DENSE = 2

# ------------------------------------------------------------
# PLY I/O
# ------------------------------------------------------------
def load_ply_points(path):
    p = PlyData.read(path)
    v = p['vertex']
    return np.column_stack([v['x'], v['y'], v['z']]).astype(np.float32)

def save_ply_64bit(path, points):
    points = np.asarray(points, dtype=np.float64)
    vertex = np.array(
        [(p[0], p[1], p[2]) for p in points],
        dtype=[('x', 'f8'), ('y', 'f8'), ('z', 'f8')]
    )
    ply = PlyData([PlyElement.describe(vertex, 'vertex')], text=False)
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, "wb") as f:
        ply.write(f)

def count_vertices(ply_path):
    try:
        with open(ply_path, "r", errors="ignore") as f:
            for line in f:
                if line.startswith("element vertex"):
                    return int(re.findall(r"\d+", line)[0])
    except Exception:
        pass
    return -1

# ------------------------------------------------------------
# Feature construction (CPU, pykdtree with OpenMP)
# ------------------------------------------------------------
def normalize_points(points):
    c = np.mean(points, axis=0, dtype=np.float32)
    s = np.max(np.linalg.norm(points - c, axis=1)).astype(np.float32)
    s = s if s > 0 else np.float32(1.0)
    return ((points - c) / s).astype(np.float32), c, s

def build_features(sparse):
    n = sparse.shape[0]
    if n == 0:
        return np.zeros((0, 5), dtype=np.float32)
    k = min(K_SPARSE + 1, n)
    kd = KDTree(sparse)
    d_k, _ = kd.query(sparse, k=k)
    nn_mean = np.mean(d_k[:, 1:], axis=1).astype(np.float32) if k >= 2 else np.zeros((n,), dtype=np.float32)
    base = np.column_stack([sparse, nn_mean]).astype(np.float32)
    base_rep = np.repeat(base, M_DENSE, axis=0)
    ranks = np.tile(np.arange(1, M_DENSE + 1, dtype=np.float32), (n, 1)).reshape(-1, 1)
    return np.column_stack([base_rep, ranks]).astype(np.float32)

# ------------------------------------------------------------
# GPU model loading
# ------------------------------------------------------------
def load_gpu_model(npz_path):
    """Load exported tree arrays and prepare GPU tensors."""
    data = np.load(npz_path)
    n_trees = int(data['n_trees'])

    max_nodes = max(len(data[f'children_left_{i}']) for i in range(n_trees))

    def pad(arr, size, fill=-1):
        out = np.full(size, fill, dtype=arr.dtype)
        out[:len(arr)] = arr
        return out

    left_all = torch.tensor(
        np.stack([pad(data[f'children_left_{i}'], max_nodes) for i in range(n_trees)]),
        device='cuda', dtype=torch.long)
    right_all = torch.tensor(
        np.stack([pad(data[f'children_right_{i}'], max_nodes) for i in range(n_trees)]),
        device='cuda', dtype=torch.long)
    feat_all = torch.tensor(
        np.stack([pad(data[f'feature_{i}'], max_nodes, fill=-2) for i in range(n_trees)]),
        device='cuda', dtype=torch.long)
    thresh_all = torch.tensor(
        np.stack([pad(data[f'threshold_{i}'], max_nodes) for i in range(n_trees)]),
        device='cuda', dtype=torch.float32)

    val_list = []
    for i in range(n_trees):
        v = data[f'value_{i}'].squeeze()
        padded = np.zeros((max_nodes, 3), dtype=np.float32)
        padded[:len(v)] = v
        val_list.append(padded)
    val_all = torch.tensor(np.stack(val_list), device='cuda', dtype=torch.float32)

    out3 = torch.arange(3, device='cuda')

    # Determine max depth from tree structure
    feat0 = data['feature_0']
    left0 = data['children_left_0']
    def get_depth(node, depth):
        if feat0[node] < 0:
            return depth
        return max(get_depth(left0[node], depth+1),
                   get_depth(int(data['children_right_0'][node]), depth+1))
    max_depth = get_depth(0, 0)

    model = {
        'n_trees': n_trees,
        'max_depth': max_depth,
        'max_nodes': max_nodes,
        'left_all': left_all,
        'right_all': right_all,
        'feat_all': feat_all,
        'thresh_all': thresh_all,
        'val_all': val_all,
        'out3': out3,
    }

    print(f"✅ Loaded GPU model: {n_trees} trees, max_depth={max_depth}, max_nodes={max_nodes}")
    return model

# ------------------------------------------------------------
# GPU prediction (batched, compiled)
# ------------------------------------------------------------
def make_predictor(model):
    n_trees = model['n_trees']
    max_depth = model['max_depth']
    left_all = model['left_all']
    right_all = model['right_all']
    feat_all = model['feat_all']
    thresh_all = model['thresh_all']
    val_all = model['val_all']
    out3 = model['out3']

    def predict_rf_batched(X):
        n_samples = X.shape[0]
        node_idx = torch.zeros(n_trees, n_samples, device='cuda', dtype=torch.long)
        tree_idx = torch.arange(n_trees, device='cuda').unsqueeze(1).expand(n_trees, n_samples)
        X_exp = X.unsqueeze(0).expand(n_trees, -1, -1)
        for _ in range(max_depth):
            feat = feat_all[tree_idx, node_idx]
            is_leaf = (feat < 0)
            if is_leaf.all():
                break
            feat_clamped = feat.clamp(min=0)
            feat_vals = torch.gather(X_exp, 2, feat_clamped.unsqueeze(-1)).squeeze(-1)
            thresh = thresh_all[tree_idx, node_idx]
            go_left = feat_vals <= thresh
            next_nodes = torch.where(go_left, left_all[tree_idx, node_idx], right_all[tree_idx, node_idx])
            node_idx = torch.where(is_leaf, node_idx, next_nodes)
        values = val_all[tree_idx.unsqueeze(-1).expand(-1, -1, 3),
                         node_idx.unsqueeze(-1).expand(-1, -1, 3), out3]
        return values.mean(dim=0)

    # Compile for optimization
    try:
        compiled = torch.compile(predict_rf_batched)
        print("✅ torch.compile enabled")
    except Exception:
        compiled = predict_rf_batched
        print("⚠️  torch.compile unavailable, using eager mode")

    return compiled

# ------------------------------------------------------------
# Inference function
# # ------------------------------------------------------------
# def infer_dense_points_gpu(predictor, sparse_xyz):
#     """Build features on CPU, predict on GPU, return dense points as numpy."""
#     X = build_features(sparse_xyz)
#     X_gpu = torch.tensor(X, device='cuda', dtype=torch.float32)
#     y_pred = predictor(X_gpu)
#     torch.cuda.synchronize()
#     y_np = y_pred.cpu().numpy().astype(np.float32)
#     sparse_rep = np.repeat(sparse_xyz, M_DENSE, axis=0).astype(np.float32)
#     return sparse_rep + y_np

def infer_dense_points_gpu(predictor, sparse_xyz):
    X = build_features(sparse_xyz)
    X_gpu = torch.from_numpy(X).float().cuda()

    with torch.no_grad():
        y_pred = predictor(X_gpu)

    torch.cuda.synchronize()

    y_np = y_pred.detach().cpu().numpy().astype(np.float32)

    #FORCE FREE
    del X_gpu
    del y_pred

    sparse_rep = np.repeat(sparse_xyz, M_DENSE, axis=0).astype(np.float32)
    return sparse_rep + y_np

def infer_chain_memory_gpu(predictor, sparse_xyz, stages):
    """Multi-stage inference chain with per-stage normalization."""
    results = []
    current = sparse_xyz
    for i, (tag, label) in enumerate(stages, 1):
        sn, c, s = normalize_points(current)
        t0 = time.time()
        dense_n = infer_dense_points_gpu(predictor, sn)
        torch.cuda.synchronize()
        dt = time.time() - t0
        dense = (dense_n * s + c).astype(np.float32)
        results.append((label, current.shape[0], dense.shape[0], dt))
        current = dense
    return current, results


def init_model(model_path):
    model = load_gpu_model(model_path)
    predictor = make_predictor(model)

    # Warmup: run a small batch through the compiled predictor
    dummy = torch.randn(1000, 5, device='cuda', dtype=torch.float32)
    for _ in range(3):
        _ = predictor(dummy)
        torch.cuda.synchronize()
    del dummy


    ##################### THROW THIS LATER ##########################
    gpu_name = torch.cuda.get_device_name(0)
    omp = os.environ.get('OMP_NUM_THREADS', 'not set')
    print(f"\nModel {model_path} — GPU inference")
    print(f"  Scale factor: 2×")
    print(f"  GPU: {gpu_name}")
    print(f"  OMP_NUM_THREADS: {omp}")
    ##################################################################

    return predictor


def inference(sparse, predictor):
    stages = [("50-percent", "50→100")]
    # scale=2
    # sparse = load_ply_points(INPUT_PLY)
    final_dense, recs = infer_chain_memory_gpu(predictor, sparse, stages)
    # FOR DEBUGGING ONLY
    # save_ply_64bit("check.ply", final_dense)
    return final_dense





# INPUT_PLY = "50-percent-office-1.ply"
# OUTPUT_PLY = "output_refined.ply"
# MODEL_NPZ = "rf_cross_50t_d12_trees.npz"
# TIMING_CSV = "single_run_timing.csv"



# ------------------------------------------------------------
# Load model and create predictor
# ------------------------------------------------------------




# ------------------------------------------------------------
# Print config
# ------------------------------------------------------------




# sparse = load_ply_points(INPUT_PLY)
# print(f"[{INPUT_PLY}]")

# # --- Multi-stage inference ---
# final_dense, recs = infer_chain_memory_gpu(predictor, sparse, stages)

# # --- Sanity check ---
# # sparse_n = sparse.shape[0]
# # expected_n = sparse_n * scale
# # final_n = final_dense.shape[0]
# # if abs(final_n - expected_n) > 0.02 * expected_n:
# #     print(f"⚠️  {INPUT_PLY}: produced {final_n}, expected ≈{expected_n}")

# # --- Save output ---
# save_ply_64bit(OUTPUT_PLY, final_dense)
# out_pts = count_vertices(OUTPUT_PLY)
# total_time = sum(dt for _, _, _, dt in recs)
# print(f"   ✅ {OUTPUT_PLY} | {out_pts:,} pts (expected ≈{expected_n:,}) | {total_time:.3f}s")

# # # --- Log timings ---
# # with open(TIMING_CSV, "a", newline="") as f:
# #     writer = csv.writer(f)
# #     for (label, inc, outc, dt) in recs:
# #         writer.writerow([INPUT_PLY, domain, label, inc, outc, f"{dt:.3f}"])

# print(f"\n✅ Done. Results → {TIMING_CSV}")
# print(f"Output: {OUTPUT_PLY}/")