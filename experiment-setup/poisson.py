import numpy as np
import time
import subprocess
import sys

def poisson_calculate(nums):
    # Set the rate (lambda) for an average of 6 minutes (360 seconds)
    lambda_rate = 5 
    #old was 120
    sleep_times=[]
    # Loop through the remaining nodes and run the command at Poisson-distributed intervals
    for n in range (nums):
        # Generate a Poisson-distributed interval
        interval = np.random.poisson(lambda_rate)
        
        # Ensure the interval is within your desired range
        interval = max(0, min(interval, 60))  # Clamp the interval to [0, 720] seconds (0 to 12 minutes)
        sleep_times.append(interval)

    print(sleep_times)

poisson_calculate(24)