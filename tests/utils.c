#include "utils.h"

#include<assert.h>
#include<stdio.h>

#include<zlib.h>
#include<pcg_variants.h>

void initialize_srandom(uint64_t seed)
{
    pcg32_srandom(seed, seed);
}

uint32_t get_random_int()
{
    return pcg32_random();
}

uint8_t get_random_byte()
{
    static int remaining = 0;
    static int rndi;
    if (remaining == 0) {
        rndi = get_random_int();
        remaining = sizeof(rndi);
    }
    remaining--;
    return rndi | (0xFF << (8 * remaining));
}

const char* get_random_compressed_file(uint64_t seed, long length)
{
    assert(length > 0);

    const char *fname;
    uint8_t buffer[8192];
    int buffer_len;
    int written;
    long remaining;
    gzFile gzf;

    int z_ret;

    gzf = NULL;

    fname = tmpnam(NULL);
    if (!fname) goto fail;

    gzf = gzopen(fname, "wb");
    if (!gzf) goto fail;

    initialize_srandom(seed);

    for (remaining = length; remaining > 0; remaining -= buffer_len) {
        if (remaining < sizeof(buffer)) {
            buffer_len = remaining;
        } else {
            buffer_len = sizeof(buffer);
        }
        for (written = 0; written < buffer_len; written++) {
            buffer[written] = get_random_byte();
        }
        z_ret = gzwrite(gzf, buffer, buffer_len);
        if (z_ret == 0) goto fail;
    }

    z_ret = gzclose(gzf);
    if(z_ret != Z_OK) goto fail;

    return fname;

fail:
    if (gzf) gzclose(gzf);
    return NULL;
}

