#include "zidx.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ZX_DEBUG
#define ZX_LOG(...) \
    do { \
        fprintf(stderr, "%s:%d:%s: ", __FILE__, __LINE__, __func__); \
        fprintf(stderr, __VA_ARGS__); \
    } while(0)
#else
#define ZX_LOG(...) while(0)
#endif

typedef enum zidx_stream_state
{
    ZX_STATE_INVALID,
    ZX_STATE_FILE_HEADERS,
    ZX_STATE_DEFLATE_BLOCKS,
    ZX_STATE_FILE_TRAILER
} zidx_stream_state;

struct zidx_checkpoint_offset_s
{
    off_t uncomp;
    off_t comp;
    uint8_t comp_bits_count;
    uint8_t comp_byte;
};

struct zidx_checkpoint_s
{
    zidx_checkpoint_offset offset;
    uint32_t checksum;
    uint8_t *window_data;
};

struct zidx_index_s
{
    zidx_comp_stream *comp_stream;
    zidx_stream_state stream_state;
    zidx_stream_type stream_type;
    zidx_checkpoint_offset offset;
    z_stream* z_stream;
    int list_count;
    int list_capacity;
    zidx_checkpoint *list;
    zidx_checksum_option checksum_option;
    unsigned int window_size;
    int window_bits;
    uint8_t *comp_data_buffer;
    int comp_data_buffer_size;
    uint8_t *seeking_data_buffer;
    int seeking_data_buffer_size;
    char inflate_initialized;
};

/**
 * Return number of unused bits count in the last byte consumed by inflate().
 *
 * This function should be used after a call to inflate. See the documentation
 * of inflate() in zlib manual for more details.
 *
 * This function is used to store information about the byte which is used by
 * two blocks in a block boundary. Therefore, return value of this function is
 * meaningful for the purpose of this library only if the zs is in a block
 * boundary.
 *
 * \param zs zlib stream.
 *
 * \return The number of bits unused in the last consumed byte.
 */
static uint8_t get_unused_bits_count(z_stream* zs)
{
    return zs->data_type & 7;
}

/**
 * Check if zlib stream is on last deflate block.
 *
 * This function should be used after a call to inflate. See the documentation
 * of inflate() in zlib manual for more details.
 *
 * \param zs zlib stream.
 *
 * \return 64 if inflate stopped on block boundary, 0 otherwise.
 */
static int is_last_deflate_block(z_stream* zs)
{
    return zs->data_type & 64;
}

/**
 * Check if zlib stream is on block boundary.
 *
 * This function should be used after a call to inflate. See the documentation
 * of inflate() in zlib manual for more details.
 *
 * \param zs zlib stream.
 *
 * \return 128 if inflate stopped on block boundary, 0 otherwise.
 */
static int is_on_block_boundary(z_stream *zs)
{
    return zs->data_type & 128;
}

/**
 * Inflate using buffers from zs, and update index->offset accordingly.
 *
 * This function is just a wrapper around inflate() function of zlib library. It
 * updates offsets in index data after inflating.
 *
 * \param index Index data.
 * \param zs    zlib stream data.
 * \param flush flush parameter to pass as a second argument to inflate().
 *
 * \return The return value of inflate() call.
 */
static int inflate_and_update_offset(zidx_index* index, z_stream* zs, int flush)
{
    /* Number of bytes in input/output buffer before inflate. */
    int available_comp_bytes;
    int available_uncomp_bytes;

    /* Number of bytes of compressed/uncompressed data consuemd/produced by
     * inflate. */
    int comp_bytes_inflated;
    int uncomp_bytes_inflated;

    /* Used for return value. */
    int z_ret;

    /* Save number of bytes in input/output buffers. */
    available_comp_bytes   = zs->avail_in;
    available_uncomp_bytes = zs->avail_out;

    /* If no data is available to decompress return. If this check is not made,
     * setting index->offset.comp_byte may underflow while updating offset. */
    if (available_comp_bytes == 0) return Z_OK;

    /* Use zlib to inflate data. */
    z_ret = inflate(zs, flush);
    if (z_ret != Z_OK) return z_ret;

    /* Compute number of bytes inflated. */
    comp_bytes_inflated   = available_comp_bytes - zs->avail_in;
    uncomp_bytes_inflated = available_uncomp_bytes - zs->avail_out;

    /* Update byte offsets. */
    index->offset.comp   += comp_bytes_inflated;
    index->offset.uncomp += uncomp_bytes_inflated;

    /* Set bit offsets only if we are in block boundary. */
    /* TODO: Truncating if not in block boundary is probably unnecessary. It may
     * be deleted in future. */
    if (is_on_block_boundary(zs)) {
        index->offset.comp_bits_count = get_unused_bits_count(zs);
        if (index->offset.comp_bits_count > 0) {
            /* If there is a byte some bits of which has been used, save this
             * byte to offset. This byte will be feed to inflatePrime() if this
             * block boundary is used as a checkpoint to seek on data. See
             * zidx_seek() for more details. */
            index->offset.comp_byte = *(zs->next_in - 1);
        } else {
            /* TODO: Again, is this necessary? Bits count will be already 0. */
            index->offset.comp_byte = 0;
        }
    } else {
        index->offset.comp_bits_count = 0;
        index->offset.comp_byte = 0;
    }

    /* We return Z_OK here, as z_ret should already be Z_OK at this point. */
    return Z_OK;
}

/**
 * Initialize zs using inflateInit2() if this is first time. Otherwise use
 * inflateReset2().
 *
 * \param index       Index data.
 * \param zs          zlib stream data.
 * \param window_bits Parameter to pass as a second argument to
 *                    inflateInit2() or inflateReset2()
 *
 * \return The return value of inflateInit2() or inflateReset2().
 *
 * \todo  It is reduntant to pass zs, as it should be same with
 *        index->z_stream. Consider removing second argument.
 * */
static int initialize_inflate(zidx_index* index, z_stream* zs, int window_bits)
{
    int z_ret;
    if (!index->inflate_initialized) {
        z_ret = inflateInit2(zs, window_bits);
        if (z_ret == Z_OK) {
            index->inflate_initialized = 1;
        }
    } else {
        z_ret = inflateReset2(zs, window_bits);
    }
    return z_ret;
}

zidx_index* zidx_index_create()
{
    zidx_index *index;
    index = (zidx_index*) malloc(sizeof(zidx_index));
    return index;
}

int zidx_index_init(zidx_index* index,
                     zidx_comp_stream* comp_stream)
{
    return zidx_index_init_advanced(index, comp_stream,
                                    ZX_STREAM_GZIP_OR_ZLIB,
                                    ZX_CHECKSUM_DEFAULT, NULL,
                                    ZX_DEFAULT_INITIAL_LIST_CAPACITY,
                                    ZX_DEFAULT_WINDOW_SIZE,
                                    ZX_DEFAULT_COMPRESSED_DATA_BUFFER_SIZE,
                                    ZX_DEFAULT_SEEKING_DATA_BUFFER_SIZE);
}

int zidx_index_init_advanced(zidx_index* index,
                             zidx_comp_stream* comp_stream,
                             zidx_stream_type stream_type,
                             zidx_checksum_option checksum_option,
                             z_stream* z_stream_ptr,
                             int initial_capacity,
                             int window_size,
                             int comp_data_buffer_size,
                             int seeking_data_buffer_size)
{
    /* Temporary variables that will be used to initialize corresponding members
     * of index. These are not modified directly on index, because we don't want
     * to modify it in case of a failure. */
    zidx_checkpoint *list;
    uint8_t* comp_data_buffer;
    uint8_t* seeking_data_buffer;
    int window_bits;

    /* Flag used to indicate whether z_stream_ptr argument should be released in
     * case of a failure. */
    int free_zs_on_failure;

    /* Sanity checks. */
    if (index == NULL || comp_stream == NULL) {
        ZX_LOG("ERROR: index or comp_stream is null.");
        return ZX_ERR_PARAMS;
    }
    if (stream_type != ZX_STREAM_GZIP && stream_type != ZX_STREAM_DEFLATE
            && stream_type != ZX_STREAM_GZIP_OR_ZLIB) {
        ZX_LOG("ERROR: Unknown stream_type: %d", (int) stream_type);
        return ZX_ERR_PARAMS;
    }
    if (checksum_option != ZX_CHECKSUM_DISABLED
            && checksum_option != ZX_CHECKSUM_DEFAULT
            && checksum_option != ZX_CHECKSUM_FORCE_CRC32
            && checksum_option != ZX_CHECKSUM_FORCE_ADLER32) {
        ZX_LOG("ERROR: Unknown checksum_option: %d", (int) stream_type);
        return ZX_ERR_PARAMS;
    }
    if (initial_capacity <= 0) {
        ZX_LOG("ERROR: initial_capacity is nonpositive.");
        return ZX_ERR_PARAMS;
    }
    if (window_size <= 0) {
        ZX_LOG("ERROR: window_size is nonpositive.");
        return ZX_ERR_PARAMS;
    }
    if (window_size < 512 || window_size > 32768) {
        ZX_LOG("ERROR: window_size should be between 512-32768 inclusive.");
        return ZX_ERR_PARAMS;
    }
    for (window_bits = 9; window_bits <= 15; window_bits++) {
        if (window_size == (1 << window_bits)) {
            break;
        }
        if (window_size < (1 << window_bits)) {
            ZX_LOG("ERROR: window_size should be a power of 2.");
            return ZX_ERR_PARAMS;
        }
    }
    if (comp_data_buffer_size <= 0) {
        ZX_LOG("ERROR: comp_data_buffer_size is nonpositive.");
        return ZX_ERR_PARAMS;
    }
    if (seeking_data_buffer_size <= 0) {
        ZX_LOG("ERROR: seeking_data_buffer_size is nonpositive.");
        return ZX_ERR_PARAMS;
    }

    /* Assign NULL to anything to be freed in case of a failure. */
    list                = NULL;
    comp_data_buffer    = NULL;
    seeking_data_buffer = NULL;

    /* Initialize z_stream_ptr if not provided. free_sz_on_failure is set to
     * indicate that z_stream_ptr is allocated by this function and it should be
     * deallocated in case of a failure. */
    if (z_stream_ptr) {
        free_zs_on_failure = 0;
    } else {
        z_stream_ptr = (z_stream*) malloc(sizeof(z_stream));
        if (!z_stream_ptr) goto memory_fail;

        z_stream_ptr->zalloc = Z_NULL;
        z_stream_ptr->zfree  = Z_NULL;
        z_stream_ptr->opaque = Z_NULL;

        free_zs_on_failure = 1;
    }
    z_stream_ptr->avail_in = 0;
    z_stream_ptr->next_in  = Z_NULL;

    /* Initialize list. Note: Currently initial_capacity can't be zero, as it
     * was checked in above sabity checks. */
    list = (zidx_checkpoint*) malloc(sizeof(zidx_checkpoint) * initial_capacity);
    if (!list) goto memory_fail;

    /* Initialize compressed data buffer. */
    comp_data_buffer = (uint8_t*) malloc(comp_data_buffer_size);
    if (!comp_data_buffer) goto memory_fail;

    /* Initialize seeking data buffer. */
    seeking_data_buffer = (uint8_t*) malloc(seeking_data_buffer_size);
    if (!seeking_data_buffer) goto memory_fail;

    /* Now that there are no failure possibilities (right?), we can modify
     * index data structure. */

    /* Set list. */
    index->list          = list;
    index->list_count    = 0;
    index->list_capacity = initial_capacity;

    /* Set compressed data buffer. */
    index->comp_data_buffer      = comp_data_buffer;
    index->comp_data_buffer_size = comp_data_buffer_size;

    /* Set seeking data buffer. */
    index->seeking_data_buffer      = seeking_data_buffer;
    index->seeking_data_buffer_size = seeking_data_buffer_size;

    /* Set window size and bits. */
    index->window_size = window_size;
    index->window_bits = window_bits;

    /* Set compression stream. */
    index->comp_stream = comp_stream;

    /* Set current offsets. */
    index->offset.comp            = 0;
    index->offset.comp_bits_count = 0;
    index->offset.comp_byte       = 0;
    index->offset.uncomp          = 0;

    /* Set stream options. */
    index->z_stream            = z_stream_ptr;
    index->stream_type         = stream_type;
    index->stream_state        = ZX_STATE_FILE_HEADERS;
    index->inflate_initialized = 0;

    /* Set checksum option. */
    index->checksum_option = checksum_option;

    return 0;

memory_fail:
    if(free_zs_on_failure) free(z_stream_ptr);
    free(list);
    free(comp_data_buffer);
    free(seeking_data_buffer);
    return -1;
}

int zidx_index_destroy(zidx_index* index)
{
    int z_ret;
    zidx_checkpoint *it;
    zidx_checkpoint *end;

    if (!index) return 0;

    end = index->list + index->list_count;

    z_ret = inflateEnd(index->z_stream);
    if (z_ret != Z_OK) return -1;

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
        case ZX_STATE_FILE_HEADERS:
            switch(index->stream_type) {
                case ZX_STREAM_DEFLATE:
                    ZX_LOG("DEFLATE is being initialized.\n");
                    z_ret = initialize_inflate(index, zs, -MAX_WBITS);
                    if (z_ret != Z_OK) return -1;
                    break;
                case ZX_STREAM_GZIP:
                    ZX_LOG("GZIP is being initialized.\n");
                    z_ret = initialize_inflate(index, zs, 16 + MAX_WBITS);
                    if (z_ret != Z_OK) return -1;
                    goto read_headers;
                case ZX_STREAM_GZIP_OR_ZLIB:
                    ZX_LOG("GZIP/ZLIB is being initialized.\n");
                    z_ret = initialize_inflate(index, zs, 32 + MAX_WBITS);
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

                        /* TODO/BUG: Avail in can be truncated. Fix this.
                         * Bug happens when comp_buf_len is less than header
                         * size. */
                        zs->next_in     = comp_buf;
                        zs->avail_in    = s_read_len;

                        z_ret = inflate_and_update_offset(index, zs, Z_BLOCK);

                        ZX_LOG("[HEADER] z_ret: %d\n", z_ret);
                        if (z_ret == Z_OK) {
                            ZX_LOG("[HEADER] Done reading.\n");
                            read_completed = 1;
                        } else {
                            ZX_LOG("[HEADER] Error reading.\n");
                            return -4;
                        }
                    } // while not read_completed
                    z_ret = initialize_inflate(index, zs, -MAX_WBITS);
                    if (z_ret != Z_OK) return -5;
                    break;
                default:
                    return -6;
            }

            index->stream_state = ZX_STATE_DEFLATE_BLOCKS;

            ZX_LOG("[HEADER] Inflate initialized.\n");

        case ZX_STATE_DEFLATE_BLOCKS:
            zs->next_out  = buffer;
            zs->avail_out = nbytes;
            read_completed = 0;
            while (!read_completed) {
                if(zs->avail_in == 0) {
                    ZX_LOG("[BLOCKS] Reading compressed data.\n");
                    s_read_len = stream->read(stream->context, comp_buf,
                                              comp_buf_len);
                    if (stream->error(stream->context)) return -7;

                    zs->next_in  = comp_buf;
                    zs->avail_in = s_read_len;
                }
                z_ret = inflate_and_update_offset(index, zs, Z_BLOCK);
                ZX_LOG("[BLOCKS] z_ret: %d\n", z_ret);
                if (z_ret == Z_OK) {
                    if (is_on_block_boundary(zs)) {
                        ZX_LOG("[BLOCKS] On block boundary.\n");
                        if (is_last_deflate_block(zs)) {
                            ZX_LOG("[BLOCKS] Last block.\n");
                            read_completed = 1;
                        }
                        if (block_callback != NULL) {
                            ZX_LOG("[BLOCKS] Block boundary callback.\n");
                            s_ret = (*block_callback)(callback_context,
                                                      index, &index->offset,
                                                      read_completed);
                            if(s_ret != 0) return -100;
                        }
                    }
                    if (zs->avail_out == 0) {
                        ZX_LOG("[BLOCKS] Buffer is full.\n");
                        read_completed = 1;
                    }
                } else if (z_ret == Z_STREAM_END) {
                    ZX_LOG("[BLOCKS] End of stream.\n");
                    return 0;
                } else {
                    #ifdef ZX_DEBUG
                    ZX_LOG("[BLOCKS] Read error");
                    if(zs->msg != NULL) {
                        ZX_LOG(": %s", zs->msg);
                    } else {
                        ZX_LOG(".");
                    }
                    ZX_LOG("\n");
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

    /* TODO: Implement whence */

    checkpoint_idx = zidx_get_checkpoint(index, offset);
    checkpoint = checkpoint_idx >= 0 ? &index->list[checkpoint_idx] : NULL;

    if (checkpoint == NULL) {
        ZX_LOG("[SEEK] No checkpoint found.\n");

        f_ret = index->comp_stream->seek(
                                        index->comp_stream->context,
                                        0,
                                        ZX_SEEK_SET);
        if (f_ret < 0) return -2;

        index->stream_state = ZX_STATE_FILE_HEADERS;
        index->offset.comp = 0;
        index->offset.comp_byte = 0;
        index->offset.comp_bits_count = 0;
        index->offset.uncomp = 0;
        index->z_stream->avail_in = 0;
    } else if (
            index->offset.comp < checkpoint->offset.comp
            || index->offset.comp > offset) {
        ZX_LOG("[SEEK] Checkpoint found. (comp: %ld, uncomp: %ld)\n",
                checkpoint->offset.comp, checkpoint->offset.uncomp);
        /* TODO: Fix window size */
        z_ret = initialize_inflate(index, index->z_stream, -MAX_WBITS);
        if (z_ret != Z_OK) return -3;

        f_ret = index->comp_stream->seek(index->comp_stream->context,
                                         checkpoint->offset.comp,
                                         ZX_SEEK_SET);
        if (f_ret < 0) return -4;
        /* TODO: Add stream eof check. */

        if (checkpoint->offset.comp_bits_count > 0) {
            byte = checkpoint->offset.comp_byte;
            byte >>= (8 - checkpoint->offset.comp_bits_count);

            z_ret = inflatePrime(index->z_stream,
                                 checkpoint->offset.comp_bits_count, byte);
            if (z_ret != Z_OK) return -6;
        }

        z_ret = inflateSetDictionary(index->z_stream, checkpoint->window_data,
                                     index->window_size);
        if (z_ret != Z_OK) return -7;

        index->stream_state = ZX_STATE_DEFLATE_BLOCKS;
        index->offset.comp = checkpoint->offset.comp;
        index->offset.comp_bits_count = checkpoint->offset.comp_bits_count;
        index->offset.uncomp = checkpoint->offset.uncomp;
        index->z_stream->avail_in = 0;
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
        if(f_ret == 0) return 1;

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

zidx_checkpoint* zidx_create_checkpoint()
{
    zidx_checkpoint *ckp;
    ckp = calloc(1, sizeof(zidx_checkpoint));
    return ckp;
}

int zidx_fill_checkpoint(zidx_index* index,
                         zidx_checkpoint* new_checkpoint,
                         zidx_checkpoint_offset* offset)
{
    int z_ret;
    unsigned int dict_length;

    if (index == NULL) return -1;
    if (new_checkpoint == NULL) return -2;
    if (offset == NULL) return -3;

    if (new_checkpoint->window_data == NULL) {
        new_checkpoint->window_data = (uint8_t *) malloc(index->window_size);
    }

    memcpy(&new_checkpoint->offset, offset, sizeof(zidx_checkpoint_offset));

    /* TODO: Use dict_length to store variable length window_data maybe? */
    dict_length = index->window_size;

    z_ret = inflateGetDictionary(index->z_stream, new_checkpoint->window_data,
                                 &dict_length);
    if (z_ret != Z_OK) return -4;

    return 0;
}

int zidx_add_checkpoint(zidx_index* index, zidx_checkpoint* checkpoint)
{
    int last_uncomp;
    if (index == NULL) return -1;
    if (checkpoint == NULL) return -2;

    if (index->list_capacity == index->list_count) {
        zidx_extend_index_size(index, index->list_count);
    }

    if (index->list_count > 0) {
        last_uncomp = index->list[index->list_count - 1].offset.uncomp;
        if (checkpoint->offset.uncomp <= last_uncomp) {
            return -3;
        }
    }

    memcpy(&index->list[index->list_count], checkpoint, sizeof(*checkpoint));
    index->list_count++;

    return 0;
}

int zidx_get_checkpoint(zidx_index* index, off_t offset)
{
    #define ZX_OFFSET_(idx) (index->list[idx].offset.uncomp)

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
    if(ZX_OFFSET_(right) < offset) {
        return idx;
    }

    /* Binary search for index. */
    while (left < right) {

        idx = (left + right) / 2;

        /* If current offset is greater, we need to move the range to left by
         * updating `right`. */
        if(ZX_OFFSET_(idx) > offset) {
            right = idx - 1;

        /* If current offset is less than or equal, but also the following one,
         * we need to move the range to right by updating `left`. */
        } else if(ZX_OFFSET_(idx + 1) <= offset) {
            left = idx + 1;

        /* If current offset is less than or equal, and the following one is
         * greater, we found the lower bound, return it. */
        } else {
            return idx;
        }
    }

    return -1;

    #undef ZX_OFFSET_
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


#ifdef __cplusplus
} // extern "C"
#endif
