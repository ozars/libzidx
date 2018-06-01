#ifndef _GZIDX_H_
#define _GZIDX_H_

#include <stdio.h>     // FILE
#include <sys/types.h> // off_t, size_t

typedef struct gzidx_checkpoint_metadata_struct
{

} gzidx_checkpoint_metadata;

typedef struct gzidx_checkpoint_struct
{
    gzidx_checkpoint_metadata metadata;
    void *data;
} gzidx_checkpoint;

typedef struct gzidx_index_struct
{

} gzidx_index;

typedef
int (*gzidx_stream_read_callback)(void *stream_context, void *buffer,
                                  size_t nbytes);

typedef
int (*gzidx_stream_write_callback)(void *stream_context, const void *buffer,
                                   size_t nbytes);

typedef
int (*gzidx_stream_seek_callback)(void *stream_context, off_t offset,
                                  int origin);

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
                                    gzidx_checkpoint_metadata *metadata);

typedef
int (*gzidx_export_filter_callback)(void *export_context,
                                    gzidx_checkpoint_metadata *metadata);

int gzidx_import_advanced(gzidx_index *index,
                          const gzidx_gzip_index_stream *stream,
                          gzidx_import_filter_callback filter);

int gzidx_export_advanced(gzidx_index *index,
                          const gzidx_gzip_index_stream *stream,
                          gzidx_import_filter_callback filter);

int gzidx_raw_file_read(void *file, void *buffer, size_t nbytes);
int gzidx_raw_file_write(void *file, const void *buffer, size_t nbytes);
int gzidx_raw_file_seek(void *file, off_t offset, int origin);
int gzidx_raw_file_tell(void *file);
int gzidx_raw_file_eof(void *file);
int gzidx_raw_file_error(void *file);
int gzidx_raw_file_size(void *file);

int gzidx_import(gzidx_index *index, FILE* input_index_file);
int gzidx_export(gzidx_index *index, FILE* output_index_file);

#endif // _GZIDX_H_
