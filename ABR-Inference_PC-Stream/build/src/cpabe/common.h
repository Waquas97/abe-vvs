// In-memory version of read_cpabe_file
void parse_cpabe_buffer(GByteArray* buffer, GByteArray** cph_buf, int* file_len, GByteArray** aes_buf);
#pragma once
#include <glib.h>
#include <pbc.h>

/**
 * Simple flags: which coordinates to encrypt/strip.
 */
typedef struct {
    int encrypt_x;
    int encrypt_y;
    int encrypt_z;
} EncryptPattern;

EncryptPattern parse_pattern(const char* pattern);

/**
 * A single property definition from PLY header.
 */
typedef struct {
    char name[32];     // e.g., "x", "red", "nx"
    char type[16];     // e.g., "float", "double", "uchar"
    int size;          // size in bytes (4,8,1,…)
    int strip;         // 1 if stripped, 0 if retained
} PlyProperty;

/**
 * Main PLY element definition (only vertices matter here).
 */
typedef struct {
    int count;            // number of vertices
    int prop_count;       // number of properties
    PlyProperty* props;   // array
    int stride_orig;      // bytes per vertex before stripping
    int stride_stripped;  // bytes per vertex after stripping
} PlyVertexLayout;

/**
 * Encrypt-time processing: read header, strip coords, write reduced PLY,
 * and return a byte array containing payload (original values to encrypt).
 */


/**
 * Decrypt-time processing (legacy restore path).
 */
GByteArray* restore_ply_with_coords(
    GByteArray* reduced_ply_buf,
    const char* out_file,
    GByteArray* decvals,
    EncryptPattern pat
);


/* Portable fallback: rebuild a full PLY by streaming reduced rows and
 * interleaving decrypted prefixes, writing to 'out_ply'. Slower but works everywhere.
 */
GByteArray* restore_stripped_rebuild(
    GByteArray* reduced_ply_buf,
    const char* out_file,
    GByteArray* decvals,
    EncryptPattern pat
);

/* --- already in your code --- */
char* suck_file_str(char* file);
char* suck_stdin();
GByteArray* suck_file(char* file);
void spit_file(char* file, GByteArray* b, int free);

void read_cpabe_file(char* file, GByteArray** cph_buf, int* file_len, GByteArray** aes_buf);
void die(char* fmt, ...);

GByteArray* aes_128_cbc_encrypt(GByteArray* pt, element_t k);
GByteArray* aes_128_cbc_decrypt(GByteArray* ct, element_t k);

extern char* pattern_arg;

#define CPABE_VERSION PACKAGE_NAME "%s " PACKAGE_VERSION "\n" \
"\n" \
"Parts Copyright (C) 2006, 2007 John Bethencourt and SRI International.\n" \
"This is free software released under the GPL, see the source for copying\n" \
"conditions. There is NO warranty; not even for MERCHANTABILITY or FITNESS\n" \
"FOR A PARTICULAR PURPOSE.\n" \
"\n" \
"Report bugs to John Bethencourt <bethenco@cs.berkeley.edu>.\n"

