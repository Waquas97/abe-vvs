#define PACKAGE_NAME "cpabe"
#define PACKAGE_VERSION "1.0"

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <pbc.h>
#include <pbc_random.h>

#include "bswabe.h"
#include "common.h"

char* usage =
"Usage: cpabe-dec [OPTION ...] PUB_KEY PRIV_KEY FILE SCHEME\n"
"\n"
"Decrypt FILE using private key PRIV_KEY and assuming public key\n"
"PUB_KEY. SCHEME specifies which coordinate(s) were encrypted.\n"
"\n"
"If the name of FILE is X.cpabe, the decrypted file will be written\n"
"as X and FILE will be removed. Otherwise the file will be decrypted\n"
"in place. Use of the -o option overrides this behavior.\n"
"\n"
"SCHEME must be one of: x, y, z, xy, yz, xyz\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n\n"
" -h, --help               print this message\n\n"
" -v, --version            print version information\n\n"
" -k, --keep-input-file    don't delete original file\n\n"
" -o, --output FILE        write output to FILE\n\n"
" -d, --deterministic      use deterministic \"random\" numbers\n"
"                          (only for debugging)\n\n";

char* pub_file   = 0;
char* prv_file   = 0;
char* in_file    = 0;
char* out_file   = 0;
int   keep       = 0;
char* pattern_arg = NULL; // required

static void parse_args( int argc, char** argv )
{
    int i;
    int positional = 0;

    for( i = 1; i < argc; i++ )
        if(      !strcmp(argv[i], "-h") || !strcmp(argv[i], "--help") )
        {
            printf("%s", usage);
            exit(0);
        }
        else if( !strcmp(argv[i], "-v") || !strcmp(argv[i], "--version") )
        {
            printf(CPABE_VERSION, "-dec");
            exit(0);
        }
        else if( !strcmp(argv[i], "-k") || !strcmp(argv[i], "--keep-input-file") )
        {
            keep = 1;
        }
        else if( !strcmp(argv[i], "-o") || !strcmp(argv[i], "--output") )
        {
            if( ++i >= argc )
                die(usage);
            else
                out_file = argv[i];
        }
        else if( !strcmp(argv[i], "-d") || !strcmp(argv[i], "--deterministic") )
        {
            pbc_random_set_deterministic(0);
        }
        else {
            // positional args
            switch (positional) {
                case 0: pub_file = argv[i]; break;
                case 1: prv_file = argv[i]; break;
                case 2: in_file  = argv[i]; break;
                case 3: pattern_arg = argv[i]; break;
                default: die("Too many positional arguments!\n%s", usage);
            }
            positional++;
        }

    if( !pub_file || !prv_file || !in_file || !pattern_arg )
        die("Missing arguments!\n%s", usage);

    if( !out_file )
    {
        if(  strlen(in_file) > 6 &&
                !strcmp(in_file + strlen(in_file) - 6, ".cpabe") )
            out_file = g_strndup(in_file, strlen(in_file) - 6);
        else
            out_file = strdup(in_file);
    }

    if( keep && !strcmp(in_file, out_file) )
        die("cannot keep input file when decrypting file in place (try -o)\n");
}

int main( int argc, char** argv )
{
    bswabe_pub_t* pub;
    bswabe_prv_t* prv;
    int file_len;
    GByteArray* aes_buf;
    GByteArray* pt_payload;    /* [uint32 datalen][coords...] */
    GByteArray* cph_buf;
    bswabe_cph_t* cph;
    element_t m;

    parse_args(argc, argv);

    /* validate scheme */
    EncryptPattern pat = parse_pattern(pattern_arg);
    if (!(pat.encrypt_x || pat.encrypt_y || pat.encrypt_z))
        die("Invalid SCHEME. Use one of: x, y, z, xy, yz, xyz\n");

    pub = bswabe_pub_unserialize(suck_file(pub_file), 1);
    prv = bswabe_prv_unserialize(pub, suck_file(prv_file), 1);

    /* read trailer (file_len, aes_buf, cph_buf) */
    read_cpabe_file(in_file, &cph_buf, &file_len, &aes_buf);

    /* recover session key */
    cph = bswabe_cph_unserialize(pub, cph_buf, 1);
    if( !bswabe_dec(pub, prv, cph, m) )
        die("%s", bswabe_error());
    bswabe_cph_free(cph);

    /* decrypt payload to [uint32 datalen][coords...] */
    pt_payload = aes_128_cbc_decrypt(aes_buf, m);
    g_byte_array_set_size(pt_payload, file_len);
    g_byte_array_free(aes_buf, 1);
    element_clear(m);

    /* Always use portable fallback: rebuild to out_file */
    restore_stripped_rebuild(in_file, out_file, pt_payload, pat);
    if (!keep)
        unlink(in_file);

    g_byte_array_free(pt_payload, 1);

    return 0;
}
