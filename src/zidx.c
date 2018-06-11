#include "zidx.h"

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int zidx_index_init(zidx_index* index,
                     zidx_compressed_stream* compressed_stream)
{
    return zidx_index_init_advanced(index, compressed_stream,
                                    ZIDX_STREAM_GZIP_OR_ZLIB,
                                    ZIDX_CHECKSUM_DEFAULT, NULL,
                                    ZIDX_DEFAULT_INITIAL_LIST_CAPACITY,
                                    ZIDX_DEFAULT_WINDOW_SIZE,
                                    ZIDX_DEFAULT_COMPRESSED_DATA_BUFFER_SIZE);
}

int zidx_index_init_advanced(zidx_index* index,
                             zidx_compressed_stream* compressed_stream,
                             zidx_stream_type stream_type,
                             zidx_checksum_option checksum_option,
                             z_stream* z_stream_ptr, int initial_capacity,
                             int window_size, int compressed_data_buffer_size)
{
    /* assert(index != NULL); */
    /* assert(compressed_stream != NULL); */
    /* assert stream_type is valid. */
    /* assert checksum_option is valid. */
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

    index->list = (zidx_checkpoint*) malloc(sizeof(zidx_checkpoint)
                                                 * initial_capacity);
    if (!index->list) goto memory_fail;
    index->list_count    = 0;
    index->list_capacity = initial_capacity;

    index->compressed_data_buffer = (unsigned char*)
                                        malloc(compressed_data_buffer_size);
    if (!index->compressed_data_buffer) goto memory_fail;

    index->window_size                 = window_size;
    index->compressed_data_buffer_size = compressed_data_buffer_size;

    index->compressed_stream = compressed_stream;
    index->z_stream          = z_stream_ptr;
    index->stream_type       = stream_type;

    if (index->stream_type == ZIDX_STREAM_DEFLATE) {
        index->stream_state = ZIDX_EXPECT_DEFLATE_BLOCKS;
    } else {
        index->stream_state = ZIDX_EXPECT_FILE_HEADERS;
    }

    index->checksum_option = checksum_option;

    return 0;
memory_fail:
    free(z_stream_ptr);
    free(index->list);
    free(index->compressed_data_buffer);
/* fail: */
    return -1;
}

int zidx_index_destroy(zidx_index* index)
{
    zidx_checkpoint *it;
    zidx_checkpoint *end = index->list + index->list_count;

    if (!index) return 0;

    free(index->z_stream);

    for (it = index->list; it < end; it++) {
        free(it->window_data);
    }
    free(index->list);

    return 0;
}

int zidx_gzip_read(zidx_index* index, void *buffer, size_t nbytes);
int zidx_gzip_read_advanced(zidx_index* index, void *buffer, size_t nbytes,
                            zidx_block_callback block_callback,
                            void *callback_context);
int zidx_gzip_seek(zidx_index* index, off_t offset, int whence);
int zidx_gzip_seek_advanced(zidx_index* index, off_t offset, int whence,
                            zidx_block_callback next_block_callback,
                            void *callback_context);
off_t zidx_gzip_tell(zidx_index* index);
int zidx_gzip_rewind(zidx_index* index);

int zidx_build_index(zidx_index* index, off_t spacing_length);
int zidx_build_index_advanced(zidx_index* index,
                              zidx_block_callback next_block_callback,
                              void *callback_context);

int zidx_add_checkpoint(zidx_index* index, zidx_checkpoint* checkpoint);
int zidx_get_checkpoint(zidx_index* index, off_t offset);

void zidx_extend_index_size(zidx_index* index, size_t nmembers);
void zidx_shrink_index_size(zidx_index* index);

/* TODO: Implement these. */
int zidx_import_advanced(zidx_index *index,
                         const zidx_gzip_index_stream *stream,
                         zidx_import_filter_callback filter,
                         void *filter_context) { return 0; }

int zidx_export_advanced(zidx_index *index,
                         const zidx_gzip_index_stream *stream,
                         zidx_export_filter_callback filter,
                         void *filter_context) { return 0; }

int zidx_import(zidx_index *index, FILE* input_index_file)
{
    const zidx_gzip_index_stream input_stream = {
        zidx_raw_file_read,
        zidx_raw_file_write,
        zidx_raw_file_seek,
        zidx_raw_file_tell,
        zidx_raw_file_eof,
        zidx_raw_file_error,
        (void*) input_index_file
    };
    return zidx_import_advanced(index, &input_stream, NULL, NULL);
}

int zidx_export(zidx_index *index, FILE* output_index_file)
{
    const zidx_gzip_index_stream output_stream = {
        zidx_raw_file_read,
        zidx_raw_file_write,
        zidx_raw_file_seek,
        zidx_raw_file_tell,
        zidx_raw_file_eof,
        zidx_raw_file_error,
        (void*) output_index_file
    };
    return zidx_export_advanced(index, &output_stream, NULL, NULL);
}

size_t zidx_raw_file_read(void *file, void *buffer, size_t nbytes)
{
    return fread(buffer, 1, nbytes, (FILE*) file);
}

size_t zidx_raw_file_write(void *file, const void *buffer, size_t nbytes)
{
    return fwrite(buffer, 1, nbytes, (FILE*) file);
}

int zidx_raw_file_seek(void *file, off_t offset, int whence)
{
    switch(whence)
    {
        case ZIDX_SEEK_SET:
            return fseek((FILE*) file, offset, SEEK_SET);
        case ZIDX_SEEK_CUR:
            return fseek((FILE*) file, offset, SEEK_CUR);
        case ZIDX_SEEK_END:
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
