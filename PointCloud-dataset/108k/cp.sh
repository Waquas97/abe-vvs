#!/bin/bash

src="108k-xyz.ply.cpabe"

# Loop from 1 to 1440 (24fps * 60sec)
for i in $(seq 1 1440); do
    num=$(printf "%05d" $i)   # force 5 digits
    cp "$src" "${num}.ply.cpabe"
done

