import open3d as o3d

# 1. Read the PLY file
pcd = o3d.io.read_point_cloud("50-percent-office-1.ply")

# (Optional) Process the point cloud, e.g., downsample
# pcd = pcd.voxel_down_sample(voxel_size=0.05)

# 2. Save the PLY file
o3d.io.write_point_cloud("work.ply", pcd)
