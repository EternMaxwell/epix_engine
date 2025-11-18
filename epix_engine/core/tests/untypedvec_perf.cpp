#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "epix/core/meta/info.hpp"
#include "epix/core/storage/untypedvec.hpp"

using namespace epix::core::storage;

struct HeavyPerf {
    std::string s;
    HeavyPerf() = default;
    HeavyPerf(const std::string& v) : s(v) {}
    HeavyPerf(const HeavyPerf& o) : s(o.s) {}
    HeavyPerf(HeavyPerf&& o) noexcept : s(std::move(o.s)) {}
};

static inline long long now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
}

void bench_int(size_t N, size_t reps) {
    const auto* int_desc = epix::meta::type_info::of<int>();

    long long best_untyped = LLONG_MAX;
    long long best_std     = LLONG_MAX;

    for (size_t r = 0; r < reps; ++r) {
        // untyped_vector
        untyped_vector uv(int_desc, N);
        long long t0 = now_us();
        for (size_t i = 0; i < N; ++i) {
            int v = static_cast<int>(i);
            uv.push_back_from_move(&v);
        }
        long long t1 = now_us();
        best_untyped = std::min(best_untyped, t1 - t0);

        // std::vector
        std::vector<int> sv;
        sv.reserve(N);
        t0 = now_us();
        for (size_t i = 0; i < N; ++i) {
            sv.push_back(static_cast<int>(i));
        }
        t1       = now_us();
        best_std = std::min(best_std, t1 - t0);
    }

    std::cout << "INT bench (N=" << N << ", reps=" << reps << "):\n";
    std::cout << "  untyped_vector best: " << best_untyped << " us\n";
    std::cout << "  std::vector      best: " << best_std << " us\n";

    // read/access benchmark: measure sequential reads via different APIs
    long long best_uv_get    = LLONG_MAX;
    long long best_uv_get_as = LLONG_MAX;
    long long best_sv_idx    = LLONG_MAX;

    // prepare data once
    untyped_vector uv_read(epix::meta::type_info::of<int>(), N);
    std::vector<int> sv_read;
    sv_read.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        int v = static_cast<int>(i);
        uv_read.push_back_from(&v);
        sv_read.push_back(v);
    }

    for (size_t r = 0; r < std::max<size_t>(1, reps / 2); ++r) {
        long long t0      = now_us();
        volatile int sink = 0;
        for (size_t i = 0; i < N; ++i) sink += *reinterpret_cast<int*>(uv_read.get(i));
        long long t1 = now_us();
        best_uv_get  = std::min(best_uv_get, t1 - t0);

        t0 = now_us();
        for (size_t i = 0; i < N; ++i) sink += uv_read.get_as<int>(i);
        t1             = now_us();
        best_uv_get_as = std::min(best_uv_get_as, t1 - t0);

        t0 = now_us();
        for (size_t i = 0; i < N; ++i) sink += sv_read[i];
        t1          = now_us();
        best_sv_idx = std::min(best_sv_idx, t1 - t0);
        (void)sink;
    }

    std::cout << "INT read (N=" << N << "):\n";
    std::cout << "  uv.get raw best: " << best_uv_get << " us\n";
    std::cout << "  uv.get_as best:  " << best_uv_get_as << " us\n";
    std::cout << "  sv[idx] best:    " << best_sv_idx << " us\n";
}

void bench_heavy(size_t N, size_t reps) {
    const auto* heavy_desc = epix::meta::type_info::of<HeavyPerf>();

    long long best_untyped = LLONG_MAX;
    long long best_std     = LLONG_MAX;

    for (size_t r = 0; r < reps; ++r) {
        // untyped_vector using emplace_back
        untyped_vector uv(epix::meta::type_info::of<HeavyPerf>(), N);
        long long t0 = now_us();
        for (size_t i = 0; i < N; ++i) {
            uv.emplace_back<HeavyPerf>(std::to_string(i));
        }
        long long t1 = now_us();
        best_untyped = std::min(best_untyped, t1 - t0);

        // std::vector
        std::vector<HeavyPerf> sv;
        sv.reserve(N);
        t0 = now_us();
        for (size_t i = 0; i < N; ++i) {
            sv.emplace_back(std::to_string(i));
        }
        t1       = now_us();
        best_std = std::min(best_std, t1 - t0);
    }

    std::cout << "HEAVY bench (N=" << N << ", reps=" << reps << "):\n";
    std::cout << "  untyped_vector best: " << best_untyped << " us\n";
    std::cout << "  std::vector      best: " << best_std << " us\n";

    // read/access benchmark for HeavyPerf (use get_as only; raw get would require cast)
    long long best_uv_get_raw = LLONG_MAX;
    long long best_uv_get_as  = LLONG_MAX;
    long long best_sv_idx     = LLONG_MAX;

    untyped_vector uv_read(epix::meta::type_info::of<HeavyPerf>(), N);
    std::vector<HeavyPerf> sv_read;
    sv_read.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        uv_read.emplace_back<HeavyPerf>(std::to_string(i));
        sv_read.emplace_back(std::to_string(i));
    }

    for (size_t r = 0; r < std::max<size_t>(1, reps / 2); ++r) {
        long long t0          = now_us();
        volatile size_t dummy = 0;  // volatile to prevent optimization
        // raw get + reinterpret_cast
        for (size_t i = 0; i < N; ++i) dummy += reinterpret_cast<const HeavyPerf*>(uv_read.get(i))->s.size();
        long long t1    = now_us();
        best_uv_get_raw = std::min(best_uv_get_raw, t1 - t0);

        t0 = now_us();
        for (size_t i = 0; i < N; ++i) dummy += uv_read.get_as<HeavyPerf>(i).s.size();
        t1             = now_us();
        best_uv_get_as = std::min(best_uv_get_as, t1 - t0);

        t0 = now_us();
        for (size_t i = 0; i < N; ++i) dummy += sv_read[i].s.size();
        t1          = now_us();
        best_sv_idx = std::min(best_sv_idx, t1 - t0);
        (void)dummy;
    }

    std::cout << "HEAVY read (N=" << N << "):\n";
    std::cout << "  uv.get raw best: " << best_uv_get_raw << " us\n";
    std::cout << "  uv.get_as best:  " << best_uv_get_as << " us\n";
    std::cout << "  sv[idx] best:    " << best_sv_idx << " us\n";
}

int main() {
    // Tune these parameters as desired; keep moderate defaults so tests finish quickly.
    const size_t N_int    = 1000000;  // number of elements for int test
    const size_t reps_int = 10;

    const size_t N_heavy    = 500000;  // number of elements for heavy test
    const size_t reps_heavy = 10;

    bench_int(N_int, reps_int);
    bench_heavy(N_heavy, reps_heavy);

    return 0;
}
