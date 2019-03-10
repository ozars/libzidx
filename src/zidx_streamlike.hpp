#ifndef STREAMLIKE_ZIDX_HPP
#define STREAMLIKE_ZIDX_HPP

#include <utility>
#include <type_traits>

#include "streamlike.hpp"

namespace streamlike {

template<class T, class U = void>
class StreamlikeZidx : public Streamlike {
    public:
        StreamlikeZidx(T&& gzipStream);
        template<class V = U>
        StreamlikeZidx(T&& gzipStream, typename std::enable_if<
                       !std::is_same<V, void>::value, V>::type&& indexStream);
        StreamlikeZidx(StreamlikeZidx<T, U>&&) = default;
        StreamlikeZidx<T, U>& operator=(StreamlikeZidx<T, U>&&) = default;
        ~StreamlikeZidx();

    private:
        T mGzipStream;
};

class StreamlikeZidxImpl {
    private:
        using self_type = Streamlike::self_type;

        StreamlikeZidxImpl() = delete;

        static self_type createSelf(self_type gzipSelf);
        static self_type createSelf(self_type gzipSelf, self_type indexSelf);
        static void destroySelf(self_type self);

        template<class T, class U>
        friend class StreamlikeZidx;
};

template<class T, class U>
StreamlikeZidx<T, U>::StreamlikeZidx(T&& gzipStream)
        : Streamlike(StreamlikeZidxImpl::createSelf(getSelf(gzipStream))),
          mGzipStream(std::forward<T>(gzipStream)) {}

template<class T, class U>
template<class V>
StreamlikeZidx<T, U>::StreamlikeZidx(
            T&& gzipStream, typename std::enable_if<
                        !std::is_same<V, void>::value, V>::type&& indexStream)
        : Streamlike(StreamlikeZidxImpl::createSelf(
                            getSelf(gzipStream), getSelf(indexStream))),
          mGzipStream(std::forward<T>(gzipStream)) {}

template<class T, class U>
StreamlikeZidx<T, U>::~StreamlikeZidx() {
    StreamlikeZidxImpl::destroySelf(self);
}

template<class T>
StreamlikeZidx<T> createStreamlikeZidx(T&& gzipStream) {
    return StreamlikeZidx<T>(std::forward<T>(gzipStream));
}

template<class T, class U>
StreamlikeZidx<T, U> createStreamlikeZidx(T&& gzipStream, U&& indexStream) {
    return { std::forward<T>(gzipStream), std::forward<U>(indexStream) };
}

} // namespace streamlike

#endif /* STREAMLIKE_ZIDX_HPP */
