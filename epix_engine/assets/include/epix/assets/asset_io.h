#pragma once

#include <epix/app.h>

namespace epix::assets {
struct AssetIO {
    epix::thread_pool pool;

    AssetIO(int threads = 4) : pool(threads) {}
    AssetIO(const AssetIO&)            = delete;
    AssetIO(AssetIO&&)                 = delete;
    AssetIO& operator=(const AssetIO&) = delete;
    AssetIO& operator=(AssetIO&&)      = delete;

    template <typename T>
    auto submit(T&& func) {
        return pool.submit_task(func);
    }
};
}  // namespace epix::assets