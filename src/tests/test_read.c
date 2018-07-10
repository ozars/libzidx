#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>

#include<zidx.h>

int block_callback(void *context, zidx_index *index,
                   zidx_checkpoint_offset *offset)
{
    static int cnt = 0;
    printf("%d) COMP: %jd,%d / UNCOMP: %jd\n", ++cnt,
                                           offset->comp, offset->comp_bits,
                                           offset->uncomp);
    zidx_checkpoint *pt = malloc(sizeof(zidx_checkpoint));
    if(zidx_create_checkpoint(index, pt, offset)) {
        printf("ERROR!!!\n");
        return -1;
    }
    if(zidx_add_checkpoint(index, pt)) {
        printf("ERROR 2!!!\n");
        return -2;
    }
    return 0;
}

int main()
{
    zidx_index index;
    FILE *f;
    unsigned char buffer[16] = "omer";
    int z_ret;

    f = fopen("test.gz", "rb");
    if(!f) return -1;

    zidx_comp_stream stream = {
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
        z_ret = zidx_read_advanced(&index, buffer, sizeof(buffer),
                                   block_callback, NULL);
        printf("Read length: %d\n", z_ret);
        if(z_ret >= 0) {
            printf("%.*s\n", z_ret, buffer);
        }
    } while(z_ret > 0);

    return 0;
}
