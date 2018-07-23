#include "utils.h"

#include<assert.h>
#include<stdio.h>

#include<zlib.h>
#include<pcg_variants.h>

void initialize_srandom(uint64_t seed)
{
    pcg32_srandom(seed, seed);
}

void initialize_srandom2(uint64_t seed, uint64_t skip)
{
    int i;
    pcg32_srandom(seed, skip / 4);
    for (i = 0; i < skip % 4; i++) {
        get_random_byte();
    }
}

uint32_t get_random_int()
{
    return pcg32_random();
}

uint8_t get_random_byte()
{
    static int remaining = 0;
    static uint32_t rndi;
    if (remaining == 0) {
        rndi = get_random_int();
        remaining = sizeof(rndi);
    }
    remaining--;
    return (rndi >> (8 * remaining)) & 0xFFU;
}

const char* get_random_compressed_file(uint64_t seed, long length, uint8_t *data)
{
    assert(length > 0);

    const char *fname;
    long written;
    gzFile gzf;

    int z_ret;

    gzf = NULL;

    fname = tmpnam(NULL);
    if (!fname) goto fail;

    gzf = gzopen(fname, "wb");
    if (!gzf) goto fail;

    initialize_srandom(seed);

    for (written = 0; written < length; written++) {
        data[written] = get_random_byte();
    }

    z_ret = gzwrite(gzf, data, length);
    if (z_ret == 0) goto fail;

    z_ret = gzclose(gzf);
    if(z_ret != Z_OK) goto fail;

    return fname;

fail:
    if (gzf) gzclose(gzf);
    return NULL;
}

