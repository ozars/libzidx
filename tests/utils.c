#define _POSIX_SOURCE
#include<stdio.h>
#undef _POSIX_SOURCE

#include "utils.h"

#include<assert.h>

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
    uint8_t ret;
    if (remaining == 0) {
        rndi = get_random_int();
        remaining = sizeof(rndi);
    }
    remaining--;
    ret = (rndi >> (8 * remaining)) & 0xFFU;
    return ret % 100;
}

FILE* get_random_compressed_file(uint64_t seed, long length, uint8_t *data)
{
    assert(length > 0);

    FILE *fhandle;
    long written;
    gzFile gzf;

    int z_ret;
    int fd = -1;

    gzf = NULL;

    fhandle = tmpfile();
    if (!fhandle) goto fail;

    fd = dup(fileno(fhandle));
    if (fd < 0) goto fail;

    gzf = gzdopen(fd, "wb");
    if (!gzf) goto fail;

    initialize_srandom(seed);

    for (written = 0; written < length; written++) {
        data[written] = get_random_byte();
    }

    z_ret = gzwrite(gzf, data, length);
    if (z_ret == 0) goto fail;

    z_ret = gzclose(gzf);
    if(z_ret != Z_OK) goto fail;

    return fhandle;

fail:
    if (gzf) gzclose(gzf);
    if (fd >= 0) close(fd);
    return NULL;
}

