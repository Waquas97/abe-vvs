# Pipelined Streaming Client for Point Cloud Video

## 1. Overview
This client fetches and plays back **point cloud video frames** from an MPD manifest, supporting both sequential (HTTPS, HTTP only) and pipelined (multi-threaded) (for HTTP-ABE) operation. It simulates a client for a single quality level.

Features:
- **MPD parsing** for framerate, duration, and frame URLs
- **HTTP/HTTPS downloading** (ignores TLS certificates)
- **Optional CP-ABE decryption** (in-process, no external binary)
- **pipelined download, Configurable download queue** for pipelined mode
- **Buffering** for streaming playback
- **Virtual player** thread
- **CSV logging** of download/decrypt times, buffer levels, and stalls

---

## 2. Usage

```
./stream_client --url <mpd_path_or_url> --buffer <seconds>
                [--decrypt --pub <pub_key> --priv <priv_key> --pattern <scheme>]
                [--download-queue <size>]
                [--write-output]
```

- `--url <path>` : Path or URL to the MPD file
- `--buffer <seconds>` : Playback buffer size in seconds
- `--decrypt` : Enable CP-ABE decryption (requires `--pub`, `--priv`, `--pattern`)
- `--download-queue <size>` : Max frames in download queue (for pipelined mode)
- `--write-output` : Write the final decrypted/rebuilt PLY frame to disk for ABE, for HTTPS, HTTP-only, curl write to disk (default: disabled)

**Note:** In-memory download and decryption is now the default and permanent approach. All frame data is processed in memory unless `--write-output` is specified to save the final PLY.

Examples:
```bash
# Streaming without decryption, 2-second buffer
./stream_client --url office-196k.mpd --buffer 2

# Streaming with pipelined download, sequential decrypt, queue size 10
./stream_client --url office-196k.mpd --buffer 2 --decrypt --pub pub_key --priv priv_key --pattern xyz --download-queue 10

# Streaming and writing final decrypted PLY frames
./stream_client --url office-196k.mpd --buffer 2 --decrypt --pub pub_key --priv priv_key --pattern xyz --write-output
```

---

## 3. Architecture & Features
### In-Memory Operations (Default)
- All frame downloads and decryptions are performed in memory for maximum speed and efficiency.
- Disk writes are only performed if `--write-output` is specified.

### Output File Flag
- If `--write-output` is set, the client writes the final decrypted and rebuilt PLY frame(s) to disk after decryption.

### CP-ABE Vertex Rebuilding
- This version uses a CP-ABE implementation that rebuilds vertex rows from decrypted coordinates and reduced rows, replacing the previous fallocate-based approach.
- The vertex rebuilding logic is based on the fallback version of the fallocate approach which was fast.

### MPD Parsing
- Reads MPD manifest before starting (excluded from timing)
- Extracts `frameRate`, `mediaPresentationDuration`, and frame URLs

### Pipelined Download
- **Downloader thread** downloads frames and pushes them to a thread-safe queue
- **Decryptor sequential** pops frames from the queue, decrypts, and adds to buffer
- **Queue size** is set by `--download-queue`; downloader blocks if full
- If decryption is disabled, frames are added to buffer immediately after download (sequential mode)

### Buffer
- Configurable size: `--buffer <seconds>`
- Measured in seconds worth of frames (`seconds * fps`)
- Frames added after download (+ decrypt if enabled)
- If the buffer is full, wait 1ms, then recheck if the  buffer can get a new frame.

### Virtual Player
- Consumes frames from buffer at exact `fps` using monotonic clock
- Logs stall (rebuffering) events if buffer is empty
- Runs in its own thread

### Logging
- Logs kept in memory, flushed at end to `logs/stream.csv` and `logs/player.csv`
- **Frame logs**: `frame,download_ms,decrypt_ms,buffer_count`
- **Stall logs**: `stall_start_ms,duration_ms`
- **Player logs**: buffer and player state

---

## 4. Earlier Limitations
- **Buffer full**: SOLVED through waiting 1ms for buffer to have space before adding new frame **SOLVED**
- **Polling stalls**: player checks every 1ms (no condvars yet)
- **Output in memory:** SOLVED, in-memory download/decrypt is now the default; disk writes only with `--write-output` **SOLVED**

---

## 5. Planned Improvements
- **ABR** extend to work with ABR
- **Condition variables** for player (no busy polling)
- **Improved MPD fetching**

---

## 6. Directory Layout
```
stream-client/
 ├── src/                # all client code
 │   ├── main.c
 │   ├── mpd_parser.[ch]
 │   ├── downloader.[ch]
 │   ├── decryptor.[ch]
 │   ├── cpabe_shim.[ch]
 │   ├── buffer.[ch]
 │   ├── player.[ch]
 │   ├── logger.[ch]
 │   ├── download_queue.[ch]
 │   ├── utils.[ch]
 ├── cpabe/              # cpabe sources
 ├── stream-download/    # downloaded frames
 ├── logs/               # CSV logs
 └── Makefile
```

---

## 7. Build & Run

### Dependencies
- `libxml2-dev`, `libcurl4-openssl-dev`, `libglib2.0-dev`, `libpbc-dev`, `libgmp-dev`, `libssl-dev`

### Build
```bash
make
```

### Run
Without decryption:
```bash
./stream_client --url office-196k.mpd --buffer 2
```
With pipelined decryption:
```bash
./stream_client --url office-196k.mpd --buffer 2 --decrypt --pub pub_key --priv priv_key --pattern xyz --download-queue 10
```

---

## 8. CSV Log Format

```
# Frame Logs
frame,download_ms,decrypt_ms,buffer_count
0,12.4,5.1,1
1,11.8,0.0,2
…

# Stall Logs
stall_start_ms,duration_ms
3050.0,450.0
9200.0,300.0
```

---

## 9. Versioning / Changelog
**v2.1 (current)**
  - In-memory download/decrypt is now default and permanent
  - Optional output file writing via `--write-output` flag
  - CP-ABE vertex rebuilding replaces fallocate-based approach (uses fallback logic for compatibility)
**v2 (previous)**
  - Pipelined client: threaded download/decrypt, queue, monotonic timing
  - Logs download/decrypt/buffer/stall
  - Supports sequential mode for HTTPS and HTTP only if decryption is disabled
**v1 (older)**
  - Sequential client, in-process CP-ABE decrypt, monotonic timing

---

## 10. References
- [Libxml2](http://xmlsoft.org/)
- [libcurl](https://curl.se/libcurl/)
- [glib-2.0](https://developer.gnome.org/glib/)
- [PBC library](https://crypto.stanford.edu/pbc/)
- [CP-ABE toolkit](https://acsc.cs.utexas.edu/cpabe/)
