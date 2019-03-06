/**
 * \file
 * libzidx streamlike interface.
 */
#ifndef ZIDX_STREAMLIKE_H
#define ZIDX_STREAMLIKE_H

#include "zidx.h"

streamlike_t* sl_zx_open(zidx_index *index);
int sl_zx_close(streamlike_t *stream);

streamlike_t* sl_zx_from_stream(streamlike_t *gzip_stream);
streamlike_t* sl_zx_from_indexed_stream(streamlike_t *gzip_stream, streamlike_t *index_stream);

size_t sl_zx_read_cb(void *context, void *buffer, size_t size);
int sl_zx_seek_cb(void *context, off_t offset, int whence);
off_t sl_zx_tell_cb(void *context);
int sl_zx_eof_cb(void *context);
int sl_zx_error_cb(void *context);
off_t sl_zx_length_cb(void *context);
sl_seekable_t sl_zx_seekable_cb(void *context);

int sl_zx_ckp_count_cb(void *context);
const sl_ckp_t* sl_zx_ckp_cb(void *context, int idx);
off_t sl_zx_ckp_offset_cb(void *context, const sl_ckp_t* ckp);
size_t sl_zx_ckp_metadata_cb(void *context, const sl_ckp_t* ckp,
                             const void** result);

#endif /* ZIDX_STREAMLIKE_H */
