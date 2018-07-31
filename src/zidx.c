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
    ZX_STATE_FILE_TRAILER,
    ZX_STATE_END_OF_FILE
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
    zidx_stream *comp_stream;
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
    if (z_ret != Z_OK && z_ret != Z_STREAM_END) {
        ZX_LOG("ERROR: inflate (%d).\n", z_ret);
        return z_ret;
    }

    /* Compute number of bytes inflated. */
    comp_bytes_inflated   = available_comp_bytes - zs->avail_in;
    uncomp_bytes_inflated = available_uncomp_bytes - zs->avail_out;

    /* Update byte offsets. */
    index->offset.comp   += comp_bytes_inflated;
    index->offset.uncomp += uncomp_bytes_inflated;

    /* Set bit offsets only if we are in block boundary. */
    /* TODO: Truncating if not in block boundary is probably unnecessary. May be
     * removed in future. */
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

    /* z_ret can be either Z_OK or Z_STREAM_END in here. */
    return z_ret;
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
            ZX_LOG("Initialized inflate successfully.\n");
        }
    } else {
        z_ret = inflateReset2(zs, window_bits);
        if (z_ret == Z_OK) {
            ZX_LOG("Reset inflate successfully.\n");
        }
    }
    return z_ret;
}

/**
 * Read headers of a gzip or zlib file.
 *
 * \param index Index data.
 *
 * \return ZX_RET_OK if successful.
 *         ZX_ERR_STREAM_READ if an error happens while reading from stream.
 *         ZX_ERR_STREAM_EOF if EOF is reached unexpectedly while reading from
 *         stream.
 *         ZX_ERR_ZLIB(...) if zlib returns an error.
 *
 * \note This function assumes index->z_stream->next_out is ensured to be not
 * NULL before calling, although no output should be produced into next_out.
 * This is because inflate() returns Z_STREAM_ERROR if next_out is NULL, even if
 * avail_in is equal to zero.
 */
static int read_headers(zidx_index* index,
                        zidx_block_callback block_callback,
                        void *callback_context)
{
    /* Flag to check if reading header is completed. */
    int header_completed;

    /* Used for storing number of bytes read from stream. */
    int s_read_len;

    /* Used for storing return value of stream functions. */
    int s_ret;

    /* Used for storing return value of zlib calls. */
    int z_ret;

    /* Aliases for frequently used members of index. */
    zidx_stream* stream = index->comp_stream;
    z_stream* zs = index->z_stream;
    uint8_t* buf = index->comp_data_buffer;
    int buf_len  = index->comp_data_buffer_size;

    zs->next_in   = buf;
    zs->avail_in  = 0;

    header_completed = 0;
    while (!header_completed) {
        /* Read from stream if no data is available in buffer. */
        if (zs->avail_in == 0) {
            s_read_len = zidx_stream_read(stream, buf, buf_len);
            s_ret = zidx_stream_error(stream);
            if (s_ret) {
                ZX_LOG("ERROR: Reading from stream (%d).\n", s_ret);
                return ZX_ERR_STREAM_READ;
            }
            if (s_read_len == 0) {
                ZX_LOG("ERROR: Unexpected EOF while reading file header (%d).\n",
                       s_ret);
                return ZX_ERR_STREAM_EOF;
            }
            zs->next_in  = buf;
            zs->avail_in = s_read_len;
        }

        /* Inflate until block boundary. First block boundary is after header,
         * just before the first block. */
        z_ret = inflate_and_update_offset(index, zs, Z_BLOCK);

        if (z_ret == Z_OK) {
            /* Done if in block boundary. */
            if (is_on_block_boundary(zs)) {
                ZX_LOG("Done reading header.\n");
                header_completed = 1;
            } else {
                ZX_LOG("Read part of header. Continuing...\n");
            }
        } else {
            ZX_LOG("Error reading header (%d).\n", z_ret);
            return ZX_ERR_ZLIB(z_ret);
        }
    }

    /* Call block boundary callback if exists. For this first call uncompressed
     * offset in index->offset should be equal to 0. */
    if (block_callback) {
        s_ret = (*block_callback)(callback_context,
                                  index,
                                  &index->offset,
                                  0);
        if (s_ret != 0) {
            ZX_LOG("WARNING: Callback returned non-zero (%d). "
                   "Returning from function.\n", s_ret);
            return ZX_ERR_CALLBACK(s_ret);
        }
    }
    return ZX_RET_OK;
}

static int read_deflate_blocks(zidx_index* index,
                               zidx_block_callback block_callback,
                               void *callback_context)
{
    /* Flag to check if reading blocks is completed. */
    int reading_completed;

    /* Used for storing number of bytes read from stream. */
    int s_read_len;

    /* Used for storing return value of stream functions. */
    int s_ret;

    /* Used for storing return value of zlib calls. */
    int z_ret;

    /* Aliases for frequently used members of index. */
    zidx_stream* stream = index->comp_stream;
    z_stream* zs = index->z_stream;
    uint8_t* buf = index->comp_data_buffer;
    int buf_len  = index->comp_data_buffer_size;

    reading_completed = 0;
    while (!reading_completed) {
        /* Read from stream if no data is available in buffer. */
        if(zs->avail_in == 0) {
            s_read_len = zidx_stream_read(stream, buf, buf_len);
            s_ret = zidx_stream_error(stream);
            if (s_ret) {
                ZX_LOG("ERROR: Reading from stream (%d).\n",
                       s_ret);
                return ZX_ERR_STREAM_READ;
            }
            if (s_read_len == 0) {
                ZX_LOG("ERROR: Unexpected EOF while reading file "
                       "header. (%d).\n", s_ret);
                return ZX_ERR_STREAM_EOF;
            }

            zs->next_in  = buf;
            zs->avail_in = s_read_len;
        }
        if (block_callback == NULL) {
            z_ret = inflate_and_update_offset(index, zs, Z_SYNC_FLUSH);
        } else {
            z_ret = inflate_and_update_offset(index, zs, Z_BLOCK);
        }
        if (z_ret == Z_OK || z_ret == Z_STREAM_END) {
            if (is_on_block_boundary(zs)) {
                ZX_LOG("On block boundary.\n");
                if (is_last_deflate_block(zs)) {
                    ZX_LOG("Also last block.\n");
                    reading_completed = 1;
                    if (index->stream_type == ZX_STREAM_GZIP ||
                            index->stream_type == ZX_STREAM_GZIP_OR_ZLIB) {
                        index->stream_state = ZX_STATE_FILE_TRAILER;
                    }
                }
                if (block_callback != NULL) {
                    ZX_LOG("Calling block boundary callback.\n");
                    s_ret = (*block_callback)(callback_context,
                                              index,
                                              &index->offset,
                                              reading_completed);
                    if (s_ret != 0) {
                        ZX_LOG("WARNING: Callback returned non-zero (%d). "
                               "Returning from function.\n", s_ret);
                        return ZX_ERR_CALLBACK(s_ret);
                    }
                }
            }
            if (zs->avail_out == 0) {
                ZX_LOG("Buffer is full.\n");
                reading_completed = 1;
            }
            if (z_ret == Z_STREAM_END) {
                ZX_LOG("End of stream reached.\n");
                reading_completed = 1;
                if (index->stream_type == ZX_STREAM_GZIP ||
                        index->stream_type == ZX_STREAM_GZIP_OR_ZLIB) {
                    index->stream_state = ZX_STATE_FILE_TRAILER;
                }
            }
        } else {
            if (zs->msg != NULL) {
                ZX_LOG("ERROR: inflate_and_update_offset returned error (%d): "
                       " %s\n", z_ret, zs->msg);
            } else {
                ZX_LOG("ERROR: inflate_and_update_offset returned error (%d).\n",
                       z_ret);
            }

            return ZX_ERR_ZLIB(z_ret);
        }
    } /* while (!read_completed) */

    return ZX_RET_OK;
}

static int read_gzip_trailer(zidx_index* index)
{
    int read_bytes;
    int s_read_len;
    uint8_t trailer[8];
    z_stream* zs;

    zs = index->z_stream;

    read_bytes = zs->avail_in > 8 ? 8 : zs->avail_in;
    memcpy(trailer, zs->next_in, read_bytes);
    zs->next_in += read_bytes;
    zs->avail_in -= read_bytes;
    if (read_bytes < 8) {
        s_read_len = zidx_stream_read(index->comp_stream, trailer, 8 - read_bytes);
        if (s_read_len != 8 - read_bytes) {
            ZX_LOG("ERROR: File ended before trailer ends.");
            return ZX_ERR_STREAM_EOF;
        }
    }
    return ZX_RET_OK;
}

zidx_index* zidx_index_create()
{
    zidx_index *index;
    index = (zidx_index*) malloc(sizeof(zidx_index));
    return index;
}

int zidx_index_init(zidx_index* index,
                     zidx_stream* comp_stream)
{
    return zidx_index_init_advanced(index,
                                    comp_stream,
                                    ZX_STREAM_GZIP_OR_ZLIB,
                                    ZX_CHECKSUM_DEFAULT,
                                    NULL,
                                    ZX_DEFAULT_INITIAL_LIST_CAPACITY,
                                    ZX_DEFAULT_WINDOW_SIZE,
                                    ZX_DEFAULT_COMPRESSED_DATA_BUFFER_SIZE,
                                    ZX_DEFAULT_SEEKING_DATA_BUFFER_SIZE);
}

int zidx_index_init_advanced(zidx_index* index,
                             zidx_stream* comp_stream,
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
        ZX_LOG("ERROR: index or comp_stream is null.\n");
        return ZX_ERR_PARAMS;
    }
    if (stream_type != ZX_STREAM_GZIP && stream_type != ZX_STREAM_DEFLATE
            && stream_type != ZX_STREAM_GZIP_OR_ZLIB) {
        ZX_LOG("ERROR: Unknown stream_type (%d).\n", (int) stream_type);
        return ZX_ERR_PARAMS;
    }
    if (checksum_option != ZX_CHECKSUM_DISABLED
            && checksum_option != ZX_CHECKSUM_DEFAULT
            && checksum_option != ZX_CHECKSUM_FORCE_CRC32
            && checksum_option != ZX_CHECKSUM_FORCE_ADLER32) {
        ZX_LOG("ERROR: Unknown checksum_option (%d).\n", (int) stream_type);
        return ZX_ERR_PARAMS;
    }
    if (initial_capacity <= 0) {
        ZX_LOG("ERROR: initial_capacity is nonpositive.\n");
        return ZX_ERR_PARAMS;
    }
    if (window_size <= 0) {
        ZX_LOG("ERROR: window_size is nonpositive.\n");
        return ZX_ERR_PARAMS;
    }
    if (window_size < 512 || window_size > 32768) {
        ZX_LOG("ERROR: window_size should be between 512-32768 inclusive.\n");
        return ZX_ERR_PARAMS;
    }
    for (window_bits = 9; window_bits <= 15; window_bits++) {
        if (window_size == (1 << window_bits)) {
            break;
        }
        if (window_size < (1 << window_bits)) {
            ZX_LOG("ERROR: window_size should be a power of 2.\n");
            return ZX_ERR_PARAMS;
        }
    }
    if (comp_data_buffer_size <= 0) {
        ZX_LOG("ERROR: comp_data_buffer_size is nonpositive.\n");
        return ZX_ERR_PARAMS;
    }
    if (seeking_data_buffer_size <= 0) {
        ZX_LOG("ERROR: seeking_data_buffer_size is nonpositive.\n");
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
    if (!list) {
        ZX_LOG("ERROR: Couldn't allocate memory for checkpoint list.\n");
        goto memory_fail;
    }

    /* Initialize compressed data buffer. */
    comp_data_buffer = (uint8_t*) malloc(comp_data_buffer_size);
    if (!comp_data_buffer) {
        ZX_LOG("ERROR: Couldn't allocate memory for compression data buffer.\n");
        goto memory_fail;
    }

    /* Initialize seeking data buffer. */
    seeking_data_buffer = (uint8_t*) malloc(seeking_data_buffer_size);
    if (!seeking_data_buffer) {
        ZX_LOG("ERROR: Couldn't allocate memory for seeking data buffer.\n");
        goto memory_fail;
    }

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

    ZX_LOG("Initialization was successful.\n");

    return ZX_RET_OK;

memory_fail:
    if(free_zs_on_failure) free(z_stream_ptr);
    free(list);
    free(comp_data_buffer);
    free(seeking_data_buffer);
    return ZX_ERR_MEMORY;
}

int zidx_index_destroy(zidx_index* index)
{
    /* Return value for this function. */
    int ret;

    /* Return value used for zlib calls. */
    int z_ret;

    /* Iterator and end pointer used for iterating over checkpoints. */
    zidx_checkpoint *it;
    zidx_checkpoint *end;

    /* If index is NULL, okay is returned (to be consistent with free). */
    if (!index) return ZX_RET_OK;

    /* Unless an error happens, okay will be returned. */
    ret = ZX_RET_OK;

    /* If z_stream should not be NULL. */
    if (!index->z_stream) {
        ret = ZX_ERR_CORRUPTED;
    } else {
        /* Release internal buffers of z_stream. */
        if (index->inflate_initialized) {
            z_ret = inflateEnd(index->z_stream);
            if (z_ret != Z_OK) { ret = ZX_ERR_CORRUPTED; }
        }
        free(index->z_stream);
    }

    /* Checkpoint list should not be NULL. */
    if (!index->list) {
        ret = ZX_ERR_CORRUPTED;
    } else {
        /* Release window data on each checkpoint. */
        end = index->list + index->list_count;
        for (it = index->list; it < end; it++) {
            free(it->window_data);
        }
        free(index->list);
    }

    return ret;
}

int zidx_read(zidx_index* index, uint8_t *buffer, int nbytes)
{
    /* Pass to explicit version without block callbacks. */
    return zidx_read_advanced(index, buffer, nbytes, NULL, NULL);
}

int zidx_read_advanced(zidx_index* index,
                       uint8_t *buffer,
                       int nbytes,
                       zidx_block_callback block_callback,
                       void *callback_context)
{
    /* TODO: Implement support for concatanated gzip streams. */
    ZX_LOG("Reading at (comp: %jd, uncomp: %jd)\n", (intmax_t)index->offset.comp,
           (intmax_t)index->offset.uncomp);

    /* Return value for private (static) function calls. */
    int ret;

    /* Return value for zlib calls. */
    int z_ret;

    /* Window bits used for initializing inflate for headers. Window bits in
     * index can't be used for this purpose, because this variable will be used
     * for denoting stream type as well. */
    int window_bits;

    /* Aliases for frequently used index members. */
    z_stream *zs = index->z_stream;

    switch (index->stream_state) {
        case ZX_STATE_FILE_HEADERS:
            /* Assign window_bits with respect to stream type. */
            switch (index->stream_type) {
                case ZX_STREAM_DEFLATE:
                    window_bits = -index->window_bits;
                    break;
                case ZX_STREAM_GZIP:
                    window_bits = 16 + index->window_bits;
                    break;
                case ZX_STREAM_GZIP_OR_ZLIB:
                    window_bits = 32 + index->window_bits;
                    break;
            }

            /* Initialize inflate. Window bits should have been initialized by
             * zidx_index_init() per stream type. */
            z_ret = initialize_inflate(index, zs, window_bits);
            if (z_ret != Z_OK) {
                ZX_LOG("ERROR: inflate initialization returned error (%d).\n",
                       z_ret);
                return ZX_ERR_ZLIB(z_ret);
            }

            if (index->stream_type != ZX_STREAM_DEFLATE) {
                /* If stream type is not DEFLATE, then read headers. Since no
                 * output will be produced avail_out will be 0. However,
                 * assigning next_out to NULL causes error when calling inflate,
                 * so leave it as a non-NULL value even if it's not gonna be
                 * used. */
                zs->next_out  = buffer;
                zs->avail_out = 0;

                ret = read_headers(index, block_callback, callback_context);
                if (ret != ZX_RET_OK) {
                    ZX_LOG("ERROR: While reading headers (%d).\n", ret);
                    return ret;
                }

                /* Then initialize inflate to be used for DEFLATE blocks. This
                 * is preferable way because seeking messes with internal
                 * checksum computation of zlib. Best way to disable it to treat
                 * each gzip/zlib blocks as individual deflate blocks, and
                 * control checksum in-house. index->window_bits used
                 * intentionally instead of local variable window_bits, since
                 * inflate will be initialized as deflate. */
                z_ret = initialize_inflate(index, zs, -index->window_bits);
                if (z_ret != Z_OK) {
                    ZX_LOG("ERROR: initialize_inflate returned error (%d).\n",
                           z_ret);
                    return ZX_ERR_ZLIB(z_ret);
                }
            }

            ZX_LOG("Done reading header.\n");
            index->stream_state = ZX_STATE_DEFLATE_BLOCKS;

            /* Continue to next case to handle first deflate block. */

        case ZX_STATE_DEFLATE_BLOCKS:
            /* Input buffer (next_in, avail_in) shouldn't be modified here, as
             * there could be data left from previous reading. */

            /* Set output buffer and available bytes for output. */
            zs->next_out  = buffer;
            zs->avail_out = nbytes;

            ret = read_deflate_blocks(index, block_callback, callback_context);
            if (ret != ZX_RET_OK) {
                ZX_LOG("ERROR: While reading deflate blocks (%d).\n", ret);
                return ret;
            }
            /* Break switch if there are more blocks. */
            if (index->stream_state != ZX_STATE_FILE_TRAILER) {
                break;
            }
        case ZX_STATE_FILE_TRAILER:
            /* TODO/BUG: Implement zlib separately. THIS IS TEMPORARY!!! */
            if (index->stream_type != ZX_STREAM_DEFLATE) {
                ret = read_gzip_trailer(index);
                if (ret != ZX_RET_OK) {
                    ZX_LOG("ERROR: While parsing gzip file trailer (%d).\n",
                           ret);
                    return ret;
                }
            }
            index->stream_state = ZX_STATE_END_OF_FILE;
            break;
        case ZX_STATE_INVALID:
            /* TODO: Implement this. */
            ZX_LOG("ERROR: NOT IMPLEMENTED YET.\n");
            return ZX_ERR_CORRUPTED;

        case ZX_STATE_END_OF_FILE:
            ZX_LOG("No reading is made since state is end-of-file.\n");
            return 0;

        default:
            ZX_LOG("ERROR: Unknown state (%d).\n", (int)index->stream_state);
            return ZX_ERR_CORRUPTED;

    } /* end of switch(index->stream_state) */

    /* Return number of bytes read into the buffer. */
    return zs->next_out - buffer;
}

int zidx_seek(zidx_index* index, off_t offset, int whence)
{
    return zidx_seek_advanced(index, offset, whence, NULL, NULL);
}

int zidx_seek_advanced(zidx_index* index,
                       off_t offset,
                       int whence,
                       zidx_block_callback block_callback,
                       void *callback_context)
{
    /* TODO: If this function fails to reset z_stream, it will leave z_stream in
     * an invalid state. Must be handled. */

    /* Used for storing number of bytes read from stream. */
    int s_read_len;

    /* Used for storing return value of stream functions. */
    int s_ret;

    /* Used for storing return value of zlib calls. */
    int z_ret;

    /* Used for storing return value of zidx calls. */
    int zx_ret;

    /* Used for storing shared byte between two blocks in the boundary, if they
     * share any. */
    uint8_t byte;

    /* Used for finding the checkpoint preceding offset. */
    zidx_checkpoint *checkpoint;
    int checkpoint_idx;

    /* Number of bytes remaining to arrive given offset. After seeking to
     * checkpoint, the rest of offset is disposed using zidx_read. */
    off_t num_bytes_remaining;

    /* Number of bytes to dispose for next zidx_read call. */
    int num_bytes_next;

    /* TODO: Implement whence. Currently it is ignored. */

    checkpoint_idx = zidx_get_checkpoint(index, offset);
    checkpoint = checkpoint_idx >= 0 ? &index->list[checkpoint_idx] : NULL;

    if (checkpoint == NULL) {
        ZX_LOG("No checkpoint found.\n");

        /* Seek to the beginning of file, since no checkpoint has been found. */
        s_ret = zidx_stream_seek(index->comp_stream, 0, ZX_SEEK_SET);
        if (s_ret < 0) {
            ZX_LOG("ERROR: Couldn't seek in stream (%d).\n", s_ret);
            return ZX_ERR_STREAM_SEEK;
        }

        /* Reset stream states and offsets. TODO: It may be unnecessary to update
         * comp_byte and comp_bits_count. */
        index->stream_state           = ZX_STATE_FILE_HEADERS;
        index->offset.comp            = 0;
        index->offset.comp_byte       = 0;
        index->offset.comp_bits_count = 0;
        index->offset.uncomp          = 0;

        /* Dispose if there's anything in input buffer. */
        index->z_stream->avail_in = 0;
    } else if (
            index->offset.comp < checkpoint->offset.comp
            || index->offset.comp > offset) {
        /* If offset is between checkpoint and current index offset, jump to the
         * checkpoint. */
        ZX_LOG("Jumping to checkpoint (idx: %d, comp: %ld, uncomp: %ld).\n",
                checkpoint_idx, checkpoint->offset.comp,
                checkpoint->offset.uncomp);

        /* Initialize as deflate. */
        z_ret = initialize_inflate(index, index->z_stream, -index->window_bits);
        if (z_ret != Z_OK) {
            ZX_LOG("ERROR: inflate initialization returned error (%d).\n",
                   z_ret);
            return ZX_ERR_ZLIB(z_ret);
        }

        /* Seek to the checkpoint offset in compressed stream. */
        s_ret = zidx_stream_seek(index->comp_stream,
                                 checkpoint->offset.comp,
                                 ZX_SEEK_SET);
        if (s_ret != 0) {
            ZX_LOG("ERROR: Couldn't seek in stream (%d).\n", s_ret);
            return ZX_ERR_STREAM_SEEK;
        }

        /* Handle if there is a byte shared between two consecutive blocks. */
        if (checkpoint->offset.comp_bits_count > 0) {
            /* Higher bits of the byte should be pushed to zlib before calling
             * inflate. */
            byte = checkpoint->offset.comp_byte;
            byte >>= (8 - checkpoint->offset.comp_bits_count);

            /* Push these bits to zlib. */
            z_ret = inflatePrime(index->z_stream,
                                 checkpoint->offset.comp_bits_count,
                                 byte);
            if (z_ret != Z_OK) {
                ZX_LOG("ERROR: inflatePrime error (%d).\n", z_ret);
                return ZX_ERR_ZLIB(z_ret);
            }
        }

        /* Copy window from checkpoint. */
        z_ret = inflateSetDictionary(index->z_stream,
                                     checkpoint->window_data,
                                     index->window_size);
        if (z_ret != Z_OK) {
            ZX_LOG("ERROR: inflateSetDictionary error (%d).\n", z_ret);
            return ZX_ERR_ZLIB(z_ret);
        }

        /* Set stream states and offsets. TODO: It may be unnecessary to update
         * comp_byte and comp_bits_count. */
        index->stream_state           = ZX_STATE_DEFLATE_BLOCKS;
        index->offset.comp            = checkpoint->offset.comp;
        index->offset.comp_byte       = byte;
        index->offset.comp_bits_count = checkpoint->offset.comp_bits_count;
        index->offset.uncomp          = checkpoint->offset.uncomp;

        /* Dispose if there's anything in input buffer. */
        index->z_stream->avail_in = 0;
    }

    /* Whether we jump to somewhere in file or not, we need to consume remaining
     * bytes until the offset by decompressing. */
    num_bytes_remaining = offset - index->offset.uncomp;
    while (num_bytes_remaining > 0) {
        /* Number of bytes going to consumed in next zidx read call is equal to
         * min(num_bytes_remaining, seeking_data_buffer_size). */
        num_bytes_next =
            (num_bytes_remaining > index->seeking_data_buffer_size ?
                index->seeking_data_buffer_size :
                num_bytes_remaining);

        /* Read next compressed data and decompress it using internal seeking
         * buffer. */
        s_read_len = zidx_read_advanced(index,
                                        index->seeking_data_buffer,
                                        num_bytes_next,
                                        block_callback,
                                        callback_context);
        /* Handle error. */
        if (s_read_len < 0) {
            ZX_LOG("ERROR: Couldn't decompress remaining data while "
                   "seeking (%d)\n.", s_read_len);
            return s_read_len;
        }

        /* Handle end-of-file. */
        if (s_read_len == 0) {
            ZX_LOG("ERROR: Unexpected end-of-file while decompressing remaining "
                   "data for seeking (%d)\n.", s_read_len);
            return ZX_ERR_STREAM_EOF;
        }

        /* Decrease number of bytes read. */
        num_bytes_remaining -= s_read_len;

        #ifdef ZX_DEBUG
        /* Normally this should be unnecessary, since zidx_read_advanced can't
         * return more than buffer length, but anyway let's check it in
         * debugging mode. */
        if (num_bytes_remaining < 0) {
            ZX_LOG("ERROR: Screwed number of bytes remaining while reading for "
                   "seeking. Check the code. (num_bytes_remaining: %jd)\n",
                   (intmax_t) num_bytes_remaining);
            return ZX_ERR_CORRUPTED;
        }
        #endif
    }

    return ZX_RET_OK;
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
                         const zidx_stream *stream,
                         zidx_import_filter_callback filter,
                         void *filter_context) { return 0; }

int zidx_export_advanced(zidx_index *index,
                         const zidx_stream *stream,
                         zidx_export_filter_callback filter,
                         void *filter_context) { return 0; }

int zidx_import(zidx_index *index, FILE* input_index_file)
{
    const zidx_stream input_stream = {
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
    const zidx_stream output_stream = {
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
