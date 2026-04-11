# Functionality Memo: `latest-fast-strip-from-fallocate` Folder

## Overview
This folder contains the core decryption and restoration logic for point cloud files protected with attribute-based encryption (ABE). It processes PLY files with stripped and encrypted coordinate properties, restoring them using decrypted values.

## Main Components
- **dec.c**: Main decryption and restore driver. Handles command-line arguments, reads encrypted payloads, decrypts coordinates, and restores full PLY files using the portable fallback method.
- **common.c / common.h**: Shared helpers for parsing PLY headers, handling encryption patterns, reading/writing files, and restoring stripped PLY files. All fast in-place restore logic (fallocate/insert-range) has been removed; only portable restore remains.

## Key Functionality
- **Portable Restore Only**: Exclusively uses the portable fallback method (`restore_stripped_rebuild` and `restore_ply_with_coords`) to reconstruct full PLY files by interleaving decrypted coordinates with reduced rows. This method works on all platforms and filesystems.
- **No Fast Path**: All logic related to fast in-place restore (Linux-specific fallocate/insert-range) has been removed for simplicity and portability.
- **Helpers**: Utilities for reading/writing files, parsing encryption patterns, and handling AES decryption.

## Usage
- Run the decryption tool (`dec.c`) with appropriate arguments to restore a stripped/encrypted PLY file.
- The process reads the encrypted coordinate payload, decrypts it, and rebuilds the full PLY file using only portable methods.

## Dependencies
- Requires GLib and PBC libraries for byte array handling and pairing-based cryptography.
- Ensure development headers for GLib (`glib.h`) are installed for successful compilation.

## Restore Function Details

### restore_ply_with_coords
This function reconstructs a full PLY file from a stripped (reduced) PLY and a buffer of decrypted coordinates:
- Reads the stripped PLY header and parses vertex property layout.
- Determines which properties were stripped (encrypted) and which were retained.
- For each vertex, interleaves the retained properties from the reduced file and the decrypted coordinates from the buffer, reconstructing the original vertex rows.
- Writes the rebuilt rows to the output PLY file in batches for efficiency.
- Handles arbitrary property order and supports any combination of x/y/z stripping.


this restore method does not use byteshift or in-place expansion to create space for removed coordinates.

Instead, it reconstructs each full vertex row by reading the reduced (stripped) row and interleaving the decrypted coordinates from a separate buffer. The output is written to a new file, assembling each row from its components, rather than modifying the original file in-place or shifting bytes to make space.

This approach is robust and works on all filesystems, but does not perform any in-place byte shifting or expansion.

### restore_stripped_rebuild
This is a simple wrapper that always calls `restore_ply_with_coords`.
- Used as the portable fallback restore method.
- Ensures the codebase only uses the robust, platform-independent restore logic.

These functions guarantee that the restored PLY file matches the original structure, regardless of which coordinates were encrypted/stripped.

## Summary
This folder provides a robust, portable solution for decrypting and restoring point cloud files. All platform-specific optimizations have been removed, ensuring consistent behavior across environments.
