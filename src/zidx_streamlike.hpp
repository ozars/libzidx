#ifndef STREAMLIKE_ZIDX_HPP
#define STREAMLIKE_ZIDX_HPP

#include "streamlike.hpp"

#ifndef ZIDX_H
typedef struct zidx_index_s zidx_index;
#endif

namespace streamlike {

class StreamlikeZidx : public Streamlike {
    public:
        StreamlikeZidx(Streamlike&& gzipStream, Streamlike& indexStream);
    private:
        Streamlike mGzipStream;
};

} // namespace streamlike

#endif /* STREAMLIKE_ZIDX_HPP */
