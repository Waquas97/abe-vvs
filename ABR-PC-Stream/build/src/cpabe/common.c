// In-memory version of read_cpabe_file

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef __linux__
#  include <linux/falloc.h>
#endif
#include <glib.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include </usr/local/include/pbc/pbc.h>
#include "common.h"

EncryptPattern parse_pattern(const char* pattern) {
    EncryptPattern pat = (EncryptPattern){0,0,0};
    for (const char* p = pattern; *p; p++) {
        if (*p == 'x') pat.encrypt_x = 1;
        else if (*p == 'y') pat.encrypt_y = 1;
        else if (*p == 'z') pat.encrypt_z = 1;
    }
    return pat;
}

/* Utility: return byte size of a PLY type string */
static int type_size(const char* type) {
    if (!strcmp(type,"float")) return 4;
    if (!strcmp(type,"double")) return 8;
    if (!strcmp(type,"uchar") || !strcmp(type,"char")) return 1;
    if (!strcmp(type,"ushort")|| !strcmp(type,"short")) return 2;
    if (!strcmp(type,"uint")  || !strcmp(type,"int")) return 4;
    if (!strcmp(type,"ulong") || !strcmp(type,"long")) return 8;
    die("Unsupported PLY type: %s\n", type);
    return 0;
}

/* Process input PLY: strip coords, write reduced header+vertices,
   return payload (orig values to encrypt): [uint32 datalen][coords...] */


GByteArray* restore_ply_with_coords(
    GByteArray* reduced_ply_buf, // in-memory reduced PLY buffer
    const char* out_file,        // output filename (optional)
    GByteArray* decvals,         // decrypted coords payload
    EncryptPattern pat
)
{
    // Parse header and vertex layout from reduced_ply_buf
    const char* data = (const char*)reduced_ply_buf->data;
    size_t buflen = reduced_ply_buf->len;
    size_t pos = 0;

    char line[256];
    int vcount = 0;
    typedef struct { char name[32]; char type[16]; int size; int is_stripped; } PlyProp;
    int prop_capacity = 16;
    PlyProp* props = (PlyProp*)malloc(prop_capacity * sizeof(PlyProp));
    int prop_count = 0;
    int full_stride = 0, reduced_stride = 0;

    GString* header = g_string_new("");
    FILE* out = NULL;
    if (out_file) {
        out = fopen(out_file, "wb");
        if (!out) {
            fprintf(stderr, "Failed to open output file: %s\n", out_file);
        }
    }
    // Read header lines from buffer
    while (pos < buflen) {
        // Find next line
        size_t line_start = pos;
        char* endl = memchr(data + pos, '\n', buflen - pos);
        size_t line_len = endl ? (size_t)(endl - (data + pos)) : (buflen - pos);
        size_t copy_len = line_len < 255 ? line_len : 255;
        memcpy(line, data + pos, copy_len);
        line[copy_len] = 0;
        g_string_append(header, line);
        g_string_append_c(header, '\n');
        if (out) {
            fwrite(line, 1, copy_len, out);
            fwrite("\n", 1, 1, out);
        }
        if (!strncmp(line, "element vertex", 14)) {
            sscanf(line, "element vertex %d", &vcount);
        } else if (!strncmp(line, "property", 8)) {
            char type[32], name[32];
            if (sscanf(line, "property %31s %31s", type, name) == 2) {
                if (prop_count == prop_capacity) {
                    prop_capacity *= 2;
                    props = (PlyProp*)realloc(props, prop_capacity * sizeof(PlyProp));
                }
                PlyProp* p = &props[prop_count++];
                strcpy(p->name, name);
                strcpy(p->type, type);
                p->size = type_size(type);
                p->is_stripped =
                    ((!strcmp(p->name, "x") && pat.encrypt_x) ||
                     (!strcmp(p->name, "y") && pat.encrypt_y) ||
                     (!strcmp(p->name, "z") && pat.encrypt_z));
                full_stride += p->size;
                if (!p->is_stripped) reduced_stride += p->size;
            }
        }
        if (strstr(line, "end_header")) {
            pos += (endl ? (size_t)(endl - (data + pos)) + 1 : buflen - pos);
            break;
        }
        pos += (endl ? (size_t)(endl - (data + pos)) + 1 : buflen - pos);
    }

    const int strip_per_vertex = full_stride - reduced_stride;
    const size_t expect = (size_t)vcount * (size_t)strip_per_vertex;
    if (decvals->len < 4) die("decvals too small\n");
    guint32 datalen = 0;
    size_t payload_off = 0;
    memcpy(&datalen, decvals->data + payload_off, 4);
    payload_off += 4;
    guint8* coordbuf = decvals->data + payload_off;
    if ((size_t)datalen != expect) {
        fprintf(stderr, "coord payload len (%u) != expected (%zu)\n", datalen, expect);
        // Optionally die here if strict
    }

    GByteArray* ply_buf = g_byte_array_new();
    g_byte_array_append(ply_buf, (guint8*)header->str, header->len);

    // Build coalesced segments for final full row
    typedef struct { unsigned char src; int bytes; int red_base; int coord_base; } Seg;
    int seg_capacity = 8;
    Seg* segs = (Seg*)malloc(seg_capacity * sizeof(Seg));
    int nseg = 0;
    int red_base = 0, coord_base = 0;
    int curr_src = -1, curr_bytes = 0, curr_red_base = 0, curr_coord_base = 0;
    for (int j = 0; j < prop_count; j++) {
        int src = props[j].is_stripped ? 1 : 0;
        int sz  = props[j].size;
        if (src == 0) { /* RED */
            if (curr_src == src) {
                curr_bytes += sz;
            } else {
                if (curr_src != -1) {
                    if (nseg == seg_capacity) {
                        seg_capacity *= 2;
                        segs = (Seg*)realloc(segs, seg_capacity * sizeof(Seg));
                    }
                    segs[nseg++] = (Seg){ (unsigned char)curr_src, curr_bytes, curr_red_base, curr_coord_base };
                }
                curr_src = 0;
                curr_bytes = sz;
                curr_red_base = red_base;
                curr_coord_base = 0;
            }
            red_base += sz;
        } else { /* COORD */
            if (curr_src == src) {
                curr_bytes += sz;
            } else {
                if (curr_src != -1) {
                    if (nseg == seg_capacity) {
                        seg_capacity *= 2;
                        segs = (Seg*)realloc(segs, seg_capacity * sizeof(Seg));
                    }
                    segs[nseg++] = (Seg){ (unsigned char)curr_src, curr_bytes, curr_red_base, curr_coord_base };
                }
                curr_src = 1;
                curr_bytes = sz;
                curr_red_base = 0;
                curr_coord_base = coord_base;
            }
            coord_base += sz;
        }
    }
    if (curr_src != -1) {
        if (nseg == seg_capacity) {
            seg_capacity *= 2;
            segs = (Seg*)realloc(segs, seg_capacity * sizeof(Seg));
        }
        segs[nseg++] = (Seg){ (unsigned char)curr_src, curr_bytes, curr_red_base, curr_coord_base };
    }

    const int BATCH_VERTS = 4096;
    const size_t red_chunk_bytes  = (size_t)reduced_stride * (size_t)BATCH_VERTS;
    const size_t full_chunk_bytes = (size_t)full_stride    * (size_t)BATCH_VERTS;
    unsigned char* red_chunk  = (reduced_stride > 0) ? (unsigned char*)malloc(red_chunk_bytes)  : NULL;
    unsigned char* full_chunk = (unsigned char*)malloc(full_chunk_bytes);
    if (!full_chunk || (reduced_stride && !red_chunk)) die("OOM\n");

    int done = 0;
    size_t batch_coord_base = 0;
    // Vertex data starts after header
    size_t vertex_data_off = pos;
    while (done < vcount) {
        int this_batch = vcount - done;
        if (this_batch > BATCH_VERTS) this_batch = BATCH_VERTS;
        // Read reduced vertex rows from buffer
        if (reduced_stride > 0) {
            const size_t need = (size_t)this_batch * (size_t)reduced_stride;
            if (vertex_data_off + need > buflen)
                die("Unexpected EOF while reading reduced vertex batch at %d\n", done);
            memcpy(red_chunk, data + vertex_data_off, need);
            vertex_data_off += need;
        }
        for (int i = 0; i < this_batch; i++) {
            unsigned char* outrow = full_chunk + (size_t)i * (size_t)full_stride;
            const unsigned char* redrow =
                (reduced_stride > 0) ? (red_chunk + (size_t)i * (size_t)reduced_stride) : NULL;
            const unsigned char* coordrow =
                coordbuf + batch_coord_base + (size_t)i * (size_t)strip_per_vertex;
            int out_off = 0;
            for (int s = 0; s < nseg; s++) {
                if (segs[s].src == 0) {
                    memcpy(outrow + out_off, redrow + segs[s].red_base, (size_t)segs[s].bytes);
                } else {
                    memcpy(outrow + out_off, coordrow + segs[s].coord_base, (size_t)segs[s].bytes);
                }
                out_off += segs[s].bytes;
            }
        }
        g_byte_array_append(ply_buf, full_chunk, (size_t)full_stride * (size_t)this_batch);
        if (out) {
            fwrite(full_chunk, (size_t)full_stride, (size_t)this_batch, out);
        }
        done += this_batch;
        batch_coord_base += (size_t)this_batch * (size_t)strip_per_vertex;
    }
    if (out) fclose(out);
    if (red_chunk)  free(red_chunk);
    free(full_chunk);
    free(segs);
    free(props);
    g_string_free(header, 1);
    return ply_buf;
}
GByteArray* restore_stripped_rebuild(
    GByteArray* reduced_ply_buf,
    const char* out_file,
    GByteArray* decvals,
    EncryptPattern pat
){
    return restore_ply_with_coords(reduced_ply_buf, out_file, decvals, pat);
}

/* ======================= AES helpers (unchanged) ======================= */

void init_aes( element_t k, int enc, AES_KEY* key, unsigned char* iv )
{
  int key_len;
  unsigned char* key_buf;

  key_len = element_length_in_bytes(k) < 17 ? 17 : element_length_in_bytes(k);
  key_buf = (unsigned char*) malloc(key_len);
  element_to_bytes(key_buf, k);

  if( enc )
    AES_set_encrypt_key(key_buf + 1, 128, key);
  else
    AES_set_decrypt_key(key_buf + 1, 128, key);
  free(key_buf);

  memset(iv, 0, 16);
}

GByteArray* aes_128_cbc_encrypt( GByteArray* pt, element_t k )
{
  AES_KEY key;
  unsigned char iv[16];
  GByteArray* ct;
  guint8 len[4];
  guint8 zero;

  init_aes(k, 1, &key, iv);

  /* stuff in real length (big endian) before padding */
  len[0] = (pt->len & 0xff000000)>>24;
  len[1] = (pt->len & 0xff0000)>>16;
  len[2] = (pt->len & 0xff00)>>8;
  len[3] = (pt->len & 0xff)>>0;
  g_byte_array_prepend(pt, len, 4);

  /* pad out to multiple of 16 bytes */
  zero = 0;
  while( pt->len % 16 )
    g_byte_array_append(pt, &zero, 1);

  ct = g_byte_array_new();
  g_byte_array_set_size(ct, pt->len);

  AES_cbc_encrypt(pt->data, ct->data, pt->len, &key, iv, AES_ENCRYPT);

  return ct;
}

GByteArray* aes_128_cbc_decrypt( GByteArray* ct, element_t k )
{
  AES_KEY key;
  unsigned char iv[16];
  GByteArray* pt;
  unsigned int len;

  init_aes(k, 0, &key, iv);

  pt = g_byte_array_new();
  g_byte_array_set_size(pt, ct->len);

  AES_cbc_encrypt(ct->data, pt->data, ct->len, &key, iv, AES_DECRYPT);

  /* get real length (first 4 bytes, big-endian), then drop the 4-byte header */
  len = 0;
  len = len
    | ((pt->data[0])<<24) | ((pt->data[1])<<16)
    | ((pt->data[2])<<8)  | ((pt->data[3])<<0);
  g_byte_array_remove_index(pt, 0);
  g_byte_array_remove_index(pt, 0);
  g_byte_array_remove_index(pt, 0);
  g_byte_array_remove_index(pt, 0);

  g_byte_array_set_size(pt, len);

  return pt;
}

/* ======================= File helpers (unchanged) ======================= */

FILE* fopen_read_or_die( char* file )
{
    FILE* f;
    if( !(f = fopen(file, "r")) )
        die("can't read file: %s\n", file);
    return f;
}

FILE* fopen_write_or_die( char* file )
{
    FILE* f;
    if( !(f = fopen(file, "a")) )
        die("can't write file: %s\n", file);
    return f;
}

GByteArray* suck_file( char* file )
{
    FILE* f;
    GByteArray* a;
    struct stat s;

    a = g_byte_array_new();
    if (stat(file, &s) != 0) die("stat failed for %s\n", file);
    g_byte_array_set_size(a, (gsize)s.st_size);

    f = fopen_read_or_die(file);
    if (fread(a->data, 1, (size_t)s.st_size, f) != (size_t)s.st_size)
        die("short read on %s\n", file);
    fclose(f);

    return a;
}

char* suck_file_str( char* file )
{
    GByteArray* a;
    char* s;
    unsigned char zero;

    a = suck_file(file);
    zero = 0;
    g_byte_array_append(a, &zero, 1);
    s = (char*) a->data;
    g_byte_array_free(a, 0);

    return s;
}

char* suck_stdin()
{
    GString* s;
    char* r;
    int c;

    s = g_string_new("");
    while( (c = fgetc(stdin)) != EOF )
        g_string_append_c(s, c);

    r = (char*)s->str;
    g_string_free(s, 0);

    return r;
}

void spit_file( char* file, GByteArray* b, int free_it )
{
    FILE* f = fopen_write_or_die(file);
    fwrite(b->data, 1, b->len, f);
    fclose(f);

    if( free_it )
        g_byte_array_free(b, 1);
}

void die(char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(1);
}

void parse_cpabe_buffer(GByteArray* buffer, GByteArray** cph_buf, int* file_len, GByteArray** aes_buf)
{

    int i, len;
    const char* data = (const char*)buffer->data;
    size_t buflen = buffer->len;
    size_t pos = 0;

    *cph_buf = g_byte_array_new();
    *aes_buf = g_byte_array_new();


    // ---- parse header to get vertex_count, full_stride, header_end, and sizes of x/y/z ----
    int vcount = 0;
    int full_stride = 0;
    size_t header_end = 0;
    int sz_x = 0, sz_y = 0, sz_z = 0;
    int in_vertex = 0;
    while (pos < buflen) {
        // Find next line
        size_t line_start = pos;
        char* endl = memchr(data + pos, '\n', buflen - pos);
        size_t line_len = endl ? (size_t)(endl - (data + pos)) : (buflen - pos);
        char line[256] = {0};
        size_t copy_len = line_len < 255 ? line_len : 255;
        memcpy(line, data + pos, copy_len);
        line[copy_len] = 0;

        if (!strncmp(line, "element vertex", 14)) {
            sscanf(line, "element vertex %d", &vcount);
            in_vertex = 1;
        } else if (!strncmp(line, "element ", 8)) {
            in_vertex = 0;
        } else if (in_vertex && !strncmp(line, "property", 8)) {
            char t[32], n[32];
            if (sscanf(line, "property %31s %31s", t, n) == 2) {
                int ts = type_size(t);
                full_stride += ts;
                if      (!strcmp(n,"x")) sz_x = ts;
                else if (!strcmp(n,"y")) sz_y = ts;
                else if (!strcmp(n,"z")) sz_z = ts;
            }
        }
        if (strstr(line, "end_header")) {
            header_end = (endl ? (size_t)(endl - data) + 1 : buflen);
            break;
        }
        pos += (endl ? (size_t)(endl - (data + pos)) + 1 : buflen - pos);
    }
    if (vcount <= 0 || full_stride <= 0 || header_end <= 0)
        die("parse_cpabe_buffer: bad PLY header\n");

    EncryptPattern pat = parse_pattern(pattern_arg ? pattern_arg : "");
    int stripped_per_vertex = 0;
    if (pat.encrypt_x) stripped_per_vertex += sz_x;
    if (pat.encrypt_y) stripped_per_vertex += sz_y;
    if (pat.encrypt_z) stripped_per_vertex += sz_z;
    int reduced_stride = full_stride - stripped_per_vertex;
    if (reduced_stride < 0)
        die("parse_cpabe_buffer: negative reduced stride (bad scheme?)\n");

    // ---- direct seek to trailer: header_end + vcount * reduced_stride ----
    size_t trailer_off = header_end + (size_t)vcount * (size_t)reduced_stride;
    if (trailer_off >= buflen)
        die("parse_cpabe_buffer: trailer offset out of bounds\n");

    // verify marker
    const char* marker = "comment encrypted";
    size_t mlen = strlen(marker);
    if (trailer_off+mlen > buflen || memcmp(data+trailer_off, marker, mlen) != 0)
        die("parse_cpabe_buffer: trailer marker not found at computed offset\n");
    // Set position to start of length fields after marker
    pos = trailer_off + mlen;

    // Skip 4 bytes for file_len (unused)
    pos += 4;

    // Read aes_buf
    len = 0; for (i = 3; i >= 0; i--) len |= ((unsigned char)data[pos++]) << (i * 8);
    g_byte_array_set_size(*aes_buf, (gsize)len);
    if (pos+len > buflen)
        die("parse_cpabe_buffer: failed to read aes_buf\n");
    memcpy((*aes_buf)->data, data+pos, len);
    pos += len;

    // Read cph_buf
    len = 0; for (i = 3; i >= 0; i--) len |= ((unsigned char)data[pos++]) << (i * 8);
    g_byte_array_set_size(*cph_buf, (gsize)len);
    if (pos+len > buflen)
        die("parse_cpabe_buffer: failed to read cph_buf\n");
    memcpy((*cph_buf)->data, data+pos, len);
    pos += len;
}
