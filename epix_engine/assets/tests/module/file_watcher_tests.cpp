#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>
#include <variant>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.assets;
import epix.async_channel;

using namespace epix::assets;
using namespace epix::async_channel;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Scoped temp directory: created on construction, recursively removed on
// destruction.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(std::string_view prefix = "epix_watcher_test") {
        path =
            std::filesystem::temp_directory_path() /
            (std::string(prefix) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// Write bytes to a file (creates or overwrites).
static void write_file(const std::filesystem::path& p, std::string_view content = "data") {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
}

// Poll the receiver until a predicate is satisfied or a timeout elapses.
// Returns the collected events that triggered the predicate, or empty on
// timeout.
template <typename Pred>
static std::vector<AssetSourceEvent> poll_until(const Receiver<AssetSourceEvent>& rx,
                                                Pred pred,
                                                std::chrono::milliseconds timeout   = std::chrono::milliseconds(3000),
                                                std::chrono::milliseconds poll_step = std::chrono::milliseconds(20)) {
    std::vector<AssetSourceEvent> events;
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        // Drain all currently available events.
        while (true) {
            auto r = rx.try_recv();
            if (!r) break;
            events.push_back(std::move(*r));
        }
        if (pred(events)) return events;
        std::this_thread::sleep_for(poll_step);
    }
    // One final drain.
    while (true) {
        auto r = rx.try_recv();
        if (!r) break;
        events.push_back(std::move(*r));
    }
    return events;  // caller checks pred
}

// Check that any event in the list satisfies a predicate.
template <typename Pred>
static bool any_event(const std::vector<AssetSourceEvent>& events, Pred pred) {
    for (const auto& e : events)
        if (pred(e)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// FileAssetWatcher tests
// ---------------------------------------------------------------------------

// Fixture: sets up a TempDir and a watcher + channel for each test.
class FileWatcherTest : public ::testing::Test {
   protected:
    TempDir dir;
    Receiver<AssetSourceEvent> rx{};
    std::unique_ptr<FileAssetWatcher> watcher;

    void SetUp() override {
        auto [tx, r] = epix::async_channel::unbounded<AssetSourceEvent>();
        rx           = std::move(r);
        watcher      = std::make_unique<FileAssetWatcher>(dir.path, std::move(tx));
        // Give efsw time to start watching before we touch the directory.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
};

// --- AddedAsset ---

TEST_F(FileWatcherTest, DetectsAddedFile) {
    auto p = dir.path / "new_asset.bin";
    write_file(p);

    auto events = poll_until(rx, [&](const auto& evs) {
        return any_event(evs, [&](const AssetSourceEvent& e) {
            auto* a = std::get_if<source_events::AddedAsset>(&e);
            return a && a->path.filename() == p.filename();
        });
    });

    EXPECT_TRUE(any_event(events, [&](const AssetSourceEvent& e) {
        auto* a = std::get_if<source_events::AddedAsset>(&e);
        return a && a->path.filename() == p.filename();
    }));
}

// --- ModifiedAsset ---

TEST_F(FileWatcherTest, DetectsModifiedFile) {
    auto p = dir.path / "mod_asset.bin";
    write_file(p, "initial");
    // Wait for the Add event to be consumed so modify is clearly separate.
    poll_until(
        rx,
        [&](const auto& evs) {
            return any_event(
                evs, [&](const AssetSourceEvent& e) { return std::holds_alternative<source_events::AddedAsset>(e); });
        },
        std::chrono::milliseconds(1500));

    write_file(p, "modified");

    auto events = poll_until(rx, [&](const auto& evs) {
        return any_event(evs, [&](const AssetSourceEvent& e) {
            auto* m = std::get_if<source_events::ModifiedAsset>(&e);
            return m && m->path.filename() == p.filename();
        });
    });

    EXPECT_TRUE(any_event(events, [&](const AssetSourceEvent& e) {
        auto* m = std::get_if<source_events::ModifiedAsset>(&e);
        return m && m->path.filename() == p.filename();
    }));
}

// --- RemovedAsset ---

TEST_F(FileWatcherTest, DetectsRemovedFile) {
    auto p = dir.path / "del_asset.bin";
    write_file(p);
    poll_until(
        rx,
        [&](const auto& evs) {
            return any_event(
                evs, [&](const AssetSourceEvent& e) { return std::holds_alternative<source_events::AddedAsset>(e); });
        },
        std::chrono::milliseconds(1500));

    std::filesystem::remove(p);

    auto events = poll_until(rx, [&](const auto& evs) {
        return any_event(evs, [&](const AssetSourceEvent& e) {
            auto* r = std::get_if<source_events::RemovedAsset>(&e);
            return r && r->path.filename() == p.filename();
        });
    });

    EXPECT_TRUE(any_event(events, [&](const AssetSourceEvent& e) {
        auto* r = std::get_if<source_events::RemovedAsset>(&e);
        return r && r->path.filename() == p.filename();
    }));
}

// --- RenamedAsset ---

TEST_F(FileWatcherTest, DetectsRenamedFile) {
    auto src = dir.path / "src_asset.bin";
    auto dst = dir.path / "dst_asset.bin";
    write_file(src);
    poll_until(
        rx,
        [&](const auto& evs) {
            return any_event(
                evs, [&](const AssetSourceEvent& e) { return std::holds_alternative<source_events::AddedAsset>(e); });
        },
        std::chrono::milliseconds(1500));

    std::filesystem::rename(src, dst);

    auto events = poll_until(rx, [&](const auto& evs) {
        return any_event(evs, [&](const AssetSourceEvent& e) {
            auto* rn = std::get_if<source_events::RenamedAsset>(&e);
            return rn && rn->new_path.filename() == dst.filename();
        });
    });

    EXPECT_TRUE(any_event(events, [&](const AssetSourceEvent& e) {
        auto* rn = std::get_if<source_events::RenamedAsset>(&e);
        return rn && rn->new_path.filename() == dst.filename();
    }));
}

// --- Meta file events ---

TEST_F(FileWatcherTest, DetectsAddedMetaFile) {
    auto p = dir.path / "asset.bin.meta";
    write_file(p);

    auto events = poll_until(rx, [&](const auto& evs) {
        return any_event(
            evs, [&](const AssetSourceEvent& e) { return std::holds_alternative<source_events::AddedMeta>(e); });
    });

    EXPECT_TRUE(any_event(events, [&](const AssetSourceEvent& e) {
        auto* m = std::get_if<source_events::AddedMeta>(&e);
        return m && m->path.filename() == p.filename();
    }));
}

// --- Subdirectory events ---

TEST_F(FileWatcherTest, DetectsAddedSubdirectory) {
    auto sub = dir.path / "subdir";

    std::filesystem::create_directory(sub);

    auto events = poll_until(rx, [&](const auto& evs) {
        return any_event(evs, [&](const AssetSourceEvent& e) {
            auto* a = std::get_if<source_events::AddedDirectory>(&e);
            return a && a->path.filename() == sub.filename();
        });
    });

    EXPECT_TRUE(any_event(events, [&](const AssetSourceEvent& e) {
        auto* a = std::get_if<source_events::AddedDirectory>(&e);
        return a && a->path.filename() == sub.filename();
    }));
}
