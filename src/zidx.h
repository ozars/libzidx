/**
 * \file
 * libzidx API header.
 */
#ifndef ZIDX_H
#define ZIDX_H

#include <stdio.h>     // FILE*
#include <stdint.h>
#include <sys/types.h> // off_t

#include "zidx_stream.h"

#define ZX_DEFAULT_INITIAL_LIST_CAPACITY       (8)
#define ZX_DEFAULT_WINDOW_SIZE                 (32768)
#define ZX_DEFAULT_COMPRESSED_DATA_BUFFER_SIZE (16384)
#define ZX_DEFAULT_SEEKING_DATA_BUFFER_SIZE    (32768)

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ZLIB_H
/* Typedef declaration of z_stream from zlib.h required for z_stream* */
typedef struct z_stream_s z_stream;
#endif

/* index/checkpoint data types */

typedef struct zidx_checkpoint_offset_s zidx_checkpoint_offset;
typedef struct zidx_checkpoint_s        zidx_checkpoint;
typedef struct zidx_index_s             zidx_index;

typedef enum zidx_stream_state
{
    ZX_INVALID_STATE,
    ZX_EXPECT_FILE_HEADERS,
    ZX_EXPECT_DEFLATE_BLOCKS,
    ZX_EXPECT_FILE_TRAILER
} zidx_stream_state;

typedef enum zidx_stream_type
{
    ZX_STREAM_DEFLATE,
    ZX_STREAM_GZIP,
    ZX_STREAM_GZIP_OR_ZLIB
} zidx_stream_type;

typedef enum zidx_checksum_option
{
    ZX_CHECKSUM_DISABLED,
    ZX_CHECKSUM_DEFAULT,
    ZX_CHECKSUM_FORCE_CRC32,
    ZX_CHECKSUM_FORCE_ADLER32
} zidx_checksum_option;

/* read/write/seek/index functions */

typedef
int (*zidx_block_callback)(void *context,
                           zidx_index *index,
                           zidx_checkpoint_offset *offset,
                           int is_last_block);

int zidx_index_init(zidx_index* index,
                    zidx_comp_stream* comp_stream);
int zidx_index_init_advanced(zidx_index* index,
                             zidx_comp_stream* comp_stream,
                             zidx_stream_type stream_type,
                             zidx_checksum_option checksum_option,
                             z_stream* z_stream_ptr, int initial_capacity,
                             unsigned int window_size,
                             int comp_data_buffer_size,
                             int seeking_data_buffer_size);
int zidx_index_destroy(zidx_index* index);
int zidx_read(zidx_index* index, uint8_t *buffer, int nbytes);
int zidx_read_advanced(zidx_index* index, uint8_t *buffer,
                       int nbytes, zidx_block_callback block_callback,
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

zidx_checkpoint* zidx_create_checkpoint();
int zidx_fill_checkpoint(zidx_index* index,
                         zidx_checkpoint* new_checkpoint,
                         zidx_checkpoint_offset* offset);
int zidx_add_checkpoint(zidx_index* index, zidx_checkpoint* checkpoint);
int zidx_get_checkpoint(zidx_index* index, off_t offset);

int zidx_extend_index_size(zidx_index* index, int nmembers);
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


#ifdef __cplusplus
} // extern "C"
#endif

#endif // ZIDX_H
