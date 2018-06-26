/**
 * \file
 * libzidx API header.
 */
#ifndef _ZIDX_H_
#define _ZIDX_H_

#include <stdio.h>     // FILE*
#include <stdint.h>
#include <sys/types.h> // off_t, size_t, ssize_t

#define ZIDX_DEFAULT_INITIAL_LIST_CAPACITY       (8)
#define ZIDX_DEFAULT_WINDOW_SIZE                 (32768)
#define ZIDX_DEFAULT_COMPRESSED_DATA_BUFFER_SIZE (16384)

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ZLIB_H
/* Typedef declaration of z_stream from zlib.h required for z_stream* */
typedef struct z_stream_s z_stream;
#endif

/*******************************************************************************
 * \defgroup streamapi Stream API
 *
 * Provide an abstract way to access to underlying resource stream (file, url
 * etc.)
 *
 * @{
 ******************************************************************************/

#define ZIDX_SEEK_SET (0)
#define ZIDX_SEEK_CUR (1)
#define ZIDX_SEEK_END (2)

/**
 * Callback type to read from a stream.
 *
 * \param stream_context Pointer to user-defined stream data.
 * \param buffer         Buffer to read into.
 * \param nbytes         Number of bytes to read.
 *
 * \return Number of bytes successfully read.
 * \return Less than \p nbytes on error.
 */
typedef
size_t (*zidx_stream_read_callback)(void *stream_context, unsigned char *buffer,
                                    size_t nbytes);

/**
 * Callback type to write to a stream.
 *
 * \param stream_context Pointer to user-defined stream data.
 * \param buffer         Buffer to write from.
 * \param nbytes         Number of bytes to write.
 *
 * \return Number of bytes successfully written.
 * \return Less than \p nbytes on error.
 */
typedef
size_t (*zidx_stream_write_callback)(void *stream_context,
                                     const unsigned char *buffer,
                                     size_t nbytes);

/**
 * Callback type to seek to an offset in a stream.
 *
 * \param stream_context Pointer to user-defined stream data.
 * \param offset         Number of bytes relative to the \p whence.
 * \param whence         Position used as reference for the \p offset.
 *
 * \return Nonnegative value on success.
 * \return Negative value on error.
 */
typedef
int (*zidx_stream_seek_callback)(void *stream_context, off_t offset,
                                 int whence);

/**
 * Callback type to get current offset in a stream.
 *
 * \param stream_context Pointer to user-defined stream data.
 *
 * \return Current offset in the stream.
 * \return Negative value on error.
 */
typedef
off_t (*zidx_stream_tell_callback)(void *stream_context);

/**
 * Callback type to check if end-of-file is reached in a stream.
 *
 * \param stream_context Pointer to user-defined stream data.
 *
 * \return Positive value if end-of-file is reached.
 * \return Zero if end-of-file is not reached.
 * \return Negative value on error.
 */
typedef
int (*zidx_stream_eof_callback)(void *stream_context);

/**
 * Callback type to check if any errors happened while reading from/writing to a
 * stream.
 *
 * This function will be used to check and return specific error in case of
 * zidx_stream_read_callback() or zidx_stream_write_callback() fail to read or
 * write some part of data. Note that this method is not used for checking
 * errors for stream API calls which can return negative value themselves to
 * indicate errors.
 *
 * \param stream_context Pointer to user-defined stream data.
 *
 * \return Nonzero value if there had been an error in previous read/write call.
 */
typedef
int (*zidx_stream_error_callback)(void *stream_context);

/**
 * Callback type to get length of a stream.
 *
 * \param stream_context Pointer to user-defined stream data.
 *
 * \return Length of the stream.
 * \return Negative value on error:
 *         - `-1` is reserved for the case where the stream is continuous.
 *         **Not implemented yet.**
 *
 * \todo Implement continuous streams. Either remove dependency to this
 *       function, or make it optional.
 */
typedef
off_t (*zidx_stream_length_callback)(void *stream_context);

/**
 * Layout for a gzip input stream.
 *
 * Currently only reading is supported.
 */
typedef struct zidx_compressed_stream
{
    zidx_stream_read_callback   read;
    zidx_stream_seek_callback   seek;
    zidx_stream_tell_callback   tell;
    zidx_stream_eof_callback    eof;
    zidx_stream_error_callback  error;
    zidx_stream_length_callback length;
    void *context;
} zidx_compressed_stream;

/**
 * Layout for a gzip index stream.
 */
typedef struct zidx_index_stream
{
    zidx_stream_read_callback  read;
    zidx_stream_write_callback write;
    zidx_stream_seek_callback  seek;
    zidx_stream_tell_callback  tell;
    zidx_stream_eof_callback   eof;
    zidx_stream_error_callback error;
    void *context;
} zidx_index_stream;

/** @} */ // end of streamapi

/* index/checkpoint data types */

typedef struct zidx_checkpoint_offset
{
    off_t uncompressed_offset;
    off_t compressed_offset;
    int compressed_offset_bits;
} zidx_checkpoint_offset;

typedef struct zidx_checkpoint
{
    zidx_checkpoint_offset offset;
    uint32_t checksum;
    unsigned char *window_data;
} zidx_checkpoint;

typedef enum zidx_stream_state
{
    ZIDX_EXPECT_FILE_HEADERS = 1,
    ZIDX_EXPECT_DEFLATE_BLOCKS = 2
} zidx_stream_state;

typedef enum zidx_stream_type
{
    ZIDX_STREAM_DEFLATE,
    ZIDX_STREAM_GZIP,
    ZIDX_STREAM_GZIP_OR_ZLIB
} zidx_stream_type;

typedef enum zidx_checksum_option
{
    ZIDX_CHECKSUM_DISABLED,
    ZIDX_CHECKSUM_DEFAULT,
    ZIDX_CHECKSUM_FORCE_CRC32,
    ZIDX_CHECKSUM_FORCE_ADLER32
} zidx_checksum_option;

typedef struct zidx_index
{
    zidx_compressed_stream *compressed_stream;
    zidx_stream_state stream_state;
    zidx_stream_type stream_type;
    z_stream* z_stream;
    int list_count;
    int list_capacity;
    zidx_checkpoint *list;
    zidx_checksum_option checksum_option;
    unsigned char *compressed_data_buffer;
    int compressed_data_buffer_size;
    int window_size;
} zidx_index;

/* read/write/seek/index functions */

typedef
int (*zidx_block_callback)(void *context,
                           zidx_index *index,
                           zidx_checkpoint_offset *offset);

int zidx_index_init(zidx_index* index,
                    zidx_compressed_stream* compressed_stream);
int zidx_index_init_advanced(zidx_index* index,
                             zidx_compressed_stream* compressed_stream,
                             zidx_stream_type stream_type,
                             zidx_checksum_option checksum_option,
                             z_stream* z_stream_ptr, int initial_capacity,
                             int window_size, int compressed_data_buffer_size);
int zidx_index_destroy(zidx_index* index);
int zidx_read(zidx_index* index, unsigned char *buffer, size_t nbytes);
int zidx_read_advanced(zidx_index* index, unsigned char *buffer,
                       size_t nbytes, zidx_block_callback block_callback,
                       void *callback_context);
int zidx_seek(zidx_index* index, off_t offset, int whence);
int zidx_seek_advanced(zidx_index* index, off_t offset, int whence,
                       zidx_block_callback block_callback,
                       void *callback_context);
off_t zidx_tell(zidx_index* index);
int zidx_rewind(zidx_index* index);

int zidx_build_index(zidx_index* index, off_t spacing_length);
int zidx_build_index_advanced(zidx_index* index,
                              zidx_block_callback block_callback,
                              void *callback_context);

int zidx_create_checkpoint(zidx_checkpoint* new_checkpoint,
                           zidx_checkpoint_offset* offset,
                           z_stream* z_stream_ptr);
int zidx_add_checkpoint(zidx_index* index, zidx_checkpoint* checkpoint);
int zidx_get_checkpoint(zidx_index* index, off_t offset);

void zidx_extend_index_size(zidx_index* index, size_t nmembers);
void zidx_shrink_index_size(zidx_index* index);

/* index import/export functions */

typedef
int (*zidx_import_filter_callback)(void *import_context,
                                   zidx_index *index,
                                   zidx_checkpoint_offset *offset);

typedef
int (*zidx_export_filter_callback)(void *export_context,
                                   zidx_index *index,
                                   zidx_checkpoint *offset);

int zidx_import_advanced(zidx_index *index,
                         const zidx_index_stream *stream,
                         zidx_import_filter_callback filter,
                         void *filter_context);

int zidx_export_advanced(zidx_index *index,
                         const zidx_index_stream *stream,
                         zidx_export_filter_callback filter,
                         void *filter_context);

int zidx_import(zidx_index *index, FILE* input_index_file);
int zidx_export(zidx_index *index, FILE* output_index_file);

/* raw file callbacks */

int zidx_compressed_file_init(zidx_compressed_stream *stream, FILE *f);
int zidx_index_file_init(zidx_index_stream *stream, FILE *f);
size_t zidx_raw_file_read(void *file, unsigned char *buffer, size_t nbytes);
size_t zidx_raw_file_write(void *file, const unsigned char *buffer,
                           size_t nbytes);
int zidx_raw_file_seek(void *file, off_t offset, int whence);
off_t zidx_raw_file_tell(void *file);
int zidx_raw_file_eof(void *file);
int zidx_raw_file_error(void *file);
off_t zidx_raw_file_length(void *file);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _ZIDX_H_