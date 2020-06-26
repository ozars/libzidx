#include "zidx.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <streamlike.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ZX_DEBUG
#define ZX_LOG(...) \
    do { \
        fprintf(stderr, "%s:%d:%s: ", __FILE__, __LINE__, __func__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } while(0)
#else
#define ZX_LOG(...) while(0)
#endif

uint8_t zx_magic_prefix[] = {'Z', 'I', 'D', 'X'};
uint8_t zx_version_prefix[] = {0, 0};

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
    uint16_t window_length;
    uint8_t *window_data;
};

struct zidx_index_s
{
    streamlike_t *comp_stream;
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
    off_t compressed_size;
    off_t uncompressed_size;
};

/**
 * Returns the combined crc32 checksum from two crc checksums.
 *
 * This function is used as a helper function for updating checksums of modified blocks in file
 *
 * \param crc1 First crc checksum
 * \param crc2 Second crc checksum
 * \param len2 Length of the second sequence of data (not the length of the checksum, but the length of the data the checksum was computed from)
 *
 * \return The combined crc32 checksum of the input checksums
 *
 * TODO: Update for 32 bit return instead of 64 bit.
 */
unsigned long crc32_combine(uLong crc1,uLong crc2,z_off_t len2)
{

	Bytef *c=calloc(0,sizeof(char)*len2);
	if(!c)
	{
		free(c);
		return ZX_ERR_MEMORY;
	}
	unsigned long crc_ret=crc32(crc1,c,len2);

	unsigned long crc_ret_2=crc32(0L,Z_NULL,0);
	crc_ret_2=crc32(crc_ret_2,c,len2);

	free(c);
	return crc_ret ^ crc2 ^ crc_ret_2;
}

/**
 * Given a crc checksum and a combined checksum, returns the extracted checksum from the combined checksum.
 * For example, given data segments A and B, and their respective checksums chk_A, chk_B, create a combined checksum of blocks A and B called chk_AB.
 * Extract function returns chk_B given chk_A and chk_AB.
 *
 * This function is used as a helper function for updating checksums of modified blocks in file
 *
 * \param crc1 First crc checksum
 * \param crc2 Second crc checksum. Combined checksum of crc1 and the checksum of another block of data
 * \param len2 Length of the second block of data (not the length of the checksum but of the data)
 * \return The extracted checksum of the second block of data from a combined checksum.
 */
unsigned long crc32_extract(uLong crc1,uLong crc2,z_off_t len2)
{
	Bytef *c=calloc(0,sizeof(char)*len2);

	if(!c)
	{
			free(c);
			return ZX_ERR_MEMORY;
	}
	unsigned long crc_ret=crc32(0L,Z_NULL,0);
	crc_ret=crc32(crc_ret,c,len2);

	unsigned long crc_ret_2=crc32(crc1,c,len2);
	free(c);
	return crc2 ^ crc_ret ^ crc_ret_2;
}

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
static inline uint8_t get_unused_bits_count(z_stream* zs)
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
static inline int is_last_deflate_block(z_stream* zs)
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
static inline int is_on_block_boundary(z_stream *zs)
{
    return zs->data_type & 128;
}

/**
 * Inflate using buffers from zs, and update index->offset accordingly.
 *
 * This function is just a wrapper around inflate() function of zlib library.
 * It updates offsets in index data after inflating.
 *
 * \param index Index data.
 * \param zs    zlib stream data.
 * \param flush flush parameter to pass as a second argument to inflate().
 *
 * \return The return value of inflate() call.
 */
static int inflate_and_update_offset(zidx_index* index, z_stream* zs,
                                     int flush)
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

    /* Sanity checks. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (zs == NULL) {
        ZX_LOG("ERROR: z_stream argument zs is NULL.");
        return ZX_ERR_PARAMS;
    }

    /* Note: No sanity check is applied to flush. Instead error returned by
     * inflate will be passed in case of an invalid flush value. */

    /* Save number of bytes in input/output buffers. */
    available_comp_bytes   = zs->avail_in;
    available_uncomp_bytes = zs->avail_out;

    /* If no data is available to decompress return. If this check is not made,
     * setting index->offset.comp_byte may underflow while updating offset. */
    if (available_comp_bytes == 0) return Z_OK;

    /* Use zlib to inflate data. */
    z_ret = inflate(zs, flush);
    if (z_ret != Z_OK && z_ret != Z_STREAM_END) {
        ZX_LOG("ERROR: inflate (%d).", z_ret);
        return z_ret;
    }

    /* Compute number of bytes inflated. */
    comp_bytes_inflated   = available_comp_bytes - zs->avail_in;
    uncomp_bytes_inflated = available_uncomp_bytes - zs->avail_out;

    /*Compute checksum*/
    //Byte array we're computing on starts at (zs_avail_out-comp_bytes_inflated) and is comp_bytes_inflated bytes long

	uint32_t block_checksum=crc32(0L,Z_NULL,0);
	block_checksum=crc32(block_checksum,zs->next_out-uncomp_bytes_inflated,uncomp_bytes_inflated);


    /* Update byte offsets. */
    index->offset.comp   += comp_bytes_inflated;
    index->offset.uncomp += uncomp_bytes_inflated;

    /* Set bit offsets only if we are in block boundary. */
    /* TODO: Truncating if not in block boundary is probably unnecessary. May
     * be removed in future. */
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

    /* Sanity checks. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (zs == NULL) {
        ZX_LOG("ERROR: z_stream argument zs is NULL.");
        return ZX_ERR_PARAMS;
    }

    if (!index->inflate_initialized) {
        z_ret = inflateInit2(zs, window_bits);
        if (z_ret == Z_OK) {
            index->inflate_initialized = 1;
            ZX_LOG("Initialized inflate successfully.");
        } else {
            ZX_LOG("ERROR: inflateInit2 returned error (%d).", z_ret);
        }
    } else {
        z_ret = inflateReset2(zs, window_bits);
        if (z_ret == Z_OK) {
            ZX_LOG("Reset inflate successfully.");
        } else {
            ZX_LOG("ERROR: inflateReset2 returned error (%d).", z_ret);
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
 * This is because inflate() returns Z_STREAM_ERROR if next_out is NULL, even
 * if avail_in is equal to zero.
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

    /* Sanity check. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }

    /* Aliases for frequently used members of index. */
    streamlike_t* stream = index->comp_stream;
    z_stream* zs = index->z_stream;
    uint8_t* buf = index->comp_data_buffer;
    int buf_len  = index->comp_data_buffer_size;

    zs->next_in   = buf;
    zs->avail_in  = 0;


    header_completed = 0;
    while (!header_completed) {
        /* Read from stream if no data is available in buffer. */
        if (zs->avail_in == 0) {
            s_read_len = sl_read(stream, buf, buf_len);
            s_ret = sl_error(stream);
            if (s_ret) {
                ZX_LOG("ERROR: Reading from stream (%d).", s_ret);
                return ZX_ERR_STREAM_READ;
            }
            if (s_read_len == 0) {
                ZX_LOG("ERROR: Unexpected EOF while reading file header (%d).",
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
                ZX_LOG("Done reading header.");
                header_completed = 1;
            } else {
                ZX_LOG("Read part of header. Continuing...");
            }
        } else {
            ZX_LOG("Error reading header (%d).", z_ret);
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
                   "Returning from function.", s_ret);
            return s_ret;
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

    /* Sanity check. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }

    /* Aliases for frequently used members of index. */
    streamlike_t* stream = index->comp_stream;
    z_stream* zs = index->z_stream;
    uint8_t* buf = index->comp_data_buffer;
    int buf_len  = index->comp_data_buffer_size;

    reading_completed = 0;
    while (!reading_completed) {
        /* Read from stream if no data is available in buffer. */
        if(zs->avail_in == 0) {
            s_read_len = sl_read(stream, buf, buf_len);
            s_ret = sl_error(stream);
            if (s_ret) {
                ZX_LOG("ERROR: Reading from stream (%d).",
                       s_ret);
                return ZX_ERR_STREAM_READ;
            }
            if (s_read_len == 0) {
                ZX_LOG("ERROR: Unexpected EOF while reading file "
                       "header. (%d).", s_ret);
                return ZX_ERR_STREAM_EOF;
            }

            zs->next_in  = buf;
            zs->avail_in = s_read_len;
        }
        if (block_callback == NULL)
        {
            z_ret = inflate_and_update_offset(index, zs, Z_SYNC_FLUSH);
        }
        else
        {
            z_ret = inflate_and_update_offset(index, zs, Z_BLOCK);
        }



        if (z_ret == Z_OK || z_ret == Z_STREAM_END)
        {
            if (is_on_block_boundary(zs))
            {
                ZX_LOG("On block boundary.");

                if (is_last_deflate_block(zs))
                {
                    ZX_LOG("Also last block.");
                    reading_completed = 1;
                    if (index->stream_type == ZX_STREAM_GZIP ||
                            index->stream_type == ZX_STREAM_GZIP_OR_ZLIB) {
                        index->stream_state = ZX_STATE_FILE_TRAILER;
                    }
                }
                if (block_callback != NULL) {
                    ZX_LOG("Calling block boundary callback.");
                    s_ret = (*block_callback)(callback_context,
                                              index,
                                              &index->offset,
                                              reading_completed);
                    if (s_ret != 0) {
                        ZX_LOG("WARNING: Callback returned non-zero (%d). "
                               "Returning from function.", s_ret);
                        return s_ret;
                    }
                }
            }
            if (zs->avail_out == 0)
            {
                ZX_LOG("Buffer is full.");
                reading_completed = 1;
            }
            if (z_ret == Z_STREAM_END)
            {
                ZX_LOG("End of stream reached.");
                reading_completed = 1;
                if (index->stream_type == ZX_STREAM_GZIP || index->stream_type == ZX_STREAM_GZIP_OR_ZLIB)
                {
                    index->stream_state = ZX_STATE_FILE_TRAILER;
                }
            }
        } else {
            if (zs->msg != NULL) {
                ZX_LOG("ERROR: inflate_and_update_offset returned error (%d): "
                       " %s", z_ret, zs->msg);
            } else {
                ZX_LOG("ERROR: inflate_and_update_offset returned error (%d).",
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

    /* Sanity check. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }

    /* Aliases. */
    streamlike_t* stream = index->comp_stream;
    z_stream* zs = index->z_stream;

    /* Number of bytes already read into buffer. */
    read_bytes = zs->avail_in > 8 ? 8 : zs->avail_in;

    /* Copy those bytes from buffer to trailer, and update buffer data. */
    memcpy(trailer, zs->next_in, read_bytes);
    zs->next_in  += read_bytes;
    zs->avail_in -= read_bytes;
    index->offset.comp += read_bytes;

    /* If there are more data to be read for trailer... */
    if (read_bytes < 8) {
        /* ...read it from stream. */
        s_read_len = sl_read(stream, trailer, 8 - read_bytes);

        if (sl_error(stream) != 0) {
            ZX_LOG("ERROR: Error while reading remaining %d bytes of trailer "
                   " from stream.", 8 - read_bytes);
            return ZX_ERR_STREAM_READ;
        }
        index->offset.comp += s_read_len;
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

int zidx_index_init(zidx_index *index,
                    streamlike_t *comp_stream)
{
    return zidx_index_init_ex(index,
                              comp_stream,
                              ZX_STREAM_GZIP_OR_ZLIB,
                              ZX_CHECKSUM_DEFAULT,
                              NULL,
                              ZX_DEFAULT_INITIAL_LIST_CAPACITY,
                              ZX_DEFAULT_WINDOW_SIZE,
                              ZX_DEFAULT_COMPRESSED_DATA_BUFFER_SIZE,
                              ZX_DEFAULT_SEEKING_DATA_BUFFER_SIZE);
}

int zidx_index_init_ex(zidx_index *index,
                       streamlike_t *comp_stream,
                       zidx_stream_type stream_type,
                       zidx_checksum_option checksum_option,
                       z_stream* z_stream_ptr,
                       int initial_capacity,
                       int window_size,
                       int comp_data_buffer_size,
                       int seeking_data_buffer_size)
{
    /* Temporary variables that will be used to initialize corresponding
     * members of index. These are not modified directly on index, because we
     * don't want to modify it in case of a failure. */
    zidx_checkpoint *list;
    uint8_t* comp_data_buffer;
    uint8_t* seeking_data_buffer;
    int window_bits;

    /* Flag used to indicate whether z_stream_ptr argument should be released
     * in case of a failure. */
    int free_zs_on_failure;

    /* Sanity checks. */
    if (index == NULL || comp_stream == NULL) {
        ZX_LOG("ERROR: index or comp_stream is null.");
        return ZX_ERR_PARAMS;
    }
    if (stream_type != ZX_STREAM_GZIP && stream_type != ZX_STREAM_DEFLATE
            && stream_type != ZX_STREAM_GZIP_OR_ZLIB) {
        ZX_LOG("ERROR: Unknown stream_type (%d).", (int) stream_type);
        return ZX_ERR_PARAMS;
    }
    if (checksum_option != ZX_CHECKSUM_DISABLED
            && checksum_option != ZX_CHECKSUM_DEFAULT
            && checksum_option != ZX_CHECKSUM_FORCE_CRC32
            && checksum_option != ZX_CHECKSUM_FORCE_ADLER32) {
        ZX_LOG("ERROR: Unknown checksum_option (%d).", (int) stream_type);
        return ZX_ERR_PARAMS;
    }
    if (initial_capacity < 0) {
        ZX_LOG("ERROR: initial_capacity is negative.");
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
     * indicate that z_stream_ptr is allocated by this function and it should
     * be deallocated in case of a failure. */
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

    /* Initialize list if initial_capacity is not zero. */
    if (initial_capacity > 0) {
        list = (zidx_checkpoint*)malloc(sizeof(zidx_checkpoint)
                                            * initial_capacity);
        if (!list) {
            ZX_LOG("ERROR: Couldn't allocate memory for checkpoint list.");
            goto memory_fail;
        }
    }

    /* Initialize compressed data buffer. */
    comp_data_buffer = (uint8_t*) malloc(comp_data_buffer_size);
    if (!comp_data_buffer) {
        ZX_LOG("ERROR: Couldn't allocate memory for compression data buffer.");
        goto memory_fail;
    }

    /* Initialize seeking data buffer. */
    seeking_data_buffer = (uint8_t*) malloc(seeking_data_buffer_size);
    if (!seeking_data_buffer) {
        ZX_LOG("ERROR: Couldn't allocate memory for seeking data buffer.");
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

    /* Set default size. */
    index->compressed_size = -1;
    index->uncompressed_size = -1;

    ZX_LOG("Initialization was successful.");

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

    /* If index is NULL, return error. */
    if (!index) {
        ZX_LOG("Nothing is destroyed in index, since it's NULL.");
        return ZX_ERR_PARAMS;
    }

    /* z_stream should not be NULL. */
    if (!index->z_stream) {
        ZX_LOG("ERROR: index->z_stream is NULL.");
        return ZX_ERR_CORRUPTED;
    }

    /* Checkpoint list should not be NULL if there is some capacity. */
    if (!index->list && index->list_capacity > 0) {
        ZX_LOG("ERROR: index->list is NULL, although capacity is %d.",
               index->list_capacity);
        return ZX_ERR_CORRUPTED;
    }

    /* Checkpoint list should be NULL if there is no capacity. */
    if (index->list != NULL && index->list_capacity == 0) {
        ZX_LOG("ERROR: index->list is not NULL, although capacity is %d.",
               index->list_capacity);
        return ZX_ERR_CORRUPTED;
    }

    /* Unless an error happens, okay will be returned. */
    ret = ZX_RET_OK;

    /* Since z_stream is not NULL, release internal buffers of z_stream. */
    if (index->inflate_initialized) {
        z_ret = inflateEnd(index->z_stream);
        if (z_ret != Z_OK) {
            ZX_LOG("ERROR: inflateEnd returned error (%d).", z_ret);
            ret = ZX_ERR_ZLIB(z_ret);
        }
    }
    /* If internal buffers are released succesfully, release the z_stream
     * itself. */
    if (ret == ZX_RET_OK) {
        free(index->z_stream);
        index->z_stream = NULL;
    }

    /* If index-> list is not NULL, free it. */
    if (index->list) {
        /* Release window data on each checkpoint. */
        end = index->list + index->list_count;
        for (it = index->list; it < end; it++) {
            free(it->window_data);
        }
        free(index->list);

        /* These members are updated because user can call this function again
         * if it returns some error, so we are leaving index list in a valid
         * state. */
        index->list = NULL;
        index->list_capacity = 0;
        index->list_count = 0;
    }
    /* Else is unnecessary, since this practically means capacity is zero and
     * list is NULL. Therefore, nothing to free.  */

    /* Release buffers */
    free(index->seeking_data_buffer);
    index->seeking_data_buffer = NULL;
    free(index->comp_data_buffer);
    index->comp_data_buffer = NULL;

    return ret;
}

int zidx_read(zidx_index* index, void *buffer, int nbytes)
{
    /* Pass to explicit version without block callbacks. */
    return zidx_read_ex(index, buffer, nbytes, NULL, NULL);
}

int zidx_read_ex(zidx_index* index,
                 void *buffer,
                 int nbytes,
                 zidx_block_callback block_callback,
                 void *callback_context)
{
    /* TODO: Implement support for concatnated gzip streams. */

    /* Return value for private (static) function calls. */
    int ret;

    /* Return value for zlib calls. */
    int z_ret;

    /* Total number of bytes read. */
    int total_read = 0;

    /* Window bits used for initializing inflate for headers. Window bits in
     * index can't be used for this purpose, because this variable will be used
     * for denoting stream type as well. */
    int window_bits;

    /* Sanity checks. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (buffer == NULL) {
        ZX_LOG("ERROR: buffer is NULL.");
        return ZX_ERR_PARAMS;
    }
    /* TODO: I couldn't decide whether nbytes = 0 makes sense, so I left it as
     * a valid option. Use with caution. */
    if (nbytes < 0) {
        ZX_LOG("ERROR: nbytes can't be negative.");
        return ZX_ERR_PARAMS;
    }

    /* Aliases. */
    z_stream *zs = index->z_stream;

    ZX_LOG("Reading %d bytes at (comp: %jd, uncomp: %jd)", nbytes,
           (intmax_t)index->offset.comp, (intmax_t)index->offset.uncomp);

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
                ZX_LOG("ERROR: inflate initialization returned error (%d).",
                       z_ret);
                index->stream_state = ZX_STATE_INVALID;
                return ZX_ERR_ZLIB(z_ret);
            }

            if (index->stream_type != ZX_STREAM_DEFLATE) {
                /* If stream type is not DEFLATE, then read headers. Since no
                 * output will be produced avail_out will be 0. However,
                 * assigning next_out to NULL causes error when calling
                 * inflate, so leave it as a non-NULL value even if it's not
                 * gonna be used. */
                zs->next_out  = buffer;
                zs->avail_out = 0;

                ret = read_headers(index, block_callback, callback_context);
                if (ret != ZX_RET_OK) {
                    ZX_LOG("ERROR: While reading headers (%d).", ret);
                    index->stream_state = ZX_STATE_INVALID;
                    return ret;
                }

                /* Then initialize inflate to be used for DEFLATE blocks. This
                 * is preferable way because seeking messes with internal
                 * checksum computation of zlib. Best way to disable it to
                 * treat each gzip/zlib blocks as individual deflate blocks,
                 * and control checksum in-house. index->window_bits used
                 * intentionally instead of local variable window_bits, since
                 * inflate will be initialized as deflate. */
                z_ret = initialize_inflate(index, zs, -index->window_bits);
                if (z_ret != Z_OK) {
                    ZX_LOG("ERROR: initialize_inflate returned error (%d).",
                           z_ret);
                    index->stream_state = ZX_STATE_INVALID;
                    return ZX_ERR_ZLIB(z_ret);
                }
            }

            ZX_LOG("Done reading header.");
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
                ZX_LOG("ERROR: While reading deflate blocks (%d).", ret);
                index->stream_state = ZX_STATE_INVALID;
                return ret;
            }

            total_read = nbytes - zs->avail_out;

            /* Done if buffer is filled. */
            if (zs->avail_out == 0) {
              break;
            }
            /* Otherwise ensure stream state is file trailer. */
            if (index->stream_state != ZX_STATE_FILE_TRAILER) {
                ZX_LOG("ERROR: Short read before end of the file.");
                index->stream_state = ZX_STATE_INVALID;
                return ZX_ERR_CORRUPTED;
            }
        case ZX_STATE_FILE_TRAILER:
            /* TODO/BUG: Implement zlib separately. THIS IS TEMPORARY!!! */
            if (index->stream_type != ZX_STREAM_DEFLATE) {
                ret = read_gzip_trailer(index);
                if (ret != ZX_RET_OK) {
                    ZX_LOG("ERROR: While parsing gzip file trailer (%d).",
                           ret);
                    index->stream_state = ZX_STATE_INVALID;
                    return ret;
                }
            }
            index->stream_state = ZX_STATE_END_OF_FILE;

            /* Assign file sizes. */
            index->compressed_size = index->offset.comp;
            index->uncompressed_size = index->offset.uncomp;
            ZX_LOG("Compressed/uncompressed size: %jd/%jd.",
                   (intmax_t) index->compressed_size,
                   (intmax_t) index->uncompressed_size);
            break;
        case ZX_STATE_INVALID:
            /* TODO: Implement this. */
            ZX_LOG("ERROR: The stream is in invalid state, corrupted due to "
                   "some error.");
            return ZX_ERR_CORRUPTED;

        case ZX_STATE_END_OF_FILE:
            ZX_LOG("No reading is made since state is end-of-file.");
            return 0;

        default:
            ZX_LOG("ERROR: Unknown state (%d).", (int)index->stream_state);
            index->stream_state = ZX_STATE_INVALID;
            return ZX_ERR_CORRUPTED;

    } /* end of switch(index->stream_state) */

    ZX_LOG("Read %jd bytes.", (intmax_t)total_read);

    /* Return number of bytes read into the buffer. */
    return total_read;
}

int zidx_seek(zidx_index* index, off_t offset)
{
    return zidx_seek_ex(index, offset, NULL, NULL);
}

int zidx_seek_ex(zidx_index* index,
                 off_t offset,
                 zidx_block_callback block_callback,
                 void *callback_context)
{
    /* TODO: If this function fails to reset z_stream, it will leave z_stream
     * in an invalid state. Must be handled. */

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

    /* Sanity checks. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (offset < 0) {
        ZX_LOG("ERROR: offset (%jd) is negative.", (intmax_t)offset);
        return ZX_ERR_PARAMS;
    }

    checkpoint_idx = zidx_get_checkpoint_idx(index, offset);
    checkpoint = zidx_get_checkpoint(index, checkpoint_idx);

    if (checkpoint == NULL) {
        ZX_LOG("No checkpoint found.");

        /* Seek to the beginning of file, if no checkpoint has been found. */
        s_ret = sl_seek(index->comp_stream, 0, SL_SEEK_SET);
        if (s_ret < 0) {
            ZX_LOG("ERROR: Couldn't seek in stream (%d).", s_ret);
            return ZX_ERR_STREAM_SEEK;
        }

        /* Reset stream states and offsets. TODO: It may be unnecessary to
         * update comp_byte and comp_bits_count. */
        index->stream_state           = ZX_STATE_FILE_HEADERS;
        index->offset.comp            = 0;
        index->offset.comp_byte       = 0;
        index->offset.comp_bits_count = 0;
        index->offset.uncomp          = 0;

        /* Dispose if there's anything in input buffer. */
        index->z_stream->avail_in = 0;
    } else if (
            index->offset.uncomp < checkpoint->offset.uncomp
            || index->offset.uncomp > offset) {
        /* If offset is between checkpoint and current index offset, jump to
         * the checkpoint. */
        ZX_LOG("Jumping to checkpoint (idx: %d, comp: %ld, uncomp: %ld).",
                checkpoint_idx, checkpoint->offset.comp,
                checkpoint->offset.uncomp);

        /* Initialize as deflate. */
        z_ret = initialize_inflate(index, index->z_stream,
                                   -index->window_bits);
        if (z_ret != Z_OK) {
            ZX_LOG("ERROR: inflate initialization returned error (%d).",
                   z_ret);
            return ZX_ERR_ZLIB(z_ret);
        }

        /* Seek to the checkpoint offset in compressed stream. */
        s_ret = sl_seek(index->comp_stream,
                        checkpoint->offset.comp,
                        SL_SEEK_SET);
        if (s_ret != 0) {
            ZX_LOG("ERROR: Couldn't seek in stream (%d).", s_ret);
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
                ZX_LOG("ERROR: inflatePrime error (%d).", z_ret);
                return ZX_ERR_ZLIB(z_ret);
            }
        }

        /* Copy window from checkpoint. */
        z_ret = inflateSetDictionary(index->z_stream,
                                     checkpoint->window_data,
                                     checkpoint->window_length);
        if (z_ret != Z_OK) {
            ZX_LOG("ERROR: inflateSetDictionary error (%d).", z_ret);
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
    } else {
        ZX_LOG("No need to jump to checkpoint, since offset (%jd) is closer "
               "to the current offset (%jd) than that of checkpoint (%jd).",
               (intmax_t)offset, (intmax_t)index->offset.uncomp,
               (intmax_t)checkpoint->offset.uncomp);
    }

    /* Whether we jump to somewhere in file or not, we need to consume
     * remaining bytes until the offset by decompressing. */
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
        s_read_len = zidx_read_ex(index,
                                  index->seeking_data_buffer,
                                  num_bytes_next,
                                  block_callback,
                                  callback_context);
        /* Handle error. */
        if (s_read_len < 0) {
            ZX_LOG("ERROR: Couldn't decompress remaining data while "
                   "seeking (%d).", s_read_len);
            return s_read_len;
        }

        /* Handle end-of-file. */
        if (s_read_len == 0) {
            ZX_LOG("ERROR: Unexpected end-of-file while decompressing "
                   "remaining data for seeking (%d).", s_read_len);
            return ZX_ERR_STREAM_EOF;
        }

        /* Decrease number of bytes read. */
        num_bytes_remaining -= s_read_len;

        #ifdef ZX_DEBUG
        /* Normally this should be unnecessary, since this function should not
         * return more than buffer length, but anyway let's check it in
         * debugging mode. */
        if (num_bytes_remaining < 0) {
            ZX_LOG("ERROR: Screwed number of bytes remaining while reading "
                   "for seeking. Check the code. (num_bytes_remaining: %jd)",
                   (intmax_t) num_bytes_remaining);
            return ZX_ERR_CORRUPTED;
        }
        #endif
    }

    return ZX_RET_OK;
}

off_t zidx_tell(zidx_index* index)
{
    return index->offset.uncomp;
}

int zidx_rewind(zidx_index* index)
{
    return zidx_seek(index, 0);
}

int zidx_eof(zidx_index* index)
{
    return index->stream_state == ZX_STATE_END_OF_FILE;
}

int zidx_error(zidx_index* index)
{
    /* TODO: Implement invalid state. */
    return index->stream_state == ZX_STATE_INVALID;
}

int zidx_uncomp_size(zidx_index* index)
{
    return index->uncompressed_size;
}

typedef struct spacing_data_s
{
    off_t last_offset;
    off_t spacing_length;
    char is_uncompressed;
} spacing_data;

static int spacing_callback(void *context,
                            zidx_index *index,
                            zidx_checkpoint_offset *offset,
                            int is_last_block)
{
    /* Used for storing return value of this function. */
    int ret;

    /* Used for storing return value of zidx calls. */
    int zx_ret;

    /* New checkpoint to be added. Should be freed in case of failure. */
    zidx_checkpoint *ckp = NULL;

    /* Casted alias for context. */
    spacing_data* data = context;

    off_t current_offset;

    /* Determine which offsets to use. */
    if (data->is_uncompressed) {
        current_offset = offset->uncomp;
    } else {
        current_offset = offset->comp;
    }

    /* If spacing_length bytes passed since last saved checkpoint... */
    if (current_offset >= data->last_offset + data->spacing_length) {

        /* Create a new checkpoint. */
        ckp = zidx_create_checkpoint();
        if (ckp == NULL) {
            ZX_LOG("ERROR: Couldn't allocate memory for new checkpoint (%d).",
                   zx_ret);
            ret = zx_ret;
            goto cleanup;
        }

        /* Fill it in with current index and offset information. */
        zx_ret = zidx_fill_checkpoint(index, ckp, offset);
        if (zx_ret != ZX_RET_OK) {
            ZX_LOG("ERROR: Couldn't fill new checkpoint (%d).", zx_ret);
            ret = zx_ret;
            goto cleanup;
        }

        /* Add checkpoint to the index list. */
        zx_ret = zidx_add_checkpoint(index, ckp);
        if (zx_ret != ZX_RET_OK) {
            ZX_LOG("ERROR: Couldn't add new checkpoint (%d).", zx_ret);
            ret = zx_ret;
            goto cleanup;
        }

        /* Set last_offset. */
        data->last_offset = current_offset;
    }

    return ZX_RET_OK;

cleanup:
    if (ckp != NULL) {
        free(ckp);
    }
    return ret;
}


int zidx_build_index(zidx_index* index,
                     off_t spacing_length,
                     char is_uncompressed)
{
    /* Context for spacing_callback. */
    spacing_data data;

    /* Assign last uncompressed offset to 0, and pass spacing_length. */
    data.last_offset = 0;
    data.spacing_length = spacing_length;
    data.is_uncompressed = is_uncompressed;

    return zidx_build_index_ex(index, spacing_callback, &data);
}

int zidx_build_index_ex(zidx_index* index,
                        zidx_block_callback next_block_callback,
                        void *callback_context)
{
    /* Used for storing return value of zidx calls. */
    int zx_ret;

    /* Read as long as it's not end of stream. */
    do {
        zx_ret = zidx_read_ex(index,
                              index->seeking_data_buffer,
                              index->seeking_data_buffer_size,
                              next_block_callback,
                              callback_context);
        if (zx_ret < 0) {
            ZX_LOG("ERROR: While reading decompressed data (%d).", zx_ret);
            return zx_ret;
        }
    } while(zx_ret > 0);

    return ZX_RET_OK;
}

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
    /* TODO: Remove offset argument as it is already a member of index.
     * Alternatively, and preferably, remove index if you use dict_length for
     * window_size. */

    /* Return value for function. */
    int ret;

    /* Used for storing return value of zlib calls. */
    int z_ret;

    /* Length of dictionary, a.k.a. sliding window. */
    unsigned int dict_length;

    /* Flag to decide whether window_data should be released upon failure. */
    int window_data_allocated = 0;

    /* Sanity check. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (new_checkpoint == NULL) {
        ZX_LOG("ERROR: new_checkpoint is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (offset == NULL) {
        ZX_LOG("ERROR: offset is NULL.");
        return ZX_ERR_PARAMS;
    }

    /* Read dict_length. This will be used to allocate window space. */
    z_ret = inflateGetDictionary(index->z_stream, NULL, &dict_length);
    if (z_ret != Z_OK) {
        ZX_LOG("ERROR: inflateGetDictionary returned error (%d).", z_ret);
        ret = ZX_ERR_ZLIB(z_ret);
        goto cleanup;
    }

    /* Allocate space for window_data if there isn't one and dict_length is not
     * zero. If user provides window_data, it's his responsibility to make sure
     * its size is enough to hold window. Typically, 32768 is enough for all
     * windows. */
    if (new_checkpoint->window_data == NULL && dict_length > 0)
    {
        new_checkpoint->window_data = (uint8_t*)malloc(dict_length);
        if (new_checkpoint->window_data == NULL) {
            ZX_LOG("ERROR: Couldn't allocate memory for window data.");
            ret = ZX_ERR_MEMORY;
            goto cleanup;
        }
        window_data_allocated = 1;
    }

    z_ret = inflateGetDictionary(index->z_stream, new_checkpoint->window_data,
                                 &dict_length);
    if (z_ret != Z_OK) {
        ZX_LOG("ERROR: inflateGetDictionary returned error (%d).", z_ret);
        ret = ZX_ERR_ZLIB(z_ret);
        goto cleanup;
    }

    /* Copy current offset to checkpoint offset. */
    memcpy(&new_checkpoint->offset, offset, sizeof(zidx_checkpoint_offset));

    /* dict_length can't be more than 32768. */
    new_checkpoint->window_length = dict_length;

    /*Instantiate checkpoint checksum.*/
    /*Checksum will be filled when reading deflate blocks*/
    new_checkpoint->checksum=crc32(0L,Z_NULL,0);

    return ZX_RET_OK;

cleanup:
    if (window_data_allocated) {
        free(new_checkpoint->window_data);
        new_checkpoint->window_data = NULL;
    }
    return ret;
}

int zidx_add_checkpoint(zidx_index* index, zidx_checkpoint* checkpoint)
{
    /* Used for storing return value of zidx calls. */
    int zx_ret;

    /* Convenience variables. They keep uncompressed offsets of the last
     * checkpoint and current checkpoint. Current implementation requires added
     * checkpoints to be ordered in an array, therefore we check for the
     * uncompressed offset of last checkpoint before adding a new checkpoint. A
     * better approach would be using some balanced binary tree implementation,
     * which would allow inserting to any place in O(lgn). */
    /* TODO: Consider using balanced BST for index->list. However, let's not go
     * feature creep for now, shall we? */
    off_t last_uncomp;
    off_t chpt_uncomp;

    /* Sanity checks. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (checkpoint == NULL) {
        ZX_LOG("ERROR: checkpoint is NULL.");
        return ZX_ERR_PARAMS;
    }
    /* Check if list is corrupted. */
    if (index->list == NULL && index->list_capacity != 0) {
        ZX_LOG("ERROR: index list is NULL while capacity is not zero.");
        return ZX_ERR_CORRUPTED;
    }

    int first_chkpt_flag=0;

    /* If there are any checkpoints on the list, the new checkpoint should have
     * greater uncompressed offset than that of last checkpoint. */
    if (index->list_count > 0) {
        last_uncomp = index->list[index->list_count - 1].offset.uncomp;
        chpt_uncomp = checkpoint->offset.uncomp;
        if (chpt_uncomp <= last_uncomp) {
            ZX_LOG("ERROR: Can't add checkpoint, its uncompressed offset "
                   "(%jd) is less than that of last checkpoint (%jd).",
                   (intmax_t)chpt_uncomp, (intmax_t)last_uncomp);
            return ZX_ERR_INVALID_OP;
        }
    }
    else
    {
    	first_chkpt_flag=1;
    }

    /* Open some space in list if needed. */
    if (index->list_capacity == index->list_count) {
        /* Double the list size. Plus one is added in case list count is
         * currently zero. */
        zx_ret = zidx_extend_index_size(index, index->list_count + 1);
        if (zx_ret != ZX_RET_OK) {
            ZX_LOG("ERROR: Couldn't extend index size (%d).", zx_ret);
            return zx_ret;
        }
    }

    //If at the head of the list, just calculate checksum. Otherwise, combine your checksum with the checksum of the checkpoint before you
    if(first_chkpt_flag)
    {
    	checkpoint->checksum=crc32(checkpoint->checksum,checkpoint->window_data,checkpoint->window_length);
    }
    else
    {
    	uint32_t indiv_checksum=crc32(0L,Z_NULL,0);
    	indiv_checksum=(indiv_checksum,checkpoint->window_data,checkpoint->window_length);
    	checkpoint->checksum=crc32_combine(index->list[index->list_count-1].checksum,indiv_checksum,checkpoint->window_length);


    }
    /* Add new checkpoint.*/
    memcpy(&index->list[index->list_count], checkpoint, sizeof(*checkpoint));
    index->list_count++;

    /* TODO: Note in the documentation, that checkpoint can be freed after this
     * call. Not the window_data member of it though. */

    return ZX_RET_OK;
}

/**
 * Returns the number of elements in an index's checkpoint list
 * \param index the index holding the checkpoint list
 * \return the number of elements in the checkpoint list
 */
int zidx_get_checkpoint_list_len(zidx_index* index)
{
	if(index==NULL)
	{
		ZX_LOG("ERROR: index is null.");
		return ZX_ERR_PARAMS;
	}
	return index->list_count;
}


uint32_t zidx_get_checkpoint_checksum(zidx_index* index, int idx)
{
	if(index==NULL)
	{
		ZX_LOG("ERROR: index is null.");
		return ZX_ERR_PARAMS;
	}
	if(idx<0)
	{
		ZX_LOG("ERROR: index (%d) is negative.", idx);
		return ZX_ERR_PARAMS;
	}
	else if(idx>=index->list_count)
	{
		ZX_LOG("ERROR: index (%d) is larger than checkpoint list size (%d).", idx,index->list_count);
		return ZX_ERR_PARAMS;
	}
	return index->list[idx].checksum;
}

uint32_t zidx_get_last_checksum(zidx_index* index)
{
	if(index==NULL)
	{
		ZX_LOG("ERROR: index is null.");
		return ZX_ERR_PARAMS;
	}
	return zidx_get_checkpoint_checksum(index,index->list_count-1);
}

int zidx_get_checkpoint_idx(zidx_index* index, off_t offset)
{
    /* TODO: Return EOF error if EOF is known and offset is beyond it. */
    /* TODO: Add more logging for returns. I don't feel like doing it today. */

    /* A local definition for a cumbersome access. Undefed at the end of this
     * function. */
    #define ZX_OFFSET_(idx) (index->list[idx].offset.uncomp)

    /* Variables used for binary search. */
    int left, right;

    /* Return value holding the index or error number. */
    int idx;

    /* Sanity checks. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (offset < 0) {
        ZX_LOG("ERROR: offset (%jd) is negative.", (intmax_t)offset);
        return ZX_ERR_PARAMS;
    }

    /* Return not found if list is empty. */
    if(index->list_count == 0) {
        ZX_LOG("ERROR: List is empty, so checkpoint for given offset (%jd) is "
               "not found.", (intmax_t)offset);
        return ZX_ERR_NOT_FOUND;
    }

    left  = 0;
    right = index->list_count - 1;

    /* Check the last element first. We check it in here so that we don't
     * account for it in every iteartion of the loop below. */
    if(ZX_OFFSET_(right) < offset) {
        ZX_LOG("Offset (%jd) found at last checkpoint (%d) start at "
               "uncompressed offset (%jd).", (intmax_t)offset, right,
               ZX_OFFSET_(right));
        return right;
    }

    /* Shortcut: Since list is ordered, if offset is less than first offset,
     * than it is not covered. */
    if (offset < ZX_OFFSET_(left)) {
        ZX_LOG("ERROR: Not found, offset (%jd) is less than first offset "
               "(%jd).", (intmax_t)offset, (intmax_t)ZX_OFFSET_(left));
        return ZX_ERR_NOT_FOUND;
    }

    /* Binary search for a lowerbound index. */
    while (left <= right) {
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
            ZX_LOG("Offset (%jd) found at checkpoint (%d) start at "
                   "uncompressed offset (%jd).", (intmax_t)offset, idx,
                   ZX_OFFSET_(idx));
            return idx;
        }
    }

    /* left >= right, meaning that point range is not found. THIS STATE SHOULD
     * BE UNREACHABLE. */
    ZX_LOG("ERROR: If you see this, there's something terribly wrong in this "
           "function. The binary search failed, but it shouldn't have done "
           "so, since we compared offset to the that of first checkpoint. Go "
           "check the code. (left: %d, right: %d)", left, right);
    return ZX_ERR_NOT_FOUND;

    #undef ZX_OFFSET_
}


zidx_checkpoint* zidx_get_checkpoint(zidx_index* index, int idx)
{
    if (idx >= 0 && idx < index->list_count) {
        return &index->list[idx];
    }
    return NULL;
}

int zidx_checkpoint_count(zidx_index* index) {
    return index->list_count;
}

off_t zidx_get_checkpoint_offset(const zidx_checkpoint* ckp) {
    return ckp->offset.uncomp;
}

size_t zidx_get_checkpoint_window(const zidx_checkpoint* ckp,
                                  const void** result)
{
    if (result == NULL) {
        ZX_LOG("ERROR: result pointer is null.");
        return 0;
    }
    *result = ckp->window_data;
    return ckp->window_length;
}

int zidx_extend_index_size(zidx_index* index, int nmembers)
{
    /* New list to create. List is not created in-place to protect existing
     * list (index->list). */
    zidx_checkpoint *new_list;

    /* Sanity checks. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (nmembers <= 0) {
        ZX_LOG("ERROR: Number of items to extend (%d) is not positive.",
               nmembers);
        return ZX_ERR_PARAMS;
    }

    /* Allocate memory for nmembers more. index->list can be NULL if the
     * capacity is 0. */
    new_list = (zidx_checkpoint*)realloc(index->list, sizeof(zidx_checkpoint)
                                          * (index->list_capacity + nmembers));
    if(!new_list) {
        ZX_LOG("ERROR: Couldn't allocate memory for the extended list.");
        return ZX_ERR_MEMORY;
    }

    /* Update existing list. */
    index->list           = new_list;
    index->list_capacity += nmembers;

    return ZX_RET_OK;
}

int zidx_shrink_index_size(zidx_index* index, int nmembers)
{

    /* New list to create. List is not created in-place to protect existing
     * list (index->list). */
    zidx_checkpoint *new_list;

    /* Sanity checks. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (nmembers <= 0) {
        ZX_LOG("ERROR: Number of items to shrink (%d) is not positive.",
               nmembers);
        return ZX_ERR_PARAMS;
    }
    if (index->list_capacity < index->list_count + nmembers) {
        ZX_LOG("ERROR: Shrinking requires deallocating existing elements.");
        return ZX_ERR_PARAMS;
    }
    if (index->list_capacity < nmembers) {
        ZX_LOG("ERROR: Number of members to shrink (%d) is greater than "
               "current capacity (%d).", nmembers, index->list_capacity);
        return ZX_ERR_PARAMS;
    }

    /* Allocate memory for nmembers less. If nmembers is equal to list
     * capacity, this call is equivalent to freeing list. */
    new_list = (zidx_checkpoint*)realloc(index->list, sizeof(zidx_checkpoint)
                                          * (index->list_capacity - nmembers));
    if(!new_list) {
        ZX_LOG("ERROR: Couldn't allocate memory for the extended list.");
        return ZX_ERR_MEMORY;
    }

    /* Update existing list. */
    index->list           = new_list;
    index->list_capacity -= nmembers;

    return ZX_RET_OK;
}

int zidx_fit_index_size(zidx_index* index)
{
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    return zidx_shrink_index_size(index,
                                  index->list_capacity - index->list_count);
}

static int commit_temp_index_(zidx_index *index, zidx_index *temp_index)
{
    int needed_size;
    int zx_ret;
    zidx_checkpoint *it;
    const zidx_checkpoint *end;

    /* Extend index if needed. */
    needed_size = temp_index->list_count - index->list_capacity;
    if (needed_size > 0) {
        zx_ret = zidx_extend_index_size(index, needed_size);
        if (zx_ret != ZX_RET_OK) {
            ZX_LOG("ERROR: Couldn't extend index size %d more elements.",
                   needed_size);
            return zx_ret;
        }
    }

    /* Free existing index list members. */
    end = index->list + index->list_count;
    for (it = index->list; it < end; it++) {
        free(it->window_data);
    }
    free(index->list);

    /* Copy current index. */
    index->list       = temp_index->list;
    index->list_count = temp_index->list_count;
    temp_index->list        = NULL;
    temp_index->list_count  = 0;

    return ZX_RET_OK;
}

int zidx_import_ex(zidx_index *index,
                   streamlike_t *stream,
                   zidx_import_filter_callback filter,
                   void *filter_context)
{
    /* Local definition to tidy up cumbersome error check procedures. */
    #define ZX_READ_TEMPLATE_(buf, buflen, name) \
        do { \
            s_ret = sl_read(stream, (uint8_t*)buf, buflen); \
            if (s_ret < buflen) { \
                s_err = sl_error(stream); \
                if (s_err) { \
                    ZX_LOG("ERROR: Couldn't read " name " (%d).", s_err); \
                    ret = s_err; \
                    goto end; \
                } else if(sl_eof(stream)) { \
                    ZX_LOG("ERROR: Unexpected end-of-file while reading %s.", \
                           name); \
                    ret = ZX_ERR_STREAM_EOF; \
                    goto end; \
                } else { \
                    ZX_LOG("ERROR: Asynchronous read is not implemented."); \
                    ret = ZX_ERR_NOT_IMPLEMENTED; \
                    goto end; \
                } \
            } \
            if (sizeof(*buf) == buflen) { \
                ZX_LOG("Imported " name " (%jd) ", (intmax_t)(*buf)); \
            } else if(buflen >= 3) { \
                ZX_LOG("Imported " name " (hex %02X %02X %02X%s)", \
                       ((uint8_t*)buf)[0], ((uint8_t*)buf)[1], \
                       ((uint8_t*)buf)[2], (buflen > 3 ? "..." : "")); \
            } else if(buflen == 2) { \
                ZX_LOG("Imported " name " (hex %02X %02X)", \
                       ((uint8_t*)buf)[0], ((uint8_t*)buf)[1]); \
            } else { \
                ZX_LOG("Imported " name " (hex %02X)", ((uint8_t*)buf)[0]); \
            } \
        } while(0)

    /* Temporary index used for shadow index. This is to protect original index
     * from changes in case of a failure. Related parameters of temp_index will
     * be copied to index if everything goes alright. */
    zidx_index* temp_index = NULL;

    /* Used for storing return values of stream calls. */
    int s_ret;
    int s_err;

    /* Return value for this function. Used for clean error handling to avoid
     * memory leaks. */
    int ret;

    /* Used for zidx library calls. */
    int zx_ret;

    /* Fixed length types. Used for reading fixed-length data to int. */
    int64_t i64;
    int32_t i32;
    off_t off;

    /* General purpose byte buffer. */
    uint8_t buf[8];

    /* Used for reading type of file. */
    int16_t type_of_file;

    /* Iterator and end point for used for iterating over list member of index
     * and temp_index. */
    zidx_checkpoint *it;
    const zidx_checkpoint *end;

    /* Sanity checks. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    /* Index list can be NULL if list_capacity is zero. */
    if (index->list == NULL && index->list_capacity == 0) {
        ZX_LOG("ERROR: index list is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (stream == NULL) {
        ZX_LOG("ERROR: input stream is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (filter != NULL || filter_context != NULL) {
        ZX_LOG("ERROR: import filtering not supported.");
        return ZX_ERR_NOT_IMPLEMENTED;
    }

    temp_index = calloc(1, sizeof(zidx_index));
    if (temp_index == NULL) {
        ZX_LOG("ERROR: Couldn't allocate memory for temporary index.");
        return ZX_ERR_MEMORY;
    }

    /* Read magic string and check. */
    ZX_READ_TEMPLATE_(buf, sizeof(zx_magic_prefix), "magic prefix");
    if (memcmp(zx_magic_prefix, buf, sizeof(zx_magic_prefix))) {
        ZX_LOG("ERROR: Incorrect magic prefix.");
        ret = ZX_ERR_CORRUPTED;
        goto end;
    }

    /* Read version string and check. */
    ZX_READ_TEMPLATE_(buf, sizeof(zx_version_prefix), "version prefix");
    if (memcmp(zx_version_prefix, buf, sizeof(zx_version_prefix))) {
        ZX_LOG("ERROR: Incorrect version prefix.");
        ret = ZX_ERR_CORRUPTED;
        goto end;
    }

    /* Read type of checksum. TODO: Not implemented yet. */
    ZX_READ_TEMPLATE_(buf, 2, "the type of checksum");

    ZX_READ_TEMPLATE_(&i32,4,"running checksum");

    /* Read checksum of the rest of header. TODO: Not implemented yet. */
    ZX_READ_TEMPLATE_(buf, 4, "checksum of the header");

    /* Read the type of indexed file. */
    ZX_READ_TEMPLATE_(&type_of_file, sizeof(type_of_file), "the type of file");

    /* TODO: File type check is not done. Do it. */

    /* Read the length of compressed file. */
    ZX_READ_TEMPLATE_(&i64, 8, "the compressed length");
    off = i64;
    if (off != i64) {
        ZX_LOG("ERROR: Compressed length doesn't fit to offset type.");
        ret = ZX_ERR_INVALID_OP;
        goto end;
    }
    index->compressed_size = off;

    /* Read the length of uncompressed file. TODO: Not implemented yet. */
    ZX_READ_TEMPLATE_(&i64, 8, "the uncompressed length");
    off = i64;
    if (off != i64) {
        ZX_LOG("ERROR: Uncompressed length doesn't fit to offset type.");
        ret = ZX_ERR_INVALID_OP;
        goto end;
    }
    index->uncompressed_size = off;

    /* Checksum of indexed file. TODO: Not implemented yet. */
    ZX_READ_TEMPLATE_(buf, 4, "checksum of the index");

    /* Number of indexed checkpoints. */
    ZX_READ_TEMPLATE_(&i32, sizeof(i32), "number of checkpoints");
    if (i32 < 0) {
        ZX_LOG("ERROR: Number of checkpoints should be nonnegative (%d).",
               i32);
        ret = ZX_ERR_CORRUPTED;
        goto end;
    }
    temp_index->list_count = i32;
    temp_index->list_capacity = i32;

    /* Checksum of whole checkpoint metadata. TODO: Not implemented yet. */
    ZX_READ_TEMPLATE_(buf, 4, "checksum of metadata");

    /* Flags. TODO: Not implemented yet. Also it's non-conformant: Current
     * implementation assumes as if ZX_UNKNOWN_CHECKSUM and
     * ZX_UNKNOWN_WINDOW_CHECKSUM flags are set. */
    ZX_READ_TEMPLATE_(buf, 4, "flags");

    /* TODO: Implement optional extra data. */

    /*
     * Checkpoint section.
     */

    ZX_LOG("Completed reading header of imported file at offset %jd.",
           (intmax_t)sl_tell(stream));

    /* Allocate space for list. */
    temp_index->list = calloc(temp_index->list_count, sizeof(zidx_checkpoint));
    if (temp_index->list == NULL) {
        ZX_LOG("ERROR: Couldn't allocate space for list.");
        ret = ZX_ERR_MEMORY;
        goto end;
    }
    end = temp_index->list + temp_index->list_count;

    /* Iterate over checkpoints for writing checkpoint metadata. */
    for(it = temp_index->list; it < end; it++)
    {
        /* Read uncompressed offset. */
        ZX_READ_TEMPLATE_(&i64, sizeof(i64), "uncompressed offset");
        if (sizeof(i64) > sizeof(it->offset.uncomp)) {
            if (i64 > 0x7FFFFFFF) {
                ret = ZX_ERR_OVERFLOW;
                goto end;
            }
        }
        it->offset.uncomp = i64;

        /* Read compressed offset. */
        ZX_READ_TEMPLATE_(&i64, sizeof(i64), "compressed offset");
        if (sizeof(i64) > sizeof(it->offset.comp)) {
            if (i64 > 0x7FFFFFFF) {
                ret = ZX_ERR_OVERFLOW;
                goto end;
            }
        }
        it->offset.comp = i64;

        /* Read block boundary bits offset and boundary byte. */
        ZX_READ_TEMPLATE_(&it->offset.comp_bits_count, 1,
                          "boundary bit offset");
        ZX_READ_TEMPLATE_(&it->offset.comp_byte, 1, "boundary byte");
        if (it->offset.comp_bits_count == 0 && it->offset.comp_byte != 0) {
            ZX_LOG("ERROR: Boundary byte is not zero while bits count is.");
            ret = ZX_ERR_OVERFLOW;
            goto end;
        }

        /* Read offset of window data. TODO: Verify this data. */
        ZX_READ_TEMPLATE_(buf, 8, "window offset");

        /* Write length of window data. TODO: Non-conformant. Has a length of 2
         * bytes instead of 4 bytes. Need to update file specification. */
        ZX_READ_TEMPLATE_(&it->window_length, sizeof(it->window_length),
                          "window length");

        /*Read checksum data.*/
        ZX_READ_TEMPLATE_(&it->checksum,sizeof(i32),"checkpoint checksum");
    }

    /* TODO: Verify window data start offset. */


	uint32_t running_checksum_check=crc32(0L,Z_NULL,0);
    /* Iterate over checkpoints for writing checkpoint window data. */
    for(it = temp_index->list; it < end; it++)
    {
        if (it->window_length > 0) {
            /* Allocate space. */
            it->window_data = malloc(it->window_length);
            if (it->window_data == NULL) {
                ZX_LOG("ERROR: Couldn't allocate space for window data.");
                ret = ZX_ERR_MEMORY;
                goto end;
            }
            /* Write window data. */
            ZX_READ_TEMPLATE_(it->window_data, it->window_length,"window data");


        } else {
            /* This might be unnecessary, given we used calloc above while
             * populating list, but let's keep it. */
            it->window_data = NULL;
            ZX_LOG("No window data.");
        }
    }

    /* Now that we are good, copy temporary index to main index. */
    zx_ret = commit_temp_index_(index, temp_index);
    if (zx_ret != ZX_RET_OK) {
        ZX_LOG("ERROR: Couldn't commit temporary index (%d).", zx_ret);
        return zx_ret;
    }


    ret = ZX_RET_OK;
    // fallthrough

end:
    if (temp_index) {
        if (temp_index->list) {
            for (it = temp_index->list; it < end; it++) {
                free(it->window_data);
            }
        }
        free(temp_index->list);
    }
    free(temp_index);

    return ret;
    #undef ZX_READ_TEMPLATE_

}



int zidx_export_ex(zidx_index *index,
                   streamlike_t *stream,
                   zidx_export_filter_callback filter,
                   void *filter_context)
{
    /* Local definition to tidy up cumbersome error check procedures. */
    #define ZX_WRITE_TEMPLATE_(buf, buflen, name) \
        do { \
            s_ret = sl_write(stream, (uint8_t*)buf, buflen); \
            if (s_ret < buflen) { \
                s_err = sl_error(stream); \
                ZX_LOG("ERROR: Couldn't write " name " (%d).", s_err); \
                return s_err; \
            } \
            if (sizeof(*buf) == buflen) { \
                ZX_LOG("Exported " name " (%jd) ", (intmax_t)(*buf)); \
            } else if(buflen >= 3) { \
                ZX_LOG("Exported " name " (hex %02X %02X %02X%s)", \
                       ((uint8_t*)buf)[0], ((uint8_t*)buf)[1], \
                       ((uint8_t*)buf)[2], (buflen > 3 ? "..." : "")); \
            } else if(buflen == 2) { \
                ZX_LOG("Exported " name " (hex %02X %02X)", \
                       ((uint8_t*)buf)[0], ((uint8_t*)buf)[1]); \
            } else { \
                ZX_LOG("Exported " name " (hex %02X)", ((uint8_t*)buf)[0]); \
            } \
        } while(0)

    /* Used for storing return values of stream calls. */
    int s_ret;
    int s_err;

    /* Used for writing 0 as default for some values. */
    const uint64_t zero = 0;

    /* Type of indexed file. TODO: Currently, it's gzip. Implement others. */
    int16_t type_of_file = 0x1;

    /* Used for expanding types to fixed bit values. */
    int64_t i64;
    int32_t i32;

    /* Window data offset. Keeps track of where to write next window data. */
    int64_t window_off;

    /* Sanity checks. */
    if (index == NULL) {
        ZX_LOG("ERROR: index is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (index->list == NULL) {
        /* TODO: This sanity check will cause error if user tries to export an
         * index with 0 list_capacity since list will be NULL in that case. */
        ZX_LOG("ERROR: index list is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (stream == NULL) {
        ZX_LOG("ERROR: output stream is NULL.");
        return ZX_ERR_PARAMS;
    }
    if (filter != NULL || filter_context != NULL) {
        ZX_LOG("ERROR: export filtering not supported.");
        return ZX_ERR_NOT_IMPLEMENTED;
    }

    /*
     * Header section.
     */

    /* Write magic string. */
    ZX_WRITE_TEMPLATE_(zx_magic_prefix, sizeof(zx_magic_prefix),
                       "magic prefix");

    /* Write version info. */
    ZX_WRITE_TEMPLATE_(zx_version_prefix,
                       sizeof(zx_version_prefix),
                       "version prefix");

    /* Write type of checksum. TODO: Not implemented yet. */
    ZX_WRITE_TEMPLATE_(&zero, 2, "the type of checksum");

    ZX_WRITE_TEMPLATE_(&i32,4,"running checksum");

    /* Write checksum of the rest of header. TODO: Not implemented yet. */
    ZX_WRITE_TEMPLATE_(&zero, 4, "checksum of the header");

    /* Write the type of indexed file. */
    ZX_WRITE_TEMPLATE_(&type_of_file, sizeof(type_of_file),
                       "the type of file");

    /* Write the length of compressed file. */
    i64 = index->compressed_size;
    ZX_WRITE_TEMPLATE_(&i64, 8, "the compressed length");

    /* Write the length of uncompressed file. */
    i64 = index->uncompressed_size;
    ZX_WRITE_TEMPLATE_(&i64, 8, "the uncompressed length");


    /* Checksum of indexed file. TODO: Not implemented yet. */
    ZX_WRITE_TEMPLATE_(&i32, sizeof(i32), "checksum of the index");

    /* Number of indexed checkpoints. */
    i32 = index->list_count;
    ZX_WRITE_TEMPLATE_(&i32, sizeof(i32), "number of checkpoints");

    /* Checksum of whole checkpoint metadata. TODO: Not implemented yet. */
    ZX_WRITE_TEMPLATE_(&zero, 4, "checksum of metadata");

    /* Flags. TODO: Not implemented yet. Also it's non-conformant: Current
     * implementation assumes as if ZX_UNKNOWN_CHECKSUM and
     * ZX_UNKNOWN_WINDOW_CHECKSUM flags are set. */
    ZX_WRITE_TEMPLATE_(&zero, 4, "flags");

    /* TODO: Implement optional extra data. */

    /*
     * Checkpoint section.
     */

    ZX_LOG("Completed writing header of imported file at offset %jd.",
           (intmax_t)sl_tell(stream));

    /* Compute beginning offset of window data. TODO: Extra space is assumed to
     * be zero. */
    window_off = sl_tell(stream);
    if (window_off < 0) {
        ZX_LOG("ERROR: Couldn't tell stream offset (%ld).", window_off);
        return ZX_ERR_STREAM_SEEK;
    }
    /* Skip checkpoint headers section. */
    window_off += 24 * index->list_count;

    /* List iterator and end point. */
    zidx_checkpoint *it;
    zidx_checkpoint *end = index->list + index->list_count;

    /* Iterate over checkpoints for writing checkpoint metadata. */
    for(it = index->list; it < end; it++)
    {
        /* Write uncompressed offset. */
        i64 = it->offset.uncomp;
        ZX_WRITE_TEMPLATE_(&i64, sizeof(i64), "uncompressed offset");

        /* Write compressed offset. */
        i64 = it->offset.comp;
        ZX_WRITE_TEMPLATE_(&i64, sizeof(i64), "compressed offset");

        /* Write block boundary bits offset and boundary byte. */
        ZX_WRITE_TEMPLATE_(&it->offset.comp_bits_count, 1,
                           "boundary bit offset");
        if (it->offset.comp_bits_count == 0) {
            ZX_WRITE_TEMPLATE_(&zero, 1, "boundary byte");
        } else {
            ZX_WRITE_TEMPLATE_(&it->offset.comp_byte, 1, "boundary byte");
        }

        /* Write offset of window data. */
        ZX_WRITE_TEMPLATE_(&window_off, sizeof(window_off), "window offset");

        /* Write length of window data. TODO: Non-conformant. Has a length of 2
         * bytes instead of 4 bytes. Need to update file specification. */
        ZX_WRITE_TEMPLATE_(&it->window_length, sizeof(it->window_length),"window length");

        /*Write checksum of checkpoint*/
        ZX_WRITE_TEMPLATE_(&it->checksum,sizeof(i32),"checksum of the checkpoint");

        /* Update window offset for next checkpoint. */
        window_off += it->window_length;
    }

    /* Iterate over checkpoints for writing checkpoint window data. */
    for(it = index->list; it < end; it++)
    {
        if (it->window_length > 0) {
            /* Write window data. */
            ZX_WRITE_TEMPLATE_(it->window_data, it->window_length,"window data");

        }
    }

    return ZX_RET_OK;

    #undef ZX_WRITE_TEMPLATE_
}

int zidx_import(zidx_index *index, streamlike_t *stream)
{
    return zidx_import_ex(index, stream, NULL, NULL);
}

int zidx_export(zidx_index *index, streamlike_t *stream)

{
    return zidx_export_ex(index, stream, NULL, NULL);
}

#ifdef __cplusplus
} // extern "C"
#endif
