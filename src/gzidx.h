#ifndef _GZIDX_H_
#define _GZIDX_H_

#include <stdio.h>     // FILE
#include <sys/types.h> // off_t, size_t, ssize_t

#ifdef __cplusplus
extern "C" {
#endif

#define GZIDX_WINDOW_SIZE (0x7FFF) // 32767

/* gzidx stream functionality */

typedef
size_t (*gzidx_stream_read_callback)(void *stream_context, void *buffer,
                                     size_t nbytes);

typedef
size_t (*gzidx_stream_write_callback)(void *stream_context, const void *buffer,
                                      size_t nbytes);

typedef
int (*gzidx_stream_seek_callback)(void *stream_context, off_t offset,
                                  int whence);

typedef
off_t (*gzidx_stream_tell_callback)(void *stream_context);

typedef
int (*gzidx_stream_eof_callback)(void *stream_context);

typedef
int (*gzidx_stream_error_callback)(void *stream_context);

typedef
off_t (*gzidx_stream_length_callback)(void *stream_context);

typedef struct gzidx_gzip_input_stream_struct
{
    gzidx_stream_read_callback   read;
    gzidx_stream_seek_callback   seek;
    gzidx_stream_tell_callback   tell;
    gzidx_stream_eof_callback    eof;
    gzidx_stream_error_callback  error;
    gzidx_stream_length_callback length;
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

/* index/checkpoint data types */

typedef struct gzidx_checkpoint_offset_struct
{
    off_t uncompressed_offset;
    off_t compressed_offset;
    int compressed_offset_bits;
} gzidx_checkpoint_offset;

typedef struct gzidx_checkpoint_struct
{
    gzidx_checkpoint_offset offset;
    void *preceding_uncompressed_data;
} gzidx_checkpoint;

typedef struct gzidx_index_struct
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
                             gzidx_next_block_callback next_block_callback);
int gzidx_gzip_seek(gzidx_index* index, off_t offset, int whence);
int gzidx_gzip_seek_advanced(gzidx_index* index, off_t offset, int whence,
                             gzidx_next_block_callback next_block_callback);
off_t gzidx_gzip_tell(gzidx_index* index);
int gzidx_gzip_rewind(gzidx_index* index);

int gzidx_build_index(gzidx_index* index, off_t spacing_length);
int gzidx_build_index_advanced(gzidx_index* index,
                               gzidx_next_block_callback next_block_callback);

int gzidx_save_checkpoint(gzidx_index* index, gzidx_checkpoint* checkpoint);
int gzidx_get_offset_checkpoint_index(gzidx_index* index, off_t offset);

void gzidx_extend_index(gzidx_index* index, size_t nmembers);
void gzidx_shrink_index(gzidx_index* index);

/* index import/export functions */

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

size_t gzidx_raw_file_read(void *file, void *buffer, size_t nbytes);
size_t gzidx_raw_file_write(void *file, const void *buffer, size_t nbytes);
int gzidx_raw_file_seek(void *file, off_t offset, int whence);
off_t gzidx_raw_file_tell(void *file);
int gzidx_raw_file_eof(void *file);
int gzidx_raw_file_error(void *file);
off_t gzidx_raw_file_length(void *file);

int gzidx_import(gzidx_index *index, FILE* input_index_file);
int gzidx_export(const gzidx_index *index, FILE* output_index_file);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _GZIDX_H_
