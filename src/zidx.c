#include "zidx.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ZIDX_DEBUG
#define ZIDX_LOG(...) do { printf(__VA_ARGS__); } while(0)
#else
#define ZIDX_LOG(...) while(0)
#endif

static int get_unused_bits_count(z_stream* zs)
{
    return zs->data_type & 7;
}

static int is_last_deflate_block(z_stream* zs)
{
    return zs->data_type & 64;
}

static int is_on_block_boundary(z_stream *zs)
{
    return zs->data_type & 128;
}

static int inflate_and_update_offset(zidx_index* index, z_stream* zs, int flush)
{
    int available_comp_bytes;
    int available_uncomp_bytes;
    int comp_bytes_inflated;
    int uncomp_bytes_inflated;
    int z_ret;

    available_comp_bytes   = zs->avail_in;
    available_uncomp_bytes = zs->avail_out;

    z_ret = inflate(zs, flush);
    if(z_ret != Z_OK) return z_ret;

    comp_bytes_inflated = zs->avail_in - available_comp_bytes;
    uncomp_bytes_inflated = zs->avail_out - available_uncomp_bytes;

    index->offset.comp += comp_bytes_inflated;
    index->offset.uncomp += uncomp_bytes_inflated;
    index->offset.comp_bits = get_unused_bits_count(zs);

    return Z_OK;
}

static int initialize_zstream(zidx_index* index, z_stream* zs, int window_bits)
{
    int z_ret;
    if (!index->z_stream_initialized) {
        z_ret = inflateInit2(zs, window_bits);
        if (z_ret == Z_OK) {
            index->z_stream_initialized = 1;
        }
    } else {
        z_ret = inflateReset2(zs, window_bits);
    }
    return z_ret;
}

int zidx_index_init(zidx_index* index,
                     zidx_comp_stream* comp_stream)
{
    return zidx_index_init_advanced(index, comp_stream,
                                    ZIDX_STREAM_GZIP_OR_ZLIB,
                                    ZIDX_CHECKSUM_DEFAULT, NULL,
                                    ZIDX_DEFAULT_INITIAL_LIST_CAPACITY,
                                    ZIDX_DEFAULT_WINDOW_SIZE,
                                    ZIDX_DEFAULT_COMPRESSED_DATA_BUFFER_SIZE,
                                    ZIDX_DEFAULT_SEEKING_DATA_BUFFER_SIZE);
}

int zidx_index_init_advanced(zidx_index* index,
                             zidx_comp_stream* comp_stream,
                             zidx_stream_type stream_type,
                             zidx_checksum_option checksum_option,
                             z_stream* z_stream_ptr, int initial_capacity,
                             unsigned int window_size,
                             int comp_data_buffer_size,
                             int seeking_data_buffer_size)
{
    /* assert(index != NULL); */
    /* assert(comp_stream != NULL); */
    /* assert stream_type is valid. */
    /* assert checksum_option is valid. */
    index->list = NULL;
    index->comp_data_buffer = NULL;
    index->seeking_data_buffer = NULL;

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

    index->comp_data_buffer = (uint8_t*)
                                        malloc(comp_data_buffer_size);
    if (!index->comp_data_buffer) goto memory_fail;

    index->seeking_data_buffer = (uint8_t*) malloc(seeking_data_buffer_size);
    if (!index->seeking_data_buffer) goto memory_fail;

    index->window_size                 = window_size;
    index->comp_data_buffer_size = comp_data_buffer_size;
    index->seeking_data_buffer_size    = seeking_data_buffer_size;

    index->comp_stream             = comp_stream;
    index->offset.comp      = 0;
    index->offset.comp_bits = 0;
    index->offset.uncomp    = 0;
    index->z_stream                      = z_stream_ptr;
    index->stream_type                   = stream_type;
    index->stream_state                  = ZIDX_EXPECT_FILE_HEADERS;
    index->checksum_option               = checksum_option;
    index->z_stream_initialized          = 0;

    return 0;
memory_fail:
    free(z_stream_ptr);
    free(index->list);
    free(index->comp_data_buffer);
    free(index->seeking_data_buffer);
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

int zidx_read(zidx_index* index, uint8_t *buffer, int nbytes)
{
    return zidx_read_advanced(index, buffer, nbytes, NULL, NULL);
}

int zidx_read_advanced(zidx_index* index, uint8_t *buffer,
                       int nbytes, zidx_block_callback block_callback,
                       void *callback_context)
{
    /* TODO: Implement Z_SYNC_FLUSH option. */
    /* TODO: Implement decompression of existing compressed data after read
     * callback returns with less number of bytes read. */
    /* TODO: Check return values of read calls better in case they return
     * shorter then expected buffer length.  */
    /* TODO: Implement support for concatanated gzip streams. */
    /* TODO: Implement window bits to reflect reality (window size). */
    /* TODO: Convert buffer type to uint8_t* */

    int z_ret;      /* Return value for zlib calls. */
    int s_ret;      /* Return value for stream calls. */
    int s_read_len; /* Return value for stream read calls. */

    uint8_t read_completed;

    /* Aliases for frequently used elements. */
    zidx_comp_stream *stream = index->comp_stream;
    uint8_t *comp_buf               = index->comp_data_buffer;
    int comp_buf_len                = index->comp_data_buffer_size;
    z_stream *zs                   = index->z_stream;

    switch (index->stream_state) {
        /* If headers are expected */
        case ZIDX_EXPECT_FILE_HEADERS:
            switch(index->stream_type) {
                case ZIDX_STREAM_DEFLATE:
                    ZIDX_LOG("DEFLATE is being initialized.\n");
                    z_ret = initialize_zstream(index, zs, -MAX_WBITS);
                    if (z_ret != Z_OK) return -1;
                    break;
                case ZIDX_STREAM_GZIP:
                    ZIDX_LOG("GZIP is being initialized.\n");
                    z_ret = initialize_zstream(index, zs, 16 + MAX_WBITS);
                    if (z_ret != Z_OK) return -1;
                    goto read_headers;
                case ZIDX_STREAM_GZIP_OR_ZLIB:
                    ZIDX_LOG("GZIP/ZLIB is being initialized.\n");
                    z_ret = initialize_zstream(index, zs, 32 + MAX_WBITS);
                    if (z_ret != Z_OK) return -1;
                    goto read_headers;
                read_headers:
                    zs->next_out   = buffer;
                    zs->avail_out  = 0;
                    read_completed = 0;
                    while (!read_completed) {
                        s_read_len = stream->read(stream->context, comp_buf,
                                                  comp_buf_len);
                        if (stream->error(stream->context)) return -2;
                        if (s_read_len == 0) return -3;

                        zs->next_in     = comp_buf;
                        zs->avail_in    = s_read_len;

                        z_ret = inflate_and_update_offset(index, zs, Z_BLOCK);

                        ZIDX_LOG("[HEADER] z_ret: %d\n", z_ret);
                        if (z_ret == Z_OK) {
                            ZIDX_LOG("[HEADER] Done reading.\n");
                            read_completed = 1;
                        } else {
                            ZIDX_LOG("[HEADER] Error reading.\n");
                            return -4;
                        }
                    } // while not read_completed
                    z_ret = initialize_zstream(index, zs, -MAX_WBITS);
                    if (z_ret != Z_OK) return -5;
                    break;
                default:
                    return -6;
            }

            index->stream_state = ZIDX_EXPECT_DEFLATE_BLOCKS;

            ZIDX_LOG("[HEADER] Inflate initialized.\n");

        case ZIDX_EXPECT_DEFLATE_BLOCKS:
            zs->next_out  = buffer;
            zs->avail_out = nbytes;
            read_completed = 0;
            while (!read_completed) {
                if(zs->avail_in == 0) {
                    ZIDX_LOG("[BLOCKS] Reading compressed data.\n");
                    s_read_len = stream->read(stream->context, comp_buf,
                                              comp_buf_len);
                    if (stream->error(stream->context)) return -7;

                    zs->next_in  = comp_buf;
                    zs->avail_in = s_read_len;
                }
                z_ret = inflate_and_update_offset(index, zs, Z_BLOCK);
                ZIDX_LOG("[BLOCKS] z_ret: %d\n", z_ret);
                if (z_ret == Z_OK) {
                    if (is_on_block_boundary(zs)) {
                        ZIDX_LOG("[BLOCKS] On block boundary.\n");
                        if (is_last_deflate_block(zs)) {
                            ZIDX_LOG("[BLOCKS] Last block.\n");
                            read_completed = 1;
                        }
                        if (block_callback != NULL) {
                            ZIDX_LOG("[BLOCKS] Block boundary callback.\n");
                            (*block_callback)(callback_context,
                                              index, &index->offset);
                        }
                    }
                    if (zs->avail_out == 0) {
                        ZIDX_LOG("[BLOCKS] Buffer is full.\n");
                        read_completed = 1;
                    }
                } else if (z_ret == Z_STREAM_END) {
                    ZIDX_LOG("[BLOCKS] End of stream.\n");
                    return 0;
                } else {
                    #ifdef ZIDX_DEBUG
                    ZIDX_LOG("[BLOCKS] Read error");
                    if(zs->msg != NULL) {
                        ZIDX_LOG(": %s", zs->msg);
                    } else {
                        ZIDX_LOG(".");
                    }
                    ZIDX_LOG("\n");
                    #endif
                    return -9;
                }
            } // while not read_completed
            break;
    } // end of stream state switch

    /* Return number of bytes read into the buffer. */
    return zs->next_out - buffer;
}

int zidx_seek(zidx_index* index, off_t offset, int whence)
{
    return zidx_seek_advanced(index, offset, whence, NULL, NULL);
}

int zidx_seek_advanced(zidx_index* index, off_t offset, int whence,
                       zidx_block_callback block_callback,
                       void *callback_context)
{
    /* TODO: If this function fails to reset z_stream, it will leave z_stream in
     * an invalid state. Must be handled. */
    int z_ret;
    int f_ret;
    uint8_t byte;
    zidx_checkpoint *checkpoint;
    int checkpoint_idx;
    off_t num_bytes_remaining;
    int num_bytes_next;

    checkpoint_idx = zidx_get_checkpoint(index, offset);
    checkpoint = checkpoint_idx >= 0 ? &index->list[checkpoint_idx] : NULL;

    if (checkpoint == NULL) {
        f_ret = index->comp_stream->seek(
                                        index->comp_stream->context,
                                        0,
                                        ZIDX_SEEK_SET);
        if (f_ret < 0) return -2;

        index->stream_state = ZIDX_EXPECT_FILE_HEADERS;
        index->offset.comp = 0;
        index->offset.comp_bits = 0;
        index->offset.uncomp = 0;
    } else if (
            index->offset.comp < checkpoint->offset.comp
            || index->offset.comp > offset) {
        /* TODO: Fix window size */
        z_ret = initialize_zstream(index, index->z_stream, -MAX_WBITS);
        if (z_ret != Z_OK) return -3;

        f_ret = index->comp_stream->seek(index->comp_stream->context,
                                         checkpoint->offset.comp -
                                         (checkpoint->offset.comp_bits > 0
                                                ? 1 : 0),
                                         ZIDX_SEEK_SET);
        if (f_ret < 0) return -4;

        if (checkpoint->offset.comp_bits > 0) {
            f_ret = index->comp_stream->read(index->comp_stream->context,
                                             &byte, 1);

            if (f_ret != 1) return -5;

            /* TODO: Check eof & error */

            byte >>= (8 - checkpoint->offset.comp_bits);

            z_ret = inflatePrime(index->z_stream,
                                 checkpoint->offset.comp_bits, byte);
            if (z_ret != Z_OK) return -6;
        }

        z_ret = inflateSetDictionary(index->z_stream, checkpoint->window_data,
                                     index->window_size);
        if (z_ret != Z_OK) return -7;

        index->stream_state = ZIDX_EXPECT_DEFLATE_BLOCKS;
        index->offset.comp = checkpoint->offset.comp;
        index->offset.comp_bits = checkpoint->offset.comp_bits;
        index->offset.uncomp = checkpoint->offset.comp;
    }

    num_bytes_remaining = offset - index->offset.uncomp;
    while(num_bytes_remaining > 0) {
        num_bytes_next =
            (num_bytes_remaining > index->seeking_data_buffer_size ?
                index->seeking_data_buffer_size :
                num_bytes_remaining);
        f_ret = zidx_read_advanced(index, index->seeking_data_buffer,
                                   num_bytes_next, block_callback,
                                   callback_context);
        if(f_ret < 0) return -8;

        num_bytes_remaining -= f_ret;
    }

    return 0;
}

off_t zidx_tell(zidx_index* index);
int zidx_rewind(zidx_index* index);

int zidx_build_index(zidx_index* index, off_t spacing_length);
int zidx_build_index_advanced(zidx_index* index,
                              zidx_block_callback next_block_callback,
                              void *callback_context);

int zidx_create_checkpoint(zidx_index* index,
                           zidx_checkpoint* new_checkpoint,
                           zidx_checkpoint_offset* offset)
{
    int z_ret;

    if (index == NULL) return -1;
    if (new_checkpoint == NULL) return -2;
    if (offset == NULL) return -3;

    if (new_checkpoint->window_data == NULL) {
        new_checkpoint->window_data = (uint8_t *)
                                          malloc(index->window_size);
    }

    memcpy(&new_checkpoint->offset, offset, sizeof(*offset));

    z_ret = inflateGetDictionary(index->z_stream, new_checkpoint->window_data,
                                 &index->window_size);
    if (z_ret != Z_OK) return -4;

    return 0;
}

int zidx_add_checkpoint(zidx_index* index, zidx_checkpoint* checkpoint)
{
    if (index == NULL) return -1;
    if (checkpoint == NULL) return -2;

    if (index->list_capacity == index->list_count) {
        zidx_extend_index_size(index, index->list_count);
    }

    int last_uncomp = index->list[index->list_count - 1].offset.uncomp;
    if (checkpoint->offset.uncomp <= last_uncomp) {
        return -3;
    }

    memcpy(&index->list[index->list_count], checkpoint, sizeof(*checkpoint));
    index->list_count++;

    return 0;
}

int zidx_get_checkpoint(zidx_index* index, off_t offset)
{
    #define ZIDX_OFFSET_(idx) (index->list[idx].offset.uncomp)

    /* Variables used for binary search. */
    int left, right;

    /* Return value holding the index or error number. */
    int idx;

    /* Return not found if list is empty. */
    if(index->list_count == 0) return -1;

    left  = 0;
    right = index->list_count - 1;

    /* Check the last element first. We check it in here so that we don't
     * account for it in every iteartion of the loop below. */
    if(ZIDX_OFFSET_(right) < offset) {
        return idx;
    }

    /* Binary search for index. */
    while (left < right) {

        idx = (left + right) / 2;

        /* If current offset is greater, we need to move the range to left by
         * updating `right`. */
        if(ZIDX_OFFSET_(idx) > offset) {
            right = idx - 1;

        /* If current offset is less than or equal, but also the following one,
         * we need to move the range to right by updating `left`. */
        } else if(ZIDX_OFFSET_(idx + 1) <= offset) {
            left = idx + 1;

        /* If current offset is less than or equal, and the following one is
         * greater, we found the lower bound, return it. */
        } else {
            return idx;
        }
    }

    return -1;

    #undef ZIDX_OFFSET_
}

int zidx_extend_index_size(zidx_index* index, int nmembers)
{
    zidx_checkpoint *new_list;

    new_list = (zidx_checkpoint*) realloc(index->list, sizeof(zidx_checkpoint)
                                           * (index->list_capacity + nmembers));
    if(!new_list) return -1;

    index->list           = new_list;
    index->list_capacity += nmembers;

    return 0;
}

void zidx_shrink_index_size(zidx_index* index);

/* TODO: Implement these. */
int zidx_import_advanced(zidx_index *index,
                         const zidx_index_stream *stream,
                         zidx_import_filter_callback filter,
                         void *filter_context) { return 0; }

int zidx_export_advanced(zidx_index *index,
                         const zidx_index_stream *stream,
                         zidx_export_filter_callback filter,
                         void *filter_context) { return 0; }

int zidx_import(zidx_index *index, FILE* input_index_file)
{
    const zidx_index_stream input_stream = {
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
    const zidx_index_stream output_stream = {
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

int zidx_comp_file_init(zidx_comp_stream *stream, FILE *file)
{
    if(stream == NULL) return -1;

    stream->read    = zidx_raw_file_read;
    stream->seek    = zidx_raw_file_seek;
    stream->tell    = zidx_raw_file_tell;
    stream->eof     = zidx_raw_file_eof;
    stream->error   = zidx_raw_file_error;
    stream->length  = zidx_raw_file_length;
    stream->context = (void*) file;

    return 0;
}

int zidx_index_file_init(zidx_index_stream *stream, FILE *file)
{
    if(stream == NULL) return -1;

    stream->read    = zidx_raw_file_read;
    stream->write   = zidx_raw_file_write;
    stream->seek    = zidx_raw_file_seek;
    stream->tell    = zidx_raw_file_tell;
    stream->eof     = zidx_raw_file_eof;
    stream->error   = zidx_raw_file_error;
    stream->context = (void*) file;

    return 0;
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
