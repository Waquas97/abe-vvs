import open3d as o3d
import sys

input= sys.argv[1]

# Load point cloud
pcd = o3d.io.read_point_cloud(input)


o3d.visualization.draw_geometries([pcd])


