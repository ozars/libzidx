#include "gzidx.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    return gzidx_import_advanced(index, &input_stream, NULL);
}

int gzidx_export(gzidx_index *index, FILE* output_index_file)
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
    return gzidx_export_advanced(index, &output_stream, NULL);
}

#ifdef __cplusplus
} // extern "C"
#endif
