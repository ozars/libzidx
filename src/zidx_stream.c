#include "zidx_stream.h"

#include<stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

zidx_comp_stream* zidx_create_comp_file(FILE *file)
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

zidx_index_stream* zidx_create_index_file(FILE *file)
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
    off_t length;
    off_t saved_pos;

    saved_pos = zidx_raw_file_tell(file);
    if(saved_pos < 0) goto fail;

    if(zidx_raw_file_seek(file, 0, SEEK_END) < 0) goto cleanup;

    length = zidx_raw_file_tell(file);
    if(length < 0) goto cleanup;

    if(zidx_raw_file_seek(file, saved_pos, SEEK_SET) < 0) return -2;

    return length;

cleanup:
    if(zidx_raw_file_seek(file, saved_pos, SEEK_SET) < 0) return -2;
fail:
    return -1;
}

#ifdef __cplusplus
} // extern "C"
#endif
