#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <print>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#endif
import epix.extension.grid;

#ifdef EPIX_IMPORT_STD
import std;
#endif

using namespace epix::ext::grid;
using std::int32_t;
using std::size_t;
using std::uint32_t;

// ============================================================
// benchmark_basic — ALL grid types, ALL operations, RNG-based
//   Grids: packed, dense, sparse, dense_extendible, tree_extendible,
//          tree, bit, any (over dense)
//   Ops:   contains, get, iter_pos/cells/iter, get_unsafe,
//          set/set_new/remove/take/clear,
//          set_unsafe/remove_unsafe/take_unsafe
// ============================================================

namespace {

using pos2  = std::array<std::int32_t, 2>;
using upos2 = std::array<std::uint32_t, 2>;

constexpr uint32_t kDomW = 256, kDomH = 256;
constexpr std::size_t kN = 16384;
constexpr int kIters     = 5;

// ── RNG ─────────────────────────────────────────────────────
std::mt19937 make_rng(uint32_t seed) { return std::mt19937(seed); }

std::vector<pos2> make_pos_i32(uint32_t seed) {
    auto rng = make_rng(seed);
    std::uniform_int_distribution<int32_t> dist(0, kDomW - 1);
    std::vector<pos2> v;
    v.reserve(kN * 2);
    for (size_t i = 0; i < kN * 2; ++i) v.push_back({dist(rng), dist(rng)});
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    if (v.size() > kN) v.resize(kN);
    return v;
}
std::vector<upos2> make_pos_u32(uint32_t seed) {
    auto rng = make_rng(seed);
    std::uniform_int_distribution<uint32_t> dist(0, kDomW - 1);
    std::vector<upos2> v;
    v.reserve(kN * 2);
    for (size_t i = 0; i < kN * 2; ++i) v.push_back({dist(rng), dist(rng)});
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    if (v.size() > kN) v.resize(kN);
    return v;
}
std::vector<int> make_vals(uint32_t seed) {
    auto rng = make_rng(seed);
    std::uniform_int_distribution<int> dist(0, 9999);
    std::vector<int> v;
    v.reserve(kN);
    for (size_t i = 0; i < kN; ++i) v.push_back(dist(rng));
    return v;
}

// ── Timing ──────────────────────────────────────────────────
template <typename F>
double bench(F&& f) {
    double total = 0.0;
    for (int i = 0; i < kIters; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        f();
        auto t1 = std::chrono::steady_clock::now();
        total += std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    return total / kIters;
}

// setup() runs untimed, then op() is timed — for benchmarks that need pre-seeding.
template <typename Setup, typename Op>
double bench_seeded(Setup&& setup, Op&& op) {
    double total = 0.0;
    for (int i = 0; i < kIters; ++i) {
        setup();
        auto t0 = std::chrono::steady_clock::now();
        op();
        auto t1 = std::chrono::steady_clock::now();
        total += std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    return total / kIters;
}

// ── Sink ────────────────────────────────────────────────────
int g_sink = 0;

// Compiler barrier — forces the compiler to materialize a value
// without emitting any real instructions.  Works on GCC, Clang, and MSVC.
template <typename T>
inline void DoNotOptimize(T const& val) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r,m"(val) : "memory");
#elif defined(_MSC_VER)
    // MSVC: _ReadWriteBarrier + volatile address-taken prevents elision.
    // <intrin.h> is pulled in transitively by the standard library.
    _ReadWriteBarrier();
    auto const* volatile p = &val;
    (void)p;
#else
    // Fallback for unknown compilers — volatile access forces materialization.
    (void)val;
#endif
}

// ── Assertion ───────────────────────────────────────────────
template <typename E>
void ok(const E& r, std::string_view ctx) {
    if (!r.has_value()) throw std::runtime_error(std::string(ctx));
}

// ── Seeding ─────────────────────────────────────────────────
template <typename G, typename Pos>
void seed(G& g, const std::vector<Pos>& P, const std::vector<int>& V) {
    for (size_t i = 0; i < kN / 2; ++i) ok(g.set(P[i], V[i]), "seed");
}
template <typename G, typename Pos>
void seed_bit(G& g, const std::vector<Pos>& P) {
    for (size_t i = 0; i < kN / 2; ++i) ok(g.set(P[i]), "bit seed");
}

// ── Report ──────────────────────────────────────────────────
// ── Grid type aliases ───────────────────────────────────────
using PK = packed_grid<2, int>;
using DN = dense_grid<2, int>;
using SP = sparse_grid<2, int>;
using DX = dense_extendible_grid<2, int>;
using TX = tree_extendible_grid<2, int>;
using TR = tree_grid<2, int>;
using BT = bit_grid<2>;
// Any-wrapper aliases
constexpr grid_category kCat = static_cast<grid_category>(
    static_cast<unsigned>(grid_category::iterable) | static_cast<unsigned>(grid_category::container) |
    static_cast<unsigned>(grid_category::unsafe_viewable) | static_cast<unsigned>(grid_category::unsafe_container) |
    static_cast<unsigned>(grid_category::constness) | static_cast<unsigned>(grid_category::copyable));
using UPK = any_grid<2, int&, kCat, uint32_t, uint32_t>;
using UDN = any_grid<2, int&, kCat, uint32_t, uint32_t>;
using USP = any_grid<2, int&, kCat, uint32_t, uint32_t>;
using UTR = any_grid<2, int&, kCat, uint32_t, uint32_t>;
using UDX = any_grid<2, int&, kCat, uint32_t, int32_t>;
using UTX = any_grid<2, int&, kCat, uint32_t, int32_t>;

}  // namespace

// ── Report (outside anon namespace for std::format visibility) ──
struct Row {
    std::string op;
    double pk, dn, sp, dx, tx, tr, bt;    // raw grids
    double udn, upk, usp, udx, utx, utr;  // any wrappers
};

void report(std::string_view title, const std::vector<Row>& rows) {
    std::println("\n{}", title);
    std::println("{:<20}{:>8}{:>8}{:>8}{:>8}{:>8}{:>8}{:>8}  {:>8}{:>8}{:>8}{:>8}{:>8}{:>8}", "Op", "Packed", "Dense",
                 "Sparse", "DnExt", "TrExt", "Tree", "Bit", "u-Pkd", "u-Dns", "u-Spr", "u-DxE", "u-TxE", "u-Tree");
    std::println("{}", std::string(117, '-'));
    auto fmt_val = [](double v) {
        if (v < 0.0) return std::string("     ---");
        return std::format("{:>8.3f}", v);
    };
    for (auto& r : rows)
        std::println("{:<20}{}{}{}{}{}{}{}  {}{}{}{}{}{}", r.op, fmt_val(r.pk), fmt_val(r.dn), fmt_val(r.sp),
                     fmt_val(r.dx), fmt_val(r.tx), fmt_val(r.tr), fmt_val(r.bt), fmt_val(r.upk), fmt_val(r.udn),
                     fmt_val(r.usp), fmt_val(r.udx), fmt_val(r.utx), fmt_val(r.utr));
}

// ============================================================
// Helpers for bit_grid special cases (must be namespace-scope)
template <typename G>
constexpr bool is_bit_v = std::same_as<std::remove_cvref_t<G>, BT>;

// ============================================================
int main() {
    const auto Pi = make_pos_i32(42u);  // int32  — extendible grids
    const auto Pu = make_pos_u32(42u);  // uint32 — fixed / bit / any
    const auto V  = make_vals(99u);

    // Seed dispatcher: bit_grid uses seed_bit, others use seed.
    auto auto_seed = [&](auto& g, const auto& P, const auto& pV) {
        if constexpr (is_bit_v<decltype(g)>)
            seed_bit(g, P);
        else
            seed(g, P, pV);
    };

    // Timed: creates grid, optionally seeds (untimed), then times op.
    // Op must return bool: true = supported, false = unsupported (→ -1.0 sentinel).
    auto timed = [&](auto make_grid, const auto& P, const auto& pV, bool do_seed, auto&& op) -> double {
        double total = 0.0;
        for (int i = 0; i < kIters; ++i) {
            auto g = make_grid();
            if (do_seed) auto_seed(g, P, pV);
            auto t0 = std::chrono::steady_clock::now();
            bool ok = op(g, P, pV);
            auto t1 = std::chrono::steady_clock::now();
            if (!ok) return -1.0;
            total += std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
        return total / kIters;
    };

    // Template dispatch — benchmarks op across all 13 grid variants.
    // Set do_seed=true for ops that need pre-populated grids (contains, get, iter, remove, etc.).
    auto bench_all = [&]<typename Op>(std::string name, Op&& op, bool do_seed = false) -> Row {
        Row r{.op = std::move(name)};
        r.pk  = timed([] { return PK({kDomW, kDomH}, 0); }, Pu, V, do_seed, op);
        r.dn  = timed([] { return DN({kDomW, kDomH}); }, Pu, V, do_seed, op);
        r.sp  = timed([] { return SP({kDomW, kDomH}); }, Pu, V, do_seed, op);
        r.dx  = timed([] { return DX(); }, Pi, V, do_seed, op);
        r.tx  = timed([] { return TX(); }, Pi, V, do_seed, op);
        r.tr  = timed([] { return TR({kDomW, kDomH}); }, Pu, V, do_seed, op);
        r.bt  = timed([] { return BT({(uint32_t)kDomW, (uint32_t)kDomH}); }, Pu, V, do_seed, op);
        r.udn = timed([] { return UDN(DN({kDomW, kDomH})); }, Pu, V, do_seed, op);
        r.upk = timed([] { return UPK(PK({kDomW, kDomH}, 0)); }, Pu, V, do_seed, op);
        r.usp = timed([] { return USP(SP({kDomW, kDomH})); }, Pu, V, do_seed, op);
        r.udx = timed([] { return UDX(DX{}); }, Pi, V, do_seed, op);
        r.utx = timed([] { return UTX(TX{}); }, Pi, V, do_seed, op);
        r.utr = timed([] { return UTR(TR({kDomW, kDomH})); }, Pu, V, do_seed, op);
        return r;
    };

    std::vector<Row> rows;

    // ── contains ─────────────────────────────────────────────
    rows.push_back(bench_all(
        "contains(hit)",
        [&](auto& g, const auto& P, const auto& pV) {
            int c = 0;
            for (size_t i = 0; i < kN / 2; ++i)
                if (g.contains(P[i])) ++c;
            g_sink += c;
            return true;
        },
        true));
    rows.push_back(bench_all(
        "contains(miss)",
        [&](auto& g, const auto& P, const auto& pV) {
            int c = 0;
            for (size_t i = kN / 2; i < kN; ++i)
                if (g.contains(P[i])) ++c;
            g_sink += c;
            return true;
        },
        true));
    report("contains  (N=16384, hit=8192 miss=8192, avg-of-5 ms)", rows);
    rows.clear();

    // ── get ──────────────────────────────────────────────────
    rows.push_back(bench_all(
        "get(hit)",
        [&](auto& g, const auto& P, const auto& pV) {
            if constexpr (is_bit_v<decltype(g)>) {
                int c = 0;
                for (size_t i = 0; i < kN / 2; ++i) {
                    auto r = g.get(P[i]);
                    if (r && *r) ++c;
                }
                g_sink += c;
            } else {
                int s = 0;
                for (size_t i = 0; i < kN / 2; ++i) {
                    auto r = g.get(P[i]);
                    if (r) s += *r;
                }
                g_sink += s;
            }
            return true;
        },
        true));
    rows.push_back(bench_all(
        "get(miss)",
        [&](auto& g, const auto& P, const auto& pV) {
            if constexpr (is_bit_v<decltype(g)>) {
                int c = 0;
                for (size_t i = kN / 2; i < kN; ++i) {
                    auto r = g.get(P[i]);
                    if (r && *r) ++c;
                }
                g_sink += c;
            } else {
                int s = 0;
                for (size_t i = kN / 2; i < kN; ++i) {
                    auto r = g.get(P[i]);
                    if (r) s += *r;
                }
                g_sink += s;
            }
            return true;
        },
        true));
    report("get       (N=16384, hit=8192 miss=8192, avg-of-5 ms)", rows);
    rows.clear();

    // ── iteration ────────────────────────────────────────────
    rows.push_back(bench_all(
        "iter_pos",
        [&](auto& g, const auto& P, const auto& pV) {
            size_t c = 0;
            if constexpr (is_bit_v<decltype(g)>) {
                for (auto p : g.iter_set()) {
                    ++c;
                    DoNotOptimize(p);
                }
            } else {
                for (auto p : g.iter_pos()) {
                    ++c;
                    DoNotOptimize(p);
                }
            }
            DoNotOptimize(c);
            g_sink += (int)c;
            return true;
        },
        true));
    rows.push_back(bench_all(
        "iter_cells",
        [&](auto& g, const auto& P, const auto& pV) {
            if constexpr (is_bit_v<decltype(g)>) {
                int c = 0;
                for (auto v : g.iter_set()) {
                    ++c;
                    DoNotOptimize(v);
                }
                DoNotOptimize(c);
                g_sink += c;
            } else {
                int s = 0;
                for (auto& v : g.iter_cells()) {
                    DoNotOptimize(v);
                    s += v;
                }
                DoNotOptimize(s);
                g_sink += s;
            }
            return true;
        },
        true));
    rows.push_back(bench_all(
        "iter",
        [&](auto& g, const auto& P, const auto& pV) {
            if constexpr (is_bit_v<decltype(g)>) {
                return false;
            } else {
                int s = 0;
                for (auto&& [p, v] : g.iter()) {
                    DoNotOptimize(p);
                    DoNotOptimize(v);
                    s += v;
                }
                DoNotOptimize(s);
                g_sink += s;
                return true;
            }
        },
        true));
    report("iteration (N=8192 occupied, packed=65536 all-cells, avg-of-5 ms)", rows);
    rows.clear();

    // ── unsafe get ───────────────────────────────────────────
    rows.push_back(bench_all(
        "get_unsafe",
        [&](auto& g, const auto& P, const auto& pV) {
            if constexpr (is_bit_v<decltype(g)>) {
                return false;
            } else {
                int s = 0;
                for (size_t i = 0; i < kN / 2; ++i) s += g.get_unsafe(P[i]);
                g_sink += s;
                return true;
            }
        },
        true));
    report("unsafe-get (N=8192 hits, avg-of-5 ms)", rows);
    rows.clear();

    // ── container ────────────────────────────────────────────
    rows.push_back(bench_all("set", [&](auto& g, const auto& P, const auto& pV) {
        if constexpr (is_bit_v<decltype(g)>) {
            for (size_t i = 0; i < kN; ++i) ok(g.set(P[i]), "set");
        } else {
            for (size_t i = 0; i < kN; ++i) ok(g.set(P[i], pV[i]), "set");
        }
        return true;
    }));
    rows.push_back(bench_all("set_new", [&](auto& g, const auto& P, const auto& pV) {
        if constexpr (is_bit_v<decltype(g)>) {
            return false;
        } else {
            // packed_grid: set_new always AlreadyOccupied → use set
            if constexpr (std::same_as<std::remove_cvref_t<decltype(g)>, PK>) {
                for (size_t i = 0; i < kN; ++i) ok(g.set(P[i], pV[i]), "set");
            } else {
                for (size_t i = 0; i < kN; ++i) ok(g.set_new(P[i], pV[i]), "set_new");
            }
            return true;
        }
    }));
    rows.push_back(bench_all(
        "remove",
        [&](auto& g, const auto& P, const auto& pV) {
            if constexpr (is_bit_v<decltype(g)>) {
                return false;
            } else {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(g)>, PK>) {
                    for (size_t i = 0; i < kN / 2; ++i) ok(g.reset(P[i]), "reset");
                } else {
                    for (size_t i = 0; i < kN / 2; ++i) ok(g.remove(P[i]), "remove");
                }
                return true;
            }
        },
        true));
    rows.push_back(bench_all(
        "take",
        [&](auto& g, const auto& P, const auto& pV) {
            if constexpr (is_bit_v<decltype(g)>) {
                return false;
            } else {
                for (size_t i = 0; i < kN / 2; ++i) ok(g.take(P[i]), "take");
                return true;
            }
        },
        true));
    rows.push_back(bench_all(
        "clear",
        [&](auto& g, const auto& P, const auto& pV) {
            if constexpr (is_bit_v<decltype(g)>)
                g.reset();
            else
                g.clear();
            return true;
        },
        true));
    report("container (N=16384 set/set_new, N=8192 remove/take, avg-of-5 ms)", rows);
    rows.clear();

    // ── unsafe container ─────────────────────────────────────
    rows.push_back(bench_all("set_unsafe", [&](auto& g, const auto& P, const auto& pV) {
        if constexpr (is_bit_v<decltype(g)>) {
            return false;
        } else {
            for (size_t i = 0; i < kN; ++i) g.set_unsafe(P[i], pV[i]);
            return true;
        }
    }));
    rows.push_back(bench_all(
        "remove_unsafe",
        [&](auto& g, const auto& P, const auto& pV) {
            if constexpr (is_bit_v<decltype(g)>) {
                return false;
            } else {
                for (size_t i = 0; i < kN / 2; ++i) g.remove_unsafe(P[i]);
                return true;
            }
        },
        true));
    rows.push_back(bench_all(
        "take_unsafe",
        [&](auto& g, const auto& P, const auto& pV) {
            if constexpr (is_bit_v<decltype(g)>) {
                return false;
            } else {
                for (size_t i = 0; i < kN / 2; ++i) g_sink += g.take_unsafe(P[i]);
                return true;
            }
        },
        true));
    report("unsafe container (N=16384 set_unsafe, N=8192 remove/take_unsafe, avg-of-5 ms)", rows);

    std::println("\n(sink={})", g_sink);
}
