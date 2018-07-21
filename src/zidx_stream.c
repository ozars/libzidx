#ifndef _POSIX_SOURCE
# define _POSIX_SOURCE
#  include<stdio.h>
# undef _POSIX_SOURCE
#else
# include<stdio.h>
#endif

#include "zidx_stream.h"

#include<stdlib.h>
#include<sys/types.h>
#include<sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

zidx_comp_stream* zidx_comp_file_create(FILE *file)
{
    zidx_comp_stream *stream;

    stream = (zidx_comp_stream*) malloc(sizeof(zidx_comp_stream));
    if(stream == NULL) return NULL;

    stream->read    = zidx_raw_file_read;
    stream->seek    = zidx_raw_file_seek;
    stream->tell    = zidx_raw_file_tell;
    stream->eof     = zidx_raw_file_eof;
    stream->error   = zidx_raw_file_error;
    stream->length  = zidx_raw_file_length;
    stream->context = (void*) file;

    return stream;
}

zidx_index_stream* zidx_index_file_create(FILE *file)
{
    zidx_index_stream *stream;

    stream = (zidx_index_stream*) malloc(sizeof(zidx_index_stream));
    if(stream == NULL) return NULL;

    stream->read    = zidx_raw_file_read;
    stream->write   = zidx_raw_file_write;
    stream->seek    = zidx_raw_file_seek;
    stream->tell    = zidx_raw_file_tell;
    stream->eof     = zidx_raw_file_eof;
    stream->error   = zidx_raw_file_error;
    stream->context = (void*) file;

    return stream;
}

int zidx_raw_file_read(void *file, uint8_t *buffer, int nbytes)
{
    return fread(buffer, 1, nbytes, (FILE*) file);
}

int zidx_raw_file_write(void *file, const uint8_t *buffer,
                        int nbytes)
{
    return fwrite(buffer, 1, nbytes, (FILE*) file);
}

int zidx_raw_file_seek(void *file, off_t offset, int whence)
{
    switch(whence)
    {
        case ZX_SEEK_SET:
            return fseek((FILE*) file, offset, SEEK_SET);
        case ZX_SEEK_CUR:
            return fseek((FILE*) file, offset, SEEK_CUR);
        case ZX_SEEK_END:
            return fseek((FILE*) file, offset, SEEK_END);
    }
    return fseek((FILE*) file, offset, whence);
}

off_t zidx_raw_file_tell(void *file)
{
    return ftell((FILE*) file);
}

int zidx_raw_file_eof(void *file)
{
    return feof((FILE*) file);
}

int zidx_raw_file_error(void *file)
{
    return ferror((FILE*) file);
}

off_t zidx_raw_file_length(void *file)
{
    struct stat s;
    int fd;
    int ret;

    fd = fileno(file);
    if (fd == -1) return -1;

    ret = fstat(fd, &s);
    if (ret < 0) return -2;

    return s.st_size;
}

#ifdef __cplusplus
} // extern "C"
#endif
