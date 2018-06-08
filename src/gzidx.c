#include "gzidx.h"

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int gzidx_index_init(gzidx_index* index,
                     gzidx_gzip_input_stream* gzip_input_stream)
{
    return gzidx_index_init_advanced(index, gzip_input_stream, NULL,
                                     GZIDX_DEFAULT_INITIAL_LIST_CAPACITY,
                                     GZIDX_DEFAULT_WINDOW_SIZE,
                                     GZIDX_DEFAULT_COMPRESSED_DATA_BUFFER_SIZE);
}

int gzidx_index_init_advanced(gzidx_index* index,
                              gzidx_gzip_input_stream* gzip_input_stream,
                              z_stream* z_stream_ptr, int initial_capacity,
                              int window_size, int compressed_data_buffer_size)
{
    /* assert(index != NULL); */
    /* assert(gzip_input_stream != NULL); */
    index->list = NULL;
    index->compressed_data_buffer = NULL;

    if (!z_stream_ptr) {
        z_stream_ptr = (z_stream*) malloc(sizeof(z_stream));

        if (!z_stream_ptr) goto memory_fail;

        z_stream_ptr->zalloc   = Z_NULL;
        z_stream_ptr->zfree    = Z_NULL;
        z_stream_ptr->opaque   = Z_NULL;
    }
    z_stream_ptr->avail_in = 0;
    z_stream_ptr->next_in  = Z_NULL;

    index->list = (gzidx_checkpoint*) malloc(sizeof(gzidx_checkpoint)
                                                 * initial_capacity);
    if (!index->list) goto memory_fail;
    index->list_count    = 0;
    index->list_capacity = initial_capacity;

    index->compressed_data_buffer = (unsigned char*)
                                        malloc(compressed_data_buffer_size);
    if (!index->compressed_data_buffer) goto memory_fail;

    index->window_size                 = window_size;
    index->compressed_data_buffer_size = compressed_data_buffer_size;

    index->gzip_input_stream = gzip_input_stream;
    index->z_stream          = z_stream_ptr;
    index->stream_state      = GZIDX_STATE_FILE_HEADERS;


    return 0;
memory_fail:
    free(z_stream_ptr);
    free(index->list);
    free(index->compressed_data_buffer);
/* fail: */
    return -1;
}

int gzidx_index_destroy(gzidx_index* index)
{
    gzidx_checkpoint *it;
    gzidx_checkpoint *end = index->list + index->list_count;

    if (!index) return 0;

    free(index->z_stream);

    for (it = index->list; it < end; it++) {
        free(it->window_data);
    }
    free(index->list);

    return 0;
}

int gzidx_gzip_read(gzidx_index* index, void *buffer, size_t nbytes);
int gzidx_gzip_read_advanced(gzidx_index* index, void *buffer, size_t nbytes,
                             gzidx_block_callback block_callback,
                             void *callback_context);
int gzidx_gzip_seek(gzidx_index* index, off_t offset, int whence);
int gzidx_gzip_seek_advanced(gzidx_index* index, off_t offset, int whence,
                             gzidx_block_callback next_block_callback,
                             void *callback_context);
off_t gzidx_gzip_tell(gzidx_index* index);
int gzidx_gzip_rewind(gzidx_index* index);

int gzidx_build_index(gzidx_index* index, off_t spacing_length);
int gzidx_build_index_advanced(gzidx_index* index,
                               gzidx_block_callback next_block_callback,
                               void *callback_context);

int gzidx_add_checkpoint(gzidx_index* index, gzidx_checkpoint* checkpoint);
int gzidx_get_checkpoint(gzidx_index* index, off_t offset);

void gzidx_extend_index_size(gzidx_index* index, size_t nmembers);
void gzidx_shrink_index_size(gzidx_index* index);

/* TODO: Implement these. */
int gzidx_import_advanced(gzidx_index *index,
                          const gzidx_gzip_index_stream *stream,
                          gzidx_import_filter_callback filter,
                          void *filter_context) { return 0; }

int gzidx_export_advanced(gzidx_index *index,
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
