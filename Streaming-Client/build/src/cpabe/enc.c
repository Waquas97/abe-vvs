#define PACKAGE_NAME "cpabe"
#define PACKAGE_VERSION "1.0"

#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <pbc.h>
#include <pbc_random.h>

#include "bswabe.h"
#include "common.h"
#include "policy_lang.h"

char* usage =
"Usage: cpabe-enc [OPTION ...] PUB_KEY FILE POLICY SCHEME\n"
"\n"
"Encrypt FILE under the decryption policy POLICY using public key\n"
"PUB_KEY. SCHEME specifies which coordinate(s) to encrypt/strip.\n"
"\n"
"The encrypted file will be written to FILE.cpabe unless the -o option\n"
"is used. The original file will be removed unless -k is given.\n"
"\n"
"SCHEME must be one of: x, y, z, xy, yz, xyz\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n\n"
" -h, --help               print this message\n\n"
" -v, --version            print version information\n\n"
" -k, --keep-input-file    don't delete original file\n\n"
" -o, --output FILE        write resulting file to FILE\n\n"
" -d, --deterministic      use deterministic \"random\" numbers\n"
"                          (only for debugging)\n\n";

char* pub_file = NULL;
char* in_file = NULL;
char* out_file = NULL;
int   keep = 0;
char* policy = NULL;
char* pattern_arg = NULL;

static void parse_args(int argc, char** argv) {
    int i;
    int positional = 0; // PUB_KEY, FILE, POLICY, SCHEME

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                printf("%s", usage);
                exit(0);
            }
            else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
                printf(CPABE_VERSION, "-enc");
                exit(0);
            }
            else if (!strcmp(argv[i], "-k") || !strcmp(argv[i], "--keep-input-file")) {
                keep = 1;
            }
            else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
                if (++i >= argc)
                    die(usage);
                else
                    out_file = argv[i];
            }
            else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--deterministic")) {
                pbc_random_set_deterministic(0);
            }
            else {
                die("Unknown option: %s\n%s", argv[i], usage);
            }
        } else {
            // positional
            switch (positional) {
                case 0: pub_file = argv[i]; break;
                case 1: in_file  = argv[i]; break;
                case 2: policy   = parse_policy_lang(argv[i]); break;
                case 3: pattern_arg = argv[i]; break;
                default: die("Too many positional arguments!\n%s", usage);
            }
            positional++;
        }
    }

    if (!pub_file || !in_file)
        die("Missing PUB_KEY or FILE!\n%s", usage);

    if (!out_file)
        out_file = g_strdup_printf("%s.cpabe", in_file);

    if (!policy)
        policy = parse_policy_lang(suck_stdin());

    // SCHEME is REQUIRED
    if (!pattern_arg || !*pattern_arg)
        die("SCHEME is required and must be one of: x, y, z, xy, yz, xyz\n%s", usage);
}

int main(int argc, char** argv)
{
    bswabe_pub_t* pub;
    bswabe_cph_t* cph;
    int file_len;
    GByteArray* pt_payload;  // [uint32 datalen][coords...]
    GByteArray* cph_buf;
    GByteArray* aes_buf;
    element_t m;

    parse_args(argc, argv);

    // Validate scheme
    EncryptPattern pattern = parse_pattern(pattern_arg);
    if (!(pattern.encrypt_x || pattern.encrypt_y || pattern.encrypt_z))
        die("Invalid SCHEME. Use one of: x, y, z, xy, yz, xyz\n");

    pub = bswabe_pub_unserialize(suck_file(pub_file), 1);

    if (!(cph = bswabe_enc(pub, m, policy)))
        die("%s", bswabe_error());
    free(policy);

    cph_buf = bswabe_cph_serialize(cph);
    bswabe_cph_free(cph);

    // Strip + write reduced PLY; return plaintext payload of stripped bytes
    pt_payload = process_and_encrypt_ply(in_file, out_file, pattern);
    file_len = pt_payload->len;                 // plaintext payload length
    aes_buf  = aes_128_cbc_encrypt(pt_payload, m); // encrypt payload with session key

    g_byte_array_free(pt_payload, 1);
    element_clear(m);

    // Append trailer: marker + file_len + aes_buf + cph_buf
    write_cpabe_file(out_file, cph_buf, file_len, aes_buf);

    g_byte_array_free(cph_buf, 1);
    g_byte_array_free(aes_buf, 1);

    if (!keep)
        unlink(in_file);

    return 0;
}
