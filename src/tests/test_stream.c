#include<stdio.h>
#include<string.h>
#include<assert.h>

#include<gzidx.h>

int main()
{
    FILE *f = tmpfile();
    const char test_str[] = "It works!";
    char read_str[sizeof(test_str)] = "000000000";
    int str_len = sizeof(test_str);

    assert(gzidx_raw_file_write(f, test_str, str_len) == str_len);
    assert(gzidx_raw_file_eof(f) == 0);
    assert(gzidx_raw_file_error(f) == 0);
    assert(gzidx_raw_file_tell(f) == str_len);
    assert(gzidx_raw_file_seek(f, 0, GZIDX_SEEK_SET) == 0);
    assert(gzidx_raw_file_tell(f) == 0);

    assert(gzidx_raw_file_read(f, read_str, str_len) == str_len);
    assert(gzidx_raw_file_eof(f) == 0);
    assert(gzidx_raw_file_error(f) == 0);
    assert(gzidx_raw_file_read(f, read_str, 1) == 0);
    assert(gzidx_raw_file_error(f) == 0);
    assert(gzidx_raw_file_eof(f) > 0);

    fclose(f);
    return 0;
}
