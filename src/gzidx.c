#include "gzidx.h"

#ifdef __cplusplus
extern "C" {
#endif


int gzidx_import_advanced(gzidx_index *index,
                          const gzidx_gzip_index_stream *stream,
                          gzidx_import_filter_callback filter,
                          void *filter_context) { return 0; }

int gzidx_export_advanced(const gzidx_index *index,
                          const gzidx_gzip_index_stream *stream,
                          gzidx_export_filter_callback filter,
                          void *filter_context) { return 0; }

int gzidx_import(gzidx_index *index, FILE* input_index_file)
{
    const gzidx_gzip_index_stream input_stream = {
        gzidx_raw_file_read,
        gzidx_raw_file_write,
        gzidx_raw_file_seek,
        gzidx_raw_file_tell,
        gzidx_raw_file_eof,
        gzidx_raw_file_error,
        (void*) input_index_file
    };
    return gzidx_import_advanced(index, &input_stream, NULL, NULL);
}

int gzidx_export(const gzidx_index *index, FILE* output_index_file)
{
    const gzidx_gzip_index_stream output_stream = {
        gzidx_raw_file_read,
        gzidx_raw_file_write,
        gzidx_raw_file_seek,
        gzidx_raw_file_tell,
        gzidx_raw_file_eof,
        gzidx_raw_file_error,
        (void*) output_index_file
    };
    return gzidx_export_advanced(index, &output_stream, NULL, NULL);
}

size_t gzidx_raw_file_read(void *file, void *buffer, size_t nbytes)
{
    return fread(buffer, 1, nbytes, (FILE*) file);
}

size_t gzidx_raw_file_write(void *file, const void *buffer, size_t nbytes)
{
    return fwrite(buffer, 1, nbytes, (FILE*) file);
}

int gzidx_raw_file_seek(void *file, off_t offset, int whence)
{
    switch(whence)
    {
        case GZIDX_SEEK_SET:
            return fseek((FILE*) file, offset, SEEK_SET);
        case GZIDX_SEEK_CUR:
            return fseek((FILE*) file, offset, SEEK_CUR);
        case GZIDX_SEEK_END:
            return fseek((FILE*) file, offset, SEEK_END);
    }
    return fseek((FILE*) file, offset, whence);
}

off_t gzidx_raw_file_tell(void *file)
{
    return ftell((FILE*) file);
}

int gzidx_raw_file_eof(void *file)
{
    return feof((FILE*) file);
}

int gzidx_raw_file_error(void *file)
{
    return ferror((FILE*) file);
}

off_t gzidx_raw_file_length(void *file)
{
    off_t length;
    off_t saved_pos;

    saved_pos = gzidx_raw_file_tell(file);
    if(saved_pos < 0) goto fail;

    if(gzidx_raw_file_seek(file, 0, SEEK_END) < 0) goto cleanup;

    length = gzidx_raw_file_tell(file);
    if(length < 0) goto cleanup;

    if(gzidx_raw_file_seek(file, saved_pos, SEEK_SET) < 0) return -2;

    return length;

cleanup:
    if(gzidx_raw_file_seek(file, saved_pos, SEEK_SET) < 0) return -2;
fail:
    return -1;
}

#ifdef __cplusplus
} // extern "C"
#endif
