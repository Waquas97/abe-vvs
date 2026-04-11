import open3d as o3d
import numpy as np
import sys

input= sys.argv[1]

# # Load point cloud
pcd = o3d.io.read_point_cloud(input)


o3d.visualization.draw_geometries([pcd])


import pandas as pd
from pyntcloud import PyntCloud

cloud = PyntCloud.from_file(input)
print(cloud.points.head())  # Display first few rows of point data
#print(cloud.points)
cloud.points.to_csv(input+".csv", index=False)