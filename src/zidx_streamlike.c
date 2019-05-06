#include <stdlib.h>
#include "zidx_streamlike.h"

#define NO_SEEK_REQUIRED (-1)

typedef struct zidx_context_s
{
    zidx_index *index;
    off_t seek_offset;
} zidx_context_t;

streamlike_t* sl_zx_open(zidx_index *index)
{
    streamlike_t *sl;
    zidx_context_t *ctx;

    if (!index) {
        return NULL;
    }

    sl = malloc(sizeof(streamlike_t));
    if (!sl) {
        return NULL;
    }

    ctx = malloc(sizeof(zidx_context_t));
    if (!ctx) {
        free(sl);
        return NULL;
    }

    ctx->index = index;
    ctx->seek_offset = NO_SEEK_REQUIRED;

    sl->context      = ctx;
    sl->read         = sl_zx_read_cb;
    sl->input        = NULL;
    sl->write        = NULL;
    sl->flush        = NULL;
    sl->seek         = sl_zx_seek_cb;
    sl->tell         = sl_zx_tell_cb;
    sl->eof          = sl_zx_eof_cb;
    sl->error        = sl_zx_error_cb;
    sl->length       = sl_zx_length_cb;
    sl->seekable     = sl_zx_seekable_cb;

    sl->ckp_count    = sl_zx_ckp_count_cb;
    sl->ckp          = sl_zx_ckp_cb;
    sl->ckp_offset   = sl_zx_ckp_offset_cb;
    sl->ckp_metadata = sl_zx_ckp_metadata_cb;
    return sl;
}

int sl_zx_close(streamlike_t *stream)
{
    int ret = 0;
    if (stream && stream->context) {
        free(stream->context);
        stream->context = NULL;
    }
    free(stream);
    return 0;
}

streamlike_t* sl_zx_from_stream(streamlike_t *gzip_stream)
{
    return sl_zx_from_indexed_stream(gzip_stream, NULL);
}

streamlike_t* sl_zx_from_indexed_stream(streamlike_t *gzip_stream, streamlike_t *index_stream)
{
    zidx_index *index;
    streamlike_t *stream;

    if (!gzip_stream) {
        return NULL;
    }

    index = zidx_index_create();
    if (!index) {
        return NULL;
    }
    if (zidx_index_init(index, gzip_stream) != ZX_RET_OK) {
        goto fail;
    }
    if (index_stream && zidx_import(index, index_stream) != ZX_RET_OK) {
        goto fail;
    }
    stream = sl_zx_open(index);
    if (!stream) {
        goto fail;
    }
    return stream;
  fail:
    zidx_index_destroy(index);
    return NULL;
}

size_t sl_zx_read_cb(void *context, void *buffer, size_t size)
{
    zidx_context_t *ctx = context;
    int ret;
    if (ctx->seek_offset != NO_SEEK_REQUIRED) {
        ret = zidx_seek(ctx->index, ctx->seek_offset);
        if (ret != ZX_RET_OK)
            return ret;
        ctx->seek_offset = NO_SEEK_REQUIRED;
    }
    ret = zidx_read(ctx->index, buffer, size);
    if (ret >= 0)
        return ret;
    /* NOTE: Error type is ignored due to int->size_t conversion. */
    return 0;
}

int sl_zx_seek_cb(void *context, off_t offset, int whence)
{
    zidx_context_t *ctx = context;
    switch(whence) {
        case SL_SEEK_SET:
            ctx->seek_offset = offset;
            return ZX_RET_OK;
        default:
            /* TODO */
            return ZX_ERR_NOT_IMPLEMENTED;
    }
}

off_t sl_zx_tell_cb(void *context)
{
    zidx_context_t *ctx = context;
    if (ctx->seek_offset != NO_SEEK_REQUIRED)
        return ctx->seek_offset;
    return zidx_tell(ctx->index);
}

int sl_zx_eof_cb(void *context)
{
    zidx_context_t *ctx = context;
    return zidx_eof(ctx->index);
}

int sl_zx_error_cb(void *context)
{
    zidx_context_t *ctx = context;
    return zidx_error(ctx->index);
}

off_t sl_zx_length_cb(void *context)
{
    zidx_context_t *ctx = context;
    return zidx_uncomp_size(ctx->index);
}

sl_seekable_t sl_zx_seekable_cb(void *context)
{
    return SL_SEEKING_CHECKPOINTS;
}

int sl_zx_ckp_count_cb(void *context)
{
    zidx_context_t *ctx = context;
    return zidx_checkpoint_count(ctx->index);
}

const sl_ckp_t* sl_zx_ckp_cb(void *context, int idx)
{
    zidx_context_t *ctx = context;
    return (void*)zidx_get_checkpoint(ctx->index, idx);
}

off_t sl_zx_ckp_offset_cb(void *context, const sl_ckp_t* ckp)
{
    return zidx_get_checkpoint_offset((const zidx_checkpoint*)ckp);
}

size_t sl_zx_ckp_metadata_cb(void *context, const sl_ckp_t* ckp,
                             const void** result)
{
    return zidx_get_checkpoint_window((const zidx_checkpoint*) ckp, result);
}
