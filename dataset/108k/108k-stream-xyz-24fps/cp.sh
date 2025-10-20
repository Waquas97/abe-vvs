#!/bin/bash

# This script creates 1440 symbolic links to a single source file.
# Symbolic links are pointers to the original file, saving significant disk space
# compared to making full copies.

# The source file that the symbolic links will point to.
src="108k-xyz-00001.ply"

# Check if the source file exists before starting the loop.
if [ ! -f "$src" ]; then
    echo "Error: Source file '$src' not found."
    exit 1
fi

echo "Creating 1440 symbolic links pointing to '$src'..."

# Loop from 1 to 1440 (representing 24fps * 60sec).
for i in $(seq 1 1440); do
    # Format the loop number to be a 5-digit string with leading zeros (e.g., 00001, 00002).
    num=$(printf "%05d" "$i")

    # Define the name for the symbolic link.
    link_name="108k-xyz-${num}.ply.cpabe"

    # Create the symbolic link using 'ln -s'.
    # This command creates a link named $link_name that points to the $src file.
    ln -s "$src" "$link_name"
done

echo "Successfully created 1440 symbolic links."
