#include <stdlib.h>
#include "zidx_streamlike.h"

streamlike_t* sl_zx_open(zidx_index *index)
{
    streamlike_t *sl;
    if (!index) {
        return NULL;
    }

    sl = malloc(sizeof(streamlike_t));
    if (!sl) {
        return NULL;
    }

    sl->context      = index;
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

void sl_zx_close(streamlike_t *stream)
{
    free(stream);
}

size_t sl_zx_read_cb(void *context, void *buffer, size_t size)
{
    return zidx_read(context, buffer, size);
}

int sl_zx_seek_cb(void *context, off_t offset, int whence)
{
    switch(whence) {
        case SL_SEEK_SET:
            return zidx_seek(context, offset);
        default:
            /* TODO */
            return ZX_ERR_NOT_IMPLEMENTED;
    }
}

off_t sl_zx_tell_cb(void *context)
{
    return zidx_tell(context);
}

int sl_zx_eof_cb(void *context)
{
    return zidx_eof(context);
}

int sl_zx_error_cb(void *context)
{
    return zidx_error(context);
}

off_t sl_zx_length_cb(void *context)
{
    return zidx_uncomp_size(context);
}

sl_seekable_t sl_zx_seekable_cb(void *context)
{
    return SL_SEEKING_CHECKPOINTS;
}

int sl_zx_ckp_count_cb(void *context)
{
    return zidx_checkpoint_count(context);
}

const sl_ckp_t* sl_zx_ckp_cb(void *context, int idx)
{
    return (void*)zidx_get_checkpoint(context, idx);
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
