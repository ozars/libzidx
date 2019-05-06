extern "C" {
#include "zidx_streamlike.h"
}
#include "zidx_streamlike.hpp"
#include <stdexcept>

namespace streamlike {

StreamlikeZidxImpl::self_type StreamlikeZidxImpl::createSelf(
        self_type gzipSelf) {
    auto self = sl_zx_from_stream(gzipSelf);
    if (!self) {
        throw std::runtime_error("Couldn't create buffer stream");
    }
    return self;
}

StreamlikeZidxImpl::self_type StreamlikeZidxImpl::createSelf(
        self_type gzipSelf, self_type indexSelf) {
    auto self = sl_zx_from_indexed_stream(gzipSelf, indexSelf);
    if (!self) {
        throw std::runtime_error("Couldn't create buffer stream");
    }
    return self;
}

void StreamlikeZidxImpl::destroySelf(self_type self) {
    if (self) {
        sl_zx_close(self);
    }
}

} // namespace streamlike
