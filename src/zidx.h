/**
 * \file
 * libzidx API header.
 */
#ifndef ZIDX_H
#define ZIDX_H

#include <stdio.h>     // FILE*
#include <stdint.h>
#include <sys/types.h> // off_t

#include <streamlike.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \name DefaultValues
 *
 * Default initialization values used by zidx_index_init() call.
 *
 * @{
 */

/** Default value for initial capacity of checkpoint list. */
#define ZX_DEFAULT_INITIAL_LIST_CAPACITY (8)

/** Default value for zlib window size. */
#define ZX_DEFAULT_WINDOW_SIZE (32768)

/** Default value for size of the buffer used for decompression. */
#define ZX_DEFAULT_COMPRESSED_DATA_BUFFER_SIZE (32768)

/**
 * Default value for the size of buffer for discarding unused data while
 * seeking to an offset inside a compressed block.
 */
#define ZX_DEFAULT_SEEKING_DATA_BUFFER_SIZE (32768)

/** }@ */

/**
 * \name ReturnValues
 *
 * Return values used by zidx functions.
 *
 * All error codes must be negative. Users can get return value of callback
 * functions, however it's users responsibility to avoid conflicting return
 * values in callbacks. Users are encouraged to leave return values in the
 * range of -1 to -256 library use.
 *
 * Errors caused by zlib library will be returned by passing through
 * ZX_ERR_ZLIB() macro, so user can use this macro to check for errors.
 *
 * @{
 */

#define ZX_RET_OK           (0) /**< Sucessful call. */
#define ZX_ERR_PARAMS      (-1) /**< Error in function parameter. */
#define ZX_ERR_MEMORY      (-2) /**< Error in memory allocation. */
#define ZX_ERR_CORRUPTED   (-3) /**< Corrupted file or data structure. */
#define ZX_ERR_STREAM_READ (-4) /**< Error in reading stream. */
#define ZX_ERR_STREAM_EOF  (-5) /**< Unexpected EOF error in stream. */
#define ZX_ERR_STREAM_SEEK (-6) /**< Error in seeking stream. */
#define ZX_ERR_INVALID_OP  (-7) /**< Invalid operation. */
#define ZX_ERR_NOT_FOUND   (-8) /**< Requested item is not found. */
#define ZX_ERR_OVERFLOW    (-9) /**< Data does not fit to the given data
                                  structure. */
#define ZX_ERR_NOT_IMPLEMENTED (-10)       /**< Feature is not implemented. */
#define ZX_ERR_ZLIB(err)       (-64 + err) /**< Error caused by zlib. */
#define ZX_ERR_STREAM_WRITE (-11)
/** @} */

/**
 * \name OpaqueDataStructures
 * Opaque data structures.
 *
 * @{
 */

#ifndef ZLIB_H
/** Opaque declaration of z_stream, if not declared by zlib header already. */
typedef struct z_stream_s z_stream;
#endif

/**
 * Keeps state of opened zidx stream state as well as other data structures
 * needed.
 */
typedef struct zidx_index_s zidx_index;

/**
 * Keeps information about a single checkpoint.
 */
typedef struct zidx_checkpoint_s zidx_checkpoint;

/**
 * Keeps information about compressed and uncompressed offsets.
 */
typedef struct zidx_checkpoint_offset_s zidx_checkpoint_offset;

/** @} */

/**
 * \name ConfigurationValues
 *
 * Types defined for denoting various configuration.
 * @{
 */

/**
 * Type of the input stream file. There isn't "just zlib file type" option due
 * to design of zlib library.
 */
typedef enum zidx_stream_type
{
    ZX_STREAM_DEFLATE      = 1, /**< DEFLATE format. */
    ZX_STREAM_GZIP         = 2, /**< GZIP format. */
    ZX_STREAM_GZIP_OR_ZLIB = 3  /**< GZIP or ZLIB format determined by file
                                  header. */
} zidx_stream_type;

/**
 * Type of the checksum algorithm.
 *
 * Since the library treats all data as deflate blocks, underlying checksum
 * control mechanism of zlib is disabled. Instead, this library can be
 * configured for a given checksum mechanism.
 */
typedef enum zidx_checksum_option
{
    ZX_CHECKSUM_DISABLED = 0,      /**< Do not use any checksum algorithm. */
    ZX_CHECKSUM_DEFAULT  = 1,      /**< Use default checksum algorithm for the
                                     file type. By default DEFLATE doesn't use
                                     any checksum mechanisms, GZIP uses CRC-32,
                                     and ZLIB uses Adler-32. */
    ZX_CHECKSUM_FORCE_CRC32   = 2, /**< Force using CRC-32. */
    ZX_CHECKSUM_FORCE_ADLER32 = 3  /**< Force using Adler-32. */
} zidx_checksum_option;

/** @} */

typedef
int (*zidx_block_callback)(void *context,
                           zidx_index *index,
                           zidx_checkpoint_offset *offset,
                           int is_last_block);

zidx_index* zidx_index_create();
int zidx_index_init(zidx_index* index,
                    streamlike_t* comp_stream);
int zidx_index_init_ex(zidx_index* index,
                       streamlike_t* comp_stream,
                       zidx_stream_type stream_type,
                       zidx_checksum_option checksum_option,
                       z_stream* z_stream_ptr,
                       int initial_capacity,
                       int window_size,
                       int comp_data_buffer_size,
                       int seeking_data_buffer_size);
int zidx_index_destroy(zidx_index* index);
int zidx_read(zidx_index* index, void *buffer, int nbytes);
int zidx_read_ex(zidx_index* index,
                 void *buffer,
                 int nbytes,
                 zidx_block_callback block_callback,
                 void *callback_context);
int zidx_seek(zidx_index* index, off_t offset);
int zidx_seek_ex(zidx_index* index,
                 off_t offset,
                 zidx_block_callback block_callback,
                 void *callback_context);
off_t zidx_tell(zidx_index* index);
int zidx_rewind(zidx_index* index);
int zidx_eof(zidx_index* index);
int zidx_error(zidx_index* index);
int zidx_uncomp_size(zidx_index* index);

int zidx_build_index(zidx_index* index,
                     off_t spacing_length,
                     char is_uncompressed);
int zidx_build_index_ex(zidx_index* index,
                        zidx_block_callback block_callback,
                        void *callback_context);

zidx_checkpoint* zidx_create_checkpoint();

int zidx_fill_checkpoint(zidx_index* index,
                         zidx_checkpoint* new_checkpoint,
                         zidx_checkpoint_offset* offset);
int zidx_add_checkpoint(zidx_index* index, zidx_checkpoint* checkpoint);
int zidx_get_checkpoint_idx(zidx_index* index, off_t offset);
zidx_checkpoint* zidx_get_checkpoint(zidx_index* index, int idx);
int zidx_checkpoint_count(zidx_index* index);
/* TODO: Consider dropping consts before release. */

int zidx_get_checkpoint_list_len(zidx_index* index);
uint32_t zidx_get_checkpoint_checksum(zidx_index* index,int idx);
uint32_t zidx_get_checksum(zidx_index* index);

off_t zidx_get_checkpoint_offset(const zidx_checkpoint* ckp);
off_t zidx_get_checkpoint_comp_offset(const zidx_checkpoint* ckp);
size_t zidx_get_checkpoint_window(const zidx_checkpoint* ckp,
                                  const void** result);
uint8_t zidx_get_checkpoint_byte(const zidx_checkpoint* chkp);
uint8_t zidx_get_checkpoint_bit_count(const zidx_checkpoint* chkp);

int zidx_extend_index_size(zidx_index* index, int nmembers);
int zidx_shrink_index_size(zidx_index* index, int nmembers);
int zidx_fit_index_size(zidx_index* index);

/* index import/export functions */

typedef
int (*zidx_import_filter_callback)(void *filter_context,
                                   zidx_index *index,
                                   zidx_checkpoint_offset *offset);

typedef
int (*zidx_export_filter_callback)(void *filter_context,
                                   zidx_index *index,
                                   zidx_checkpoint *offset);

int zidx_import_ex(zidx_index *index,
                   streamlike_t *stream,
                   zidx_import_filter_callback filter,
                   void *filter_context);

int zidx_export_ex(zidx_index *index,
                   streamlike_t *stream,
                   zidx_export_filter_callback filter,
                   void *filter_context);

int zidx_import(zidx_index *index, streamlike_t *stream);
int zidx_export(zidx_index *index, streamlike_t* output_index_file);

/* index modification functions */
int zidx_clear_hanging_byte(zidx_index *index, int checkpoint_idx);
off_t zidx_get_block_length(zidx_index *index,int checkpoint_idx,int comp_flag);
int zidx_update_checksums(zidx_index *index,uint32_t new_checksum,int checkpoint_idx);
int zidx_single_byte_modify(zidx_index *index, off_t offset, char new_char);
int zidx_small_modify(zidx_index *index, off_t offset, char *buffer, int length);
int zidx_modify(zidx_index *index, off_t offset, char *buffer, int length);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ZIDX_H
