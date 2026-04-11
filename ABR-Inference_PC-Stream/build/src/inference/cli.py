from rf_sr_api import init_model, run_inference
from infer_rf_sr import load_ply_points, save_ply_64bit
import argparse
import time

parser = argparse.ArgumentParser()
parser.add_argument("input_ply")
parser.add_argument("output_ply")
args = parser.parse_args()

init_model("rf_sr_cross_liv2off.joblib")

points = load_ply_points(args.input_ply)
t1 = time.perf_counter()
dense = run_inference(points)
t2 = time.perf_counter()
elapsed_time = t2 - t1
print(f"Elapsed time: {elapsed_time} seconds")

# save_ply_64bit(args.output_ply, dense)