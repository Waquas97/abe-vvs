import sys
from pyntcloud import PyntCloud
input= sys.argv[1]

cloud = PyntCloud.from_file(input)
#print(cloud.points.head())  # Display first few rows of point data
print((cloud.points))

cloud.points.to_csv(input+".csv", index=False) #save to csv file