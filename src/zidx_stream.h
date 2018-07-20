/**
 * \file
 * libzidx Stream API header.
 *
 * Provide an abstract way to access to underlying resource stream (file, url
 * etc.)
 *
 */
#ifndef ZIDX_STREAM_H
#define ZIDX_STREAM_H

#include<stdio.h>
#include<stdint.h>
#include<sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZX_SEEK_SET (0)
#define ZX_SEEK_CUR (1)
#define ZX_SEEK_END (2)

/**
 * Callback type to read from a stream.
 *
 * \param stream_context Pointer to user-defined stream data.
 * \param buffer         Buffer to read into.
 * \param nbytes         Number of bytes to read.
 *
 * \return Number of bytes successfully read.
 */
typedef
int (*zidx_stream_read_callback)(void *stream_context, uint8_t *buffer,
                                 int nbytes);

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
int (*zidx_stream_write_callback)(void *stream_context,
                                  const uint8_t *buffer,
                                  int nbytes);

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
 * \return Nonzero value if end-of-file is reached.
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
typedef struct zidx_comp_stream
{
    zidx_stream_read_callback   read;
    zidx_stream_seek_callback   seek;
    zidx_stream_tell_callback   tell;
    zidx_stream_eof_callback    eof;
    zidx_stream_error_callback  error;
    zidx_stream_length_callback length;
    void *context;
} zidx_comp_stream;

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

zidx_comp_stream* zidx_create_comp_file(FILE *f);
zidx_index_stream* zidx_create_index_file(FILE *f);
int zidx_raw_file_read(void *file, uint8_t *buffer, int nbytes);
int zidx_raw_file_write(void *file, const uint8_t *buffer, int nbytes);
int zidx_raw_file_seek(void *file, off_t offset, int whence);
off_t zidx_raw_file_tell(void *file);
int zidx_raw_file_eof(void *file);
int zidx_raw_file_error(void *file);
off_t zidx_raw_file_length(void *file);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* ZIDX_STREAM_H */
