#include <gtest/gtest.h>

import std;
import epix.assets;

using namespace assets::memory;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Value make_val(std::string_view s) {
    auto buf = std::make_shared<std::vector<std::uint8_t>>(s.begin(), s.end());
    return Value::from_shared(buf);
}

// ---------------------------------------------------------------------------
// Thread safety — concurrent inserts to separate dirs
// ---------------------------------------------------------------------------

TEST(MemoryVfsThread, ConcurrentInserts_SeparateDirs_NoRace) {
    Directory d = Directory::create({});

    const int threads    = 8;
    const int per_thread = 200;
    std::vector<std::thread> workers;
    std::atomic<bool> ready{false};

    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([t, per_thread, &d, &ready]() {
            std::string dir = "t" + std::to_string(t);
            d.create_directory(dir);
            while (!ready.load()) std::this_thread::yield();
            for (int i = 0; i < per_thread; ++i) {
                std::string p = dir + "/file" + std::to_string(i) + ".txt";
                d.insert_file(p, make_val("x"));
            }
        });
    }

    ready.store(true);
    for (auto& w : workers) w.join();

    // Every file inserted should be queryable
    for (int t = 0; t < threads; ++t) {
        for (int i = 0; i < per_thread; ++i) {
            std::string p = "t" + std::to_string(t) + "/file" + std::to_string(i) + ".txt";
            auto r        = d.exists(p);
            ASSERT_TRUE(r.has_value());
            EXPECT_TRUE(r.value()) << "Missing: " << p;
        }
    }
}

// ---------------------------------------------------------------------------
// Thread safety — concurrent inserts to the same dir
// ---------------------------------------------------------------------------

TEST(MemoryVfsThread, ConcurrentInserts_SameDir_NoRace) {
    Directory d = Directory::create({});
    d.create_directory("shared");

    const int threads    = 8;
    const int per_thread = 100;
    std::atomic<bool> ready{false};
    std::vector<std::thread> workers;

    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([t, per_thread, &d, &ready]() {
            while (!ready.load()) std::this_thread::yield();
            for (int i = 0; i < per_thread; ++i) {
                std::string p = "shared/t" + std::to_string(t) + "_f" + std::to_string(i) + ".txt";
                d.insert_file(p, make_val("v"));
            }
        });
    }

    ready.store(true);
    for (auto& w : workers) w.join();

    auto listing = d.list_directory("shared", false);
    ASSERT_TRUE(listing.has_value());
    std::vector<std::filesystem::path> items;
    for (auto p : listing.value()) items.push_back(p);
    EXPECT_EQ(static_cast<int>(items.size()), threads * per_thread);
}

// ---------------------------------------------------------------------------
// Thread safety — concurrent mixed ops (insert / remove / move)
// ---------------------------------------------------------------------------

TEST(MemoryVfsThread, ConcurrentMixedOps_NoRace) {
    Directory d = Directory::create({});

    const int threads    = 6;
    const int per_thread = 150;
    std::atomic<bool> ready{false};
    std::vector<std::thread> workers;

    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([t, per_thread, &d, &ready]() {
            std::string dir = "mx" + std::to_string(t);
            d.create_directory(dir);
            while (!ready.load()) std::this_thread::yield();
            for (int i = 0; i < per_thread; ++i) {
                std::string p = dir + "/f" + std::to_string(i) + ".txt";
                d.insert_file(p, make_val("x"));
                if (i % 5 == 0) {
                    d.remove_file(p);
                } else if (i % 7 == 0) {
                    std::string q = dir + "/moved" + std::to_string(i) + ".txt";
                    d.move(p, q);
                }
            }
        });
    }

    ready.store(true);
    for (auto& w : workers) w.join();

    // Must not crash; listing must succeed
    auto r = d.list_directory("", false);
    ASSERT_TRUE(r.has_value());
}

// ---------------------------------------------------------------------------
// Callback fires from worker thread (not main thread)
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, CallbackFiredFromWorkerThread) {
    Directory d = Directory::create({});

    std::mutex m;
    std::set<std::thread::id> tids;
    std::thread::id main_tid = std::this_thread::get_id();

    auto id = d.add_callback([&](const DirEvent&) {
        std::lock_guard lk(m);
        tids.insert(std::this_thread::get_id());
    });

    std::thread worker([&]() {
        for (int i = 0; i < 50; ++i) d.insert_file("w/f" + std::to_string(i) + ".txt", make_val("v"));
    });
    worker.join();

    d.remove_callback(id);

    // At least one callback must have run on a non-main thread
    bool ran_off_main = false;
    for (auto const& tid : tids)
        if (tid != main_tid) ran_off_main = true;
    EXPECT_TRUE(ran_off_main);
}

// ---------------------------------------------------------------------------
// Root watcher sees events deep in the subtree
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, RootWatcher_SeesDeepSubtreeEvents) {
    Directory d = Directory::create({});

    std::atomic<int> count{0};
    auto id = d.add_callback([&](const DirEvent&) { count.fetch_add(1); });

    std::thread worker([&]() {
        d.create_directory("a/b/c");
        for (int i = 0; i < 30; ++i) d.insert_file("a/b/c/f" + std::to_string(i) + ".txt", make_val("x"));
    });
    worker.join();

    d.remove_callback(id);
    // At minimum the 30 file inserts must have fired; intermediate dir-creates may add more
    EXPECT_GE(count.load(), 30);
}

// ---------------------------------------------------------------------------
// Subdir watcher does NOT see root-level events
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, SubdirWatcher_DoesNotSeeRootOps) {
    Directory d = Directory::create({});
    auto sub_r  = d.create_directory("watchme");
    ASSERT_TRUE(sub_r.has_value());
    Directory sub = sub_r.value();

    std::atomic<int> sub_count{0};
    auto sub_id = sub.add_callback([&](const DirEvent&) { sub_count.fetch_add(1); });

    // Insert files at root level — sub watcher must NOT see these
    for (int i = 0; i < 20; ++i) d.insert_file("root_f" + std::to_string(i) + ".txt", make_val("r"));

    sub.remove_callback(sub_id);
    EXPECT_EQ(sub_count.load(), 0);
}

// ---------------------------------------------------------------------------
// Subdir watcher sees own events
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, SubdirWatcher_SeesOwnEvents) {
    Directory d = Directory::create({});
    auto sub_r  = d.create_directory("watchme");
    ASSERT_TRUE(sub_r.has_value());
    Directory sub = sub_r.value();

    std::atomic<int> sub_count{0};
    auto sub_id = sub.add_callback([&](const DirEvent&) { sub_count.fetch_add(1); });

    for (int i = 0; i < 20; ++i) d.insert_file("watchme/f" + std::to_string(i) + ".txt", make_val("w"));

    sub.remove_callback(sub_id);
    EXPECT_EQ(sub_count.load(), 20);
}

// ---------------------------------------------------------------------------
// Multiple watchers at different levels — counts are correct
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, MultipleWatchersAtDifferentLevels) {
    //  root
    //   └── mid
    //         └── leaf
    //               └── deep.txt  ← insert happens here
    //
    // Callbacks at root, mid, leaf must ALL fire for the leaf insert.
    // Callback at root must NOT double-fire (no duplicates).

    Directory d = Directory::create({});
    auto mid_r  = d.create_directory("mid");
    ASSERT_TRUE(mid_r.has_value());
    Directory mid = mid_r.value();

    auto leaf_r = d.create_directory("mid/leaf");
    ASSERT_TRUE(leaf_r.has_value());
    Directory leaf = leaf_r.value();

    std::atomic<int> root_cnt{0}, mid_cnt{0}, leaf_cnt{0};
    auto root_id = d.add_callback([&](const DirEvent&) { root_cnt.fetch_add(1); });
    auto mid_id  = mid.add_callback([&](const DirEvent&) { mid_cnt.fetch_add(1); });
    auto leaf_id = leaf.add_callback([&](const DirEvent&) { leaf_cnt.fetch_add(1); });

    const int N = 10;
    for (int i = 0; i < N; ++i) d.insert_file("mid/leaf/f" + std::to_string(i) + ".txt", make_val("x"));

    d.remove_callback(root_id);
    mid.remove_callback(mid_id);
    leaf.remove_callback(leaf_id);

    EXPECT_EQ(root_cnt.load(), N);
    EXPECT_EQ(mid_cnt.load(), N);
    EXPECT_EQ(leaf_cnt.load(), N);
}

// ---------------------------------------------------------------------------
// Root fires once per event even if multiple paths share the same subscriber
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, RootWatcher_NoDuplicateFires) {
    // Adding a root watcher must not result in duplicate callbacks when also
    // a subdir watcher using the SAME id exists (they share the same sub ptr).
    Directory d = Directory::create({});
    d.create_directory("sub");

    std::atomic<int> count{0};
    auto id = d.add_callback([&](const DirEvent&) { count.fetch_add(1); });

    d.insert_file("sub/f.txt", make_val("x"));

    d.remove_callback(id);
    EXPECT_EQ(count.load(), 1);
}

// ---------------------------------------------------------------------------
// Watcher added during concurrent activity captures subsequent events
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, WatcherAddedDuringActivity_CapturesSubsequentEvents) {
    Directory d = Directory::create({});

    std::atomic<bool> adding_done{false};
    std::atomic<int> before_add{0};
    std::atomic<int> after_add{0};

    // Phase 1: insert some files before adding watcher
    for (int i = 0; i < 20; ++i) d.insert_file("pre/f" + std::to_string(i) + ".txt", make_val("x"));

    // Now add watcher
    std::atomic<int> count{0};
    auto id = d.add_callback([&](const DirEvent&) { count.fetch_add(1); });

    // Phase 2: insert files after watcher is added
    std::thread worker([&]() {
        for (int i = 0; i < 50; ++i) d.insert_file("post/f" + std::to_string(i) + ".txt", make_val("y"));
    });
    worker.join();

    d.remove_callback(id);
    // All 50 post-add inserts must have fired
    EXPECT_EQ(count.load(), 50);
}

// ---------------------------------------------------------------------------
// Concurrent add/remove callbacks while inserts are happening
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, ConcurrentAddRemoveCallbacks_NoDeadlock) {
    Directory d = Directory::create({});

    std::atomic<bool> stop{false};
    std::atomic<int> op_count{0};

    // Thread 1: constantly inserts files
    std::thread inserter([&]() {
        int i = 0;
        while (!stop.load()) {
            d.insert_file("files/f" + std::to_string(i++) + ".txt", make_val("x"));
            op_count.fetch_add(1);
            if (i > 500) stop.store(true);
        }
    });

    // Thread 2: constantly adds and removes callbacks
    std::thread watcher([&]() {
        while (!stop.load()) {
            auto id = d.add_callback([](const DirEvent&) {});
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            d.remove_callback(id);
        }
    });

    inserter.join();
    watcher.join();

    // If we get here without deadlock or crash, the test passes
    EXPECT_GT(op_count.load(), 0);
}

// ---------------------------------------------------------------------------
// All 6 event types triggered from multiple threads concurrently
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, AllEventTypes_ConcurrentMultiThread) {
    Directory d = Directory::create({});

    std::mutex m;
    std::vector<DirEvent> events;
    std::vector<std::thread::id> cb_tids;
    std::thread::id main_tid = std::this_thread::get_id();

    auto cb_id = d.add_callback([&](const DirEvent& ev) {
        std::lock_guard lk(m);
        events.push_back(ev);
        cb_tids.push_back(std::this_thread::get_id());
    });

    const int threads           = 4;
    const int events_per_thread = 6;  // DirAdded, FileAdded, FileModified, FileAdded(m), Moved, FileRemoved,
                                      // FileRemoved(a), DirRemoved = 8; we count 6 operations
    const int expected_min_events = threads * events_per_thread;

    std::atomic<bool> started{false};
    std::vector<std::thread> workers;

    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([t, &d, &started]() {
            while (!started.load()) std::this_thread::yield();
            std::string dir = "mt" + std::to_string(t);
            d.create_directory(dir);                       // DirAdded
            d.insert_file(dir + "/a.txt", make_val("a"));  // FileAdded
            d.insert_file(dir + "/a.txt", make_val("b"));  // FileModified
            d.insert_file(dir + "/m.txt", make_val("m"));  // FileAdded
            d.move(dir + "/m.txt", dir + "/n.txt");        // Moved
            d.remove_file(dir + "/n.txt");                 // FileRemoved
            d.remove_file(dir + "/a.txt");
            d.remove_directory(dir);  // DirRemoved
        });
    }

    started.store(true);
    for (auto& w : workers) w.join();

    d.remove_callback(cb_id);

    {
        std::lock_guard lk(m);
        // 4 threads × 8 events (DirAdded, FileAdded, FileModified, FileAdded, Moved, FileRemoved×2, DirRemoved)
        EXPECT_GE(static_cast<int>(events.size()), expected_min_events);

        // At least one callback must have run on a non-main thread
        bool ran_off_main = false;
        for (auto& tid : cb_tids)
            if (tid != main_tid) ran_off_main = true;
        EXPECT_TRUE(ran_off_main);

        // Verify each event type appears at least once
        bool has_dir_added = false, has_file_added = false, has_file_modified = false;
        bool has_moved = false, has_file_removed = false, has_dir_removed = false;
        for (auto& ev : events) {
            switch (ev.type) {
                case DirEventType::DirAdded:
                    has_dir_added = true;
                    break;
                case DirEventType::FileAdded:
                    has_file_added = true;
                    break;
                case DirEventType::FileModified:
                    has_file_modified = true;
                    break;
                case DirEventType::Moved:
                    has_moved = true;
                    break;
                case DirEventType::FileRemoved:
                    has_file_removed = true;
                    break;
                case DirEventType::DirRemoved:
                    has_dir_removed = true;
                    break;
            }
        }
        EXPECT_TRUE(has_dir_added);
        EXPECT_TRUE(has_file_added);
        EXPECT_TRUE(has_file_modified);
        EXPECT_TRUE(has_moved);
        EXPECT_TRUE(has_file_removed);
        EXPECT_TRUE(has_dir_removed);
    }
}

// ---------------------------------------------------------------------------
// Callback at multiple levels — concurrent inserts
// — root watcher fires N times, mid watcher fires N times, leaf watcher fires N times
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, MultiLevelWatchers_ConcurrentInserts) {
    Directory d = Directory::create({});

    auto mid_r = d.create_directory("mid");
    ASSERT_TRUE(mid_r.has_value());
    Directory mid = mid_r.value();

    auto leaf_r = d.create_directory("mid/leaf");
    ASSERT_TRUE(leaf_r.has_value());
    Directory leaf = leaf_r.value();

    std::atomic<int> root_cnt{0}, mid_cnt{0}, leaf_cnt{0};
    auto root_id = d.add_callback([&](const DirEvent&) { root_cnt.fetch_add(1); });
    auto mid_id  = mid.add_callback([&](const DirEvent&) { mid_cnt.fetch_add(1); });
    auto leaf_id = leaf.add_callback([&](const DirEvent&) { leaf_cnt.fetch_add(1); });

    const int threads    = 4;
    const int per_thread = 25;
    std::atomic<bool> go{false};
    std::vector<std::thread> workers;

    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([t, per_thread, &d, &go]() {
            while (!go.load()) std::this_thread::yield();
            for (int i = 0; i < per_thread; ++i)
                d.insert_file("mid/leaf/t" + std::to_string(t) + "_f" + std::to_string(i) + ".txt", make_val("x"));
        });
    }

    go.store(true);
    for (auto& w : workers) w.join();

    d.remove_callback(root_id);
    mid.remove_callback(mid_id);
    leaf.remove_callback(leaf_id);

    const int total = threads * per_thread;
    EXPECT_EQ(root_cnt.load(), total);
    EXPECT_EQ(mid_cnt.load(), total);
    EXPECT_EQ(leaf_cnt.load(), total);
}

// ---------------------------------------------------------------------------
// Root watcher added AFTER subdirs exist — propagated to existing subdirs
// — concurrent inserts into pre-existing subdir
// ---------------------------------------------------------------------------

TEST(MemoryVfsWatcher, WatcherAddedAfterSubdir_PropagatedToExistingSubdir_Concurrent) {
    Directory d = Directory::create({});
    d.create_directory("pre");

    // Add root watcher AFTER "pre" already exists
    std::atomic<int> count{0};
    auto id = d.add_callback([&](const DirEvent&) { count.fetch_add(1); });

    const int N = 50;
    std::thread worker([&]() {
        for (int i = 0; i < N; ++i) d.insert_file("pre/f" + std::to_string(i) + ".txt", make_val("x"));
    });
    worker.join();

    d.remove_callback(id);
    EXPECT_EQ(count.load(), N);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
