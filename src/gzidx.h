/**
 * \file
 * libgzidx API header.
 */
#ifndef _GZIDX_H_
#define _GZIDX_H_

#include <stdio.h>     // FILE
#include <sys/types.h> // off_t, size_t, ssize_t

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * \defgroup streamapi Stream API
 *
 * Provide an abstract way to access to underlying resource (file, url etc.)
 *
 * @{
 *****************************************************************************/

#define GZIDX_SEEK_SET (0)
#define GZIDX_SEEK_CUR (1)
#define GZIDX_SEEK_END (2)

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
size_t (*gzidx_stream_read_callback)(void *stream_context, void *buffer,
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
size_t (*gzidx_stream_write_callback)(void *stream_context, const void *buffer,
                                      size_t nbytes);

/**
 * Callback type to seek to an offset in a stream.
 *
 * \param stream_context Pointer to user-defined stream data.
 * \param offset         Number of bytes relative to the \p whence.
 * \param whence         Position used as reference for the \p offset.
 *
 * \return Non-negative value on success.
 * \return Negative value on error.
 */
typedef
int (*gzidx_stream_seek_callback)(void *stream_context, off_t offset,
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
off_t (*gzidx_stream_tell_callback)(void *stream_context);

/**
 * Callback type to check if end-of-file is reached in a stream.
 *
 * \param stream_context Pointer to user-defined stream data.
 *
 * \return Current offset in the stream.
 * \return Positive value if end-of-file is reached.
 * \return Zero if end-of-file is not reached.
 * \return Negative value on error.
 */
typedef
int (*gzidx_stream_eof_callback)(void *stream_context);

/**
 * Callback type to check if any errors happened while reading from/writing to
 * a stream.
 *
 * This function will be used to check and return specific error in case of
 * gzidx_stream_read_callback() or gzidx_stream_write_callback() fail to read
 * or write some part of data. Note that this method is not used for checking
 * errors for stream API calls which can return negative value themselves to
 * indicate errors.
 *
 * \param stream_context Pointer to user-defined stream data.
 *
 * \return Negative value on error.
 */
typedef
int (*gzidx_stream_error_callback)(void *stream_context);

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
off_t (*gzidx_stream_length_callback)(void *stream_context);

/**
 * Layout for a gzip input stream.
 *
 * Currently only reading is supported.
 */
typedef struct gzidx_gzip_input_stream
{
    gzidx_stream_read_callback   read;
    gzidx_stream_seek_callback   seek;
    gzidx_stream_tell_callback   tell;
    gzidx_stream_eof_callback    eof;
    gzidx_stream_error_callback  error;
    gzidx_stream_length_callback length;
    void *context;
} gzidx_gzip_input_stream;

/**
 * Layout for a gzip index stream.
 */
typedef struct gzidx_gzip_index_stream
{
    gzidx_stream_read_callback  read;
    gzidx_stream_write_callback write;
    gzidx_stream_seek_callback  seek;
    gzidx_stream_tell_callback  tell;
    gzidx_stream_eof_callback   eof;
    gzidx_stream_error_callback error;
    void *context;
} gzidx_gzip_index_stream;

/** @} */ // end of streamapi

/* index/checkpoint data types */

typedef struct gzidx_checkpoint_offset
{
    off_t uncompressed_offset;
    off_t compressed_offset;
    int compressed_offset_bits;
} gzidx_checkpoint_offset;

typedef struct gzidx_checkpoint
{
    gzidx_checkpoint_offset offset;
    void *preceding_uncompressed_data;
} gzidx_checkpoint;

typedef struct gzidx_index
{
    void *gzip_stream;
    size_t stream_length;
    gzidx_checkpoint current_checkpoint;
    int list_count;
    int list_size;
    gzidx_checkpoint *list;
} gzidx_index;

/* read/write/seek/index functions */

typedef
int (*gzidx_next_block_callback)(void *context,
                                 gzidx_checkpoint *current_checkpoint);

int gzidx_index_init(gzidx_index* index, gzidx_gzip_input_stream* gzip_stream);
int gzidx_index_destroy(gzidx_index* index);
int gzidx_gzip_read(gzidx_index* index, void *buffer, size_t nbytes);
int gzidx_gzip_read_advanced(gzidx_index* index, void *buffer, size_t nbytes,
                             gzidx_next_block_callback next_block_callback,
                             void *next_block_callback_context);
int gzidx_gzip_seek(gzidx_index* index, off_t offset, int whence);
int gzidx_gzip_seek_advanced(gzidx_index* index, off_t offset, int whence,
                             gzidx_next_block_callback next_block_callback,
                             void *next_block_callback_context);
off_t gzidx_gzip_tell(gzidx_index* index);
int gzidx_gzip_rewind(gzidx_index* index);

int gzidx_build_index(gzidx_index* index, off_t spacing_length);
int gzidx_build_index_advanced(gzidx_index* index,
                               gzidx_next_block_callback next_block_callback,
                               void *next_block_callback_context);

int gzidx_save_checkpoint(gzidx_index* index, gzidx_checkpoint* checkpoint);
int gzidx_get_offset_checkpoint_index(gzidx_index* index, off_t offset);

void gzidx_extend_index_size(gzidx_index* index, size_t nmembers);
void gzidx_shrink_index_size(gzidx_index* index);

/* index import/export functions */

typedef
int (*gzidx_import_filter_callback)(void *import_context,
                                    gzidx_index *index,
                                    gzidx_checkpoint_offset *offset);

typedef
int (*gzidx_export_filter_callback)(void *export_context,
                                    gzidx_index *index,
                                    gzidx_checkpoint *offset);

int gzidx_import_advanced(gzidx_index *index,
                          const gzidx_gzip_index_stream *stream,
                          gzidx_import_filter_callback filter,
                          void *filter_context);

int gzidx_export_advanced(const gzidx_index *index,
                          const gzidx_gzip_index_stream *stream,
                          gzidx_export_filter_callback filter,
                          void *filter_context);


int gzidx_import(gzidx_index *index, FILE* input_index_file);
int gzidx_export(const gzidx_index *index, FILE* output_index_file);

/* raw file callbacks */

size_t gzidx_raw_file_read(void *file, void *buffer, size_t nbytes);
size_t gzidx_raw_file_write(void *file, const void *buffer, size_t nbytes);
int gzidx_raw_file_seek(void *file, off_t offset, int whence);
off_t gzidx_raw_file_tell(void *file);
int gzidx_raw_file_eof(void *file);
int gzidx_raw_file_error(void *file);
off_t gzidx_raw_file_length(void *file);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _GZIDX_H_
