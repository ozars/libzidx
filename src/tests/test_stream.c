#include<stdio.h>
#include<string.h>
#include<assert.h>

#include<zidx.h>

int main()
{
    FILE *f = fopen("testfile", "w");
    const char test_str[] = "It works!12567890";
    char buf[5] = "00000";
    int buf_len = sizeof(buf);
    int read_len;

    assert(f);
    assert(zidx_raw_file_write(f, test_str, sizeof(test_str)) == sizeof(test_str));
    assert(zidx_raw_file_eof(f) == 0);
    assert(zidx_raw_file_error(f) == 0);
    assert(zidx_raw_file_tell(f) == sizeof(test_str));
    assert(zidx_raw_file_seek(f, 0, ZIDX_SEEK_SET) == 0);
    assert(zidx_raw_file_tell(f) == 0);

    fclose(f);
    f = fopen("testfile", "r");
    assert(f);

    while(1) {
        read_len = zidx_raw_file_read(f, buf, buf_len);
        for(int i = 0; i < read_len; i++)
        {
            printf("%02x ", buf[i]);
        }
        printf("\n");
        assert(!zidx_raw_file_error(f));
        if(zidx_raw_file_eof(f)) break;
    }

    return 0;
}
