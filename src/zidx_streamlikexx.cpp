extern "C" {
#include "zidx_streamlike.h"
}
#include "zidx_streamlike.hpp"
#include <stdexcept>

namespace streamlike {

StreamlikeZidx::StreamlikeZidx(Streamlike&& gzipStream,
                               Streamlike& indexStream)
        : Streamlike(sl_zx_from_indexed_stream(getSelf(gzipStream), getSelf(indexStream)),
                     sl_zx_close),
          mGzipStream(std::move(gzipStream)) {
    if (!self) {
        throw std::runtime_error("Couldn't create zidx stream");
    }
}

} // namespace streamlike
