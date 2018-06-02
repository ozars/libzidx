#ifndef _GZIDX_H_
#define _GZIDX_H_

#include <stdio.h>     // FILE
#include <sys/types.h> // off_t, size_t

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gzidx_checkpoint_offset_struct
{
    int uncompressed_offset;
    int compressed_offset;
    int compressed_offset_bits;
} gzidx_checkpoint_offset;

typedef struct gzidx_checkpoint_struct
{
    gzidx_checkpoint_offset offset;
    void *preceding_uncompressed_data;
} gzidx_checkpoint;

typedef struct gzidx_index_struct
{
    int list_count;
    int list_size;
    gzidx_checkpoint *list;
} gzidx_index;

typedef
int (*gzidx_stream_read_callback)(void *stream_context, void *buffer,
                                  size_t nbytes);

typedef
int (*gzidx_stream_write_callback)(void *stream_context, const void *buffer,
                                   size_t nbytes);

typedef
int (*gzidx_stream_seek_callback)(void *stream_context, off_t offset,
                                  int whence);

typedef
int (*gzidx_stream_tell_callback)(void *stream_context);

typedef
int (*gzidx_stream_eof_callback)(void *stream_context);

typedef
int (*gzidx_stream_error_callback)(void *stream_context);

typedef
int (*gzidx_stream_size_callback)(void *stream_context);

typedef struct gzidx_gzip_input_stream_struct
{
    gzidx_stream_read_callback  read;
    gzidx_stream_seek_callback  seek;
    gzidx_stream_tell_callback  tell;
    gzidx_stream_eof_callback   eof;
    gzidx_stream_error_callback error;
    gzidx_stream_size_callback  size;
    void *context;
} gzidx_gzip_input_stream;

typedef struct gzidx_gzip_index_stream_struct
{
    gzidx_stream_read_callback  read;
    gzidx_stream_write_callback write;
    gzidx_stream_seek_callback  seek;
    gzidx_stream_tell_callback  tell;
    gzidx_stream_eof_callback   eof;
    gzidx_stream_error_callback error;
    void *context;
} gzidx_gzip_index_stream;

typedef
int (*gzidx_import_filter_callback)(void *import_context,
                                    gzidx_checkpoint_offset *offset);

typedef
int (*gzidx_export_filter_callback)(void *export_context,
                                    gzidx_checkpoint_offset *offset);

int gzidx_import_advanced(gzidx_index *index,
                          const gzidx_gzip_index_stream *stream,
                          gzidx_import_filter_callback filter);

int gzidx_export_advanced(const gzidx_index *index,
                          const gzidx_gzip_index_stream *stream,
                          gzidx_import_filter_callback filter);

int gzidx_raw_file_read(void *file, void *buffer, size_t nbytes);
int gzidx_raw_file_write(void *file, const void *buffer, size_t nbytes);
int gzidx_raw_file_seek(void *file, off_t offset, int whence);
int gzidx_raw_file_tell(void *file);
int gzidx_raw_file_eof(void *file);
int gzidx_raw_file_error(void *file);
int gzidx_raw_file_size(void *file);

int gzidx_import(gzidx_index *index, FILE* input_index_file);
int gzidx_export(const gzidx_index *index, FILE* output_index_file);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _GZIDX_H_
