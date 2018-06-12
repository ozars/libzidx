#include<stdio.h>
#include<string.h>
#include<assert.h>

#include<zidx.h>

int main()
{
    zidx_index index;
    FILE *f;
    unsigned char buffer[16] = "omer";
    int z_ret;

    f = fopen("test.gz", "rb");
    if(!f) return -1;

    zidx_compressed_stream stream = {
        zidx_raw_file_read,
        zidx_raw_file_seek,
        zidx_raw_file_tell,
        zidx_raw_file_eof,
        zidx_raw_file_error,
        zidx_raw_file_length,
        (void*) f
    };

    z_ret = zidx_index_init(&index, &stream);
    printf("%d\n", z_ret);
    do {
        z_ret = zidx_gzip_read(&index, buffer, sizeof(buffer));
        printf("Read length: %d\n", z_ret);
        if(z_ret >= 0) {
            printf("%.*s\n", z_ret, buffer);
        }
    } while(z_ret > 0);

    return 0;
}
