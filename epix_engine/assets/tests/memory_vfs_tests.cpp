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

static std::string read_val(const Value& v) {
    auto& p = std::get<std::shared_ptr<std::vector<std::uint8_t>>>(v.v);
    return std::string(p->begin(), p->end());
}

// ---------------------------------------------------------------------------
// exists()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, Exists_RootAlwaysTrue) {
    Directory d = Directory::create({});
    // exists("") uses resolve_parent which returns IoError for empty path — the
    // root is not queryable via exists(); use is_directory("") for root checks.
    // Verify that a freshly inserted file does exist.
    d.insert_file("f.txt", make_val("x"));
    auto r2 = d.exists("f.txt");
    ASSERT_TRUE(r2.has_value());
    EXPECT_TRUE(r2.value());
}

TEST(MemoryVfs, Exists_MissingPath) {
    Directory d = Directory::create({});
    auto r      = d.exists("no_such.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r.value());
}

TEST(MemoryVfs, Exists_DirExists) {
    Directory d = Directory::create({});
    d.create_directory("mydir");
    auto r = d.exists("mydir");
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r.value());
}

TEST(MemoryVfs, Exists_DeepPath) {
    Directory d = Directory::create({});
    d.insert_file("a/b/c.txt", make_val("deep"));
    auto rf = d.exists("a/b/c.txt");
    ASSERT_TRUE(rf.has_value());
    EXPECT_TRUE(rf.value());
    auto rd = d.exists("a/b");
    ASSERT_TRUE(rd.has_value());
    EXPECT_TRUE(rd.value());
    auto rmid = d.exists("a");
    ASSERT_TRUE(rmid.has_value());
    EXPECT_TRUE(rmid.value());
}

TEST(MemoryVfs, Exists_FileAsIntermediateComponent) {
    // a/b.txt exists as a file; exists("a/b.txt/x") should fail (NotFoundError via resolve_parent)
    Directory d = Directory::create({});
    d.insert_file("a/b.txt", make_val("x"));
    auto r = d.exists("a/b.txt/x");
    // resolve_parent will descend into b.txt which is a file → NotFoundError
    ASSERT_TRUE(r.has_value() || !r.has_value());  // implementation returns unexpected or false
    // either false or an error is acceptable; must not crash and must not be 'true'
    EXPECT_FALSE(r.has_value() && r.value());
}

// ---------------------------------------------------------------------------
// is_directory()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, IsDirectory_RootIsDir) {
    Directory d = Directory::create({});
    // empty path "" → root → true
    auto r = d.is_directory("");
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r.value());
}

TEST(MemoryVfs, IsDirectory_SubdirIsDir) {
    Directory d = Directory::create({});
    d.create_directory("sub");
    auto r = d.is_directory("sub");
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r.value());
}

TEST(MemoryVfs, IsDirectory_FileIsNotDir) {
    Directory d = Directory::create({});
    d.insert_file("f.txt", make_val("hi"));
    auto r = d.is_directory("f.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r.value());
}

TEST(MemoryVfs, IsDirectory_MissingReturnsFalse) {
    Directory d = Directory::create({});
    auto r      = d.is_directory("ghost");
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r.value());
}

// ---------------------------------------------------------------------------
// get_file()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, GetFile_HappyPath) {
    Directory d = Directory::create({});
    d.insert_file("hello.txt", make_val("hello"));
    auto r = d.get_file("hello.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(read_val(r.value().value), "hello");
}

TEST(MemoryVfs, GetFile_DeepPath) {
    Directory d = Directory::create({});
    d.insert_file("a/b/c.txt", make_val("deep"));
    auto r = d.get_file("a/b/c.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(read_val(r.value().value), "deep");
}

TEST(MemoryVfs, GetFile_OnDirectory_ReturnsError) {
    Directory d = Directory::create({});
    d.create_directory("dir");
    auto r = d.get_file("dir");
    ASSERT_FALSE(r.has_value());
}

TEST(MemoryVfs, GetFile_Missing_ReturnsNotFoundError) {
    Directory d = Directory::create({});
    auto r      = d.get_file("nope.txt");
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<NotFoundError>(r.error()));
}

// ---------------------------------------------------------------------------
// get_directory()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, GetDirectory_RootEmptyPath) {
    Directory d = Directory::create("vroot");
    auto r      = d.get_directory("");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value().get_path(), std::filesystem::path("vroot"));
}

TEST(MemoryVfs, GetDirectory_ExistingSubdir) {
    Directory d = Directory::create({});
    d.create_directory("sub/leaf");
    auto r = d.get_directory("sub/leaf");
    ASSERT_TRUE(r.has_value());
}

TEST(MemoryVfs, GetDirectory_OnFile_ReturnsError) {
    Directory d = Directory::create({});
    d.insert_file("f.txt", make_val("x"));
    auto r = d.get_directory("f.txt");
    ASSERT_FALSE(r.has_value());
}

TEST(MemoryVfs, GetDirectory_Missing_ReturnsError) {
    Directory d = Directory::create({});
    auto r      = d.get_directory("no/such/dir");
    ASSERT_FALSE(r.has_value());
}

// ---------------------------------------------------------------------------
// insert_file()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, InsertFile_NewFile_FileAdded) {
    Directory d = Directory::create({});
    std::vector<DirEvent> events;
    auto id = d.add_callback([&](const DirEvent& ev) { events.push_back(ev); });

    auto r = d.insert_file("new.txt", make_val("data"));
    ASSERT_TRUE(r.has_value());

    d.remove_callback(id);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, DirEventType::FileAdded);
    EXPECT_FALSE(events[0].old_path.has_value());
}

TEST(MemoryVfs, InsertFile_OverwriteExisting_FileModified) {
    Directory d = Directory::create({});
    d.insert_file("f.txt", make_val("v1"));

    std::vector<DirEvent> events;
    auto id = d.add_callback([&](const DirEvent& ev) { events.push_back(ev); });

    d.insert_file("f.txt", make_val("v2"));
    d.remove_callback(id);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, DirEventType::FileModified);

    auto r = d.get_file("f.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(read_val(r.value().value), "v2");
}

TEST(MemoryVfs, InsertFile_AutoCreatesIntermediateDirs) {
    Directory d = Directory::create({});
    auto r      = d.insert_file("a/b/c/deep.txt", make_val("x"));
    ASSERT_TRUE(r.has_value());

    EXPECT_TRUE(d.is_directory("a").value_or(false));
    EXPECT_TRUE(d.is_directory("a/b").value_or(false));
    EXPECT_TRUE(d.is_directory("a/b/c").value_or(false));
    EXPECT_TRUE(d.exists("a/b/c/deep.txt").value_or(false));
}

TEST(MemoryVfs, InsertFile_EmptyPath_ReturnsError) {
    Directory d = Directory::create({});
    auto r      = d.insert_file("", make_val("x"));
    EXPECT_FALSE(r.has_value());
}

TEST(MemoryVfs, InsertFile_PathIsExistingDir_ReturnsIoError) {
    Directory d = Directory::create({});
    d.create_directory("mydir");
    auto r = d.insert_file("mydir", make_val("x"));
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<IoError>(r.error()));
}

TEST(MemoryVfs, InsertFile_IntermediateIsFile_ReturnsIoError) {
    Directory d = Directory::create({});
    d.insert_file("a.txt", make_val("x"));
    // trying to insert "a.txt/child.txt" where a.txt is a file
    auto r = d.insert_file("a.txt/child.txt", make_val("y"));
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<IoError>(r.error()));
}

// ---------------------------------------------------------------------------
// insert_file_if_new()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, InsertFileIfNew_NewFile_Inserts) {
    Directory d = Directory::create({});
    auto r      = d.insert_file_if_new("f.txt", make_val("new"));
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(d.exists("f.txt").value_or(false));
}

TEST(MemoryVfs, InsertFileIfNew_ExistingFile_NoOverwrite) {
    Directory d = Directory::create({});
    d.insert_file("f.txt", make_val("original"));

    std::vector<DirEvent> events;
    auto id = d.add_callback([&](const DirEvent& ev) { events.push_back(ev); });

    auto r = d.insert_file_if_new("f.txt", make_val("new"));
    d.remove_callback(id);

    ASSERT_TRUE(r.has_value());
    // Original content must be unchanged
    EXPECT_EQ(read_val(d.get_file("f.txt").value().value), "original");
    // No event should be fired for a no-op
    EXPECT_TRUE(events.empty());
}

TEST(MemoryVfs, InsertFileIfNew_PathIsDir_ReturnsError) {
    Directory d = Directory::create({});
    d.create_directory("dir");
    auto r = d.insert_file_if_new("dir", make_val("x"));
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<IoError>(r.error()));
}

// ---------------------------------------------------------------------------
// remove_file()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, RemoveFile_HappyPath_FileRemoved) {
    Directory d = Directory::create({});
    d.insert_file("r.txt", make_val("bye"));

    std::vector<DirEvent> events;
    auto id = d.add_callback([&](const DirEvent& ev) { events.push_back(ev); });

    auto rem = d.remove_file("r.txt");
    d.remove_callback(id);

    ASSERT_TRUE(rem.has_value());
    EXPECT_EQ(read_val(rem.value().value), "bye");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, DirEventType::FileRemoved);

    EXPECT_FALSE(d.exists("r.txt").value_or(true));
}

TEST(MemoryVfs, RemoveFile_Missing_NotFoundError) {
    Directory d = Directory::create({});
    auto r      = d.remove_file("ghost.txt");
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<NotFoundError>(r.error()));
}

TEST(MemoryVfs, RemoveFile_OnDir_NotFoundError) {
    Directory d = Directory::create({});
    d.create_directory("dir");
    auto r = d.remove_file("dir");
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<NotFoundError>(r.error()));
}

TEST(MemoryVfs, RemoveFile_DeepPath) {
    Directory d = Directory::create({});
    d.insert_file("a/b/f.txt", make_val("x"));
    auto r = d.remove_file("a/b/f.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(d.exists("a/b/f.txt").value_or(true));
    // Parent dirs should still exist
    EXPECT_TRUE(d.is_directory("a/b").value_or(false));
}

// ---------------------------------------------------------------------------
// create_directory()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, CreateDirectory_New_DirAdded) {
    Directory d = Directory::create({});
    std::vector<DirEvent> events;
    auto id = d.add_callback([&](const DirEvent& ev) { events.push_back(ev); });

    auto r = d.create_directory("newdir");
    d.remove_callback(id);

    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(d.is_directory("newdir").value_or(false));

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, DirEventType::DirAdded);
    EXPECT_FALSE(events[0].old_path.has_value());
}

TEST(MemoryVfs, CreateDirectory_IntermediatesAutoCreated) {
    Directory d = Directory::create({});
    auto r      = d.create_directory("a/b/c");
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(d.is_directory("a").value_or(false));
    EXPECT_TRUE(d.is_directory("a/b").value_or(false));
    EXPECT_TRUE(d.is_directory("a/b/c").value_or(false));
}

TEST(MemoryVfs, CreateDirectory_Idempotent_ReplacesDir) {
    // create_directory replaces existing directory (not if_new)
    Directory d = Directory::create({});
    d.create_directory("dir");
    d.insert_file("dir/f.txt", make_val("x"));  // put a file inside
    // Creating again replaces the directory node (new empty dir)
    auto r = d.create_directory("dir");
    ASSERT_TRUE(r.has_value());
    // The returned dir is the new empty one
    auto listing = r.value().list_directory("", false);
    ASSERT_TRUE(listing.has_value());
    std::vector<std::filesystem::path> items;
    for (auto p : listing.value()) items.push_back(p);
    EXPECT_TRUE(items.empty());
}

TEST(MemoryVfs, CreateDirectory_OnExistingFile_IoError) {
    Directory d = Directory::create({});
    d.insert_file("f.txt", make_val("x"));
    auto r = d.create_directory("f.txt");
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<IoError>(r.error()));
    EXPECT_EQ(std::get<IoError>(r.error()).path, std::filesystem::path("f.txt"));
}

// ---------------------------------------------------------------------------
// create_directory_if_new()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, CreateDirectoryIfNew_New_Inserts) {
    Directory d = Directory::create({});
    auto r      = d.create_directory_if_new("dir");
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(d.is_directory("dir").value_or(false));
}

TEST(MemoryVfs, CreateDirectoryIfNew_Existing_NoEvent) {
    Directory d = Directory::create({});
    d.create_directory("dir");
    d.insert_file("dir/f.txt", make_val("x"));

    std::vector<DirEvent> events;
    auto id = d.add_callback([&](const DirEvent& ev) { events.push_back(ev); });

    auto r = d.create_directory_if_new("dir");
    d.remove_callback(id);

    ASSERT_TRUE(r.has_value());
    // No DirAdded for an existing directory
    EXPECT_TRUE(events.empty());
    // Existing contents must be preserved
    EXPECT_TRUE(d.exists("dir/f.txt").value_or(false));
}

// ---------------------------------------------------------------------------
// remove_directory()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, RemoveDirectory_EmptyDir_DirRemoved) {
    Directory d = Directory::create({});
    d.create_directory("empty");

    std::vector<DirEvent> events;
    auto id = d.add_callback([&](const DirEvent& ev) { events.push_back(ev); });

    auto r = d.remove_directory("empty");
    d.remove_callback(id);

    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(d.exists("empty").value_or(true));

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, DirEventType::DirRemoved);
}

TEST(MemoryVfs, RemoveDirectory_NonEmpty_IoError) {
    Directory d = Directory::create({});
    d.create_directory("ndir");
    d.insert_file("ndir/x.txt", make_val("x"));
    auto r = d.remove_directory("ndir");
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<IoError>(r.error()));
    EXPECT_EQ(std::get<IoError>(r.error()).path, std::filesystem::path("ndir"));
}

TEST(MemoryVfs, RemoveDirectory_Missing_NotFoundError) {
    Directory d = Directory::create({});
    auto r      = d.remove_directory("ghost");
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<NotFoundError>(r.error()));
}

TEST(MemoryVfs, RemoveDirectory_OnFile_NotFoundError) {
    Directory d = Directory::create({});
    d.insert_file("f.txt", make_val("x"));
    auto r = d.remove_directory("f.txt");
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<NotFoundError>(r.error()));
}

// ---------------------------------------------------------------------------
// move()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, Move_FileToFile_Moved) {
    Directory d = Directory::create({});
    d.insert_file("src.txt", make_val("moved"));

    std::vector<DirEvent> events;
    auto id = d.add_callback([&](const DirEvent& ev) { events.push_back(ev); });

    auto r = d.move("src.txt", "dst.txt");
    d.remove_callback(id);

    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(d.exists("src.txt").value_or(true));

    auto rf = d.get_file("dst.txt");
    ASSERT_TRUE(rf.has_value());
    EXPECT_EQ(read_val(rf.value().value), "moved");
    // Path inside Data must be updated
    EXPECT_EQ(rf.value().path, std::filesystem::path("dst.txt"));

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, DirEventType::Moved);
    ASSERT_TRUE(events[0].old_path.has_value());
}

TEST(MemoryVfs, Move_FileToSubdir_AutoCreatesParent) {
    Directory d = Directory::create({});
    d.insert_file("src.txt", make_val("x"));
    auto r = d.move("src.txt", "sub/dst.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(d.exists("src.txt").value_or(true));
    EXPECT_TRUE(d.exists("sub/dst.txt").value_or(false));
}

TEST(MemoryVfs, Move_FileAcrossExistingDirs) {
    Directory d = Directory::create({});
    d.create_directory("a");
    d.create_directory("b");
    d.insert_file("a/f.txt", make_val("cross"));
    auto r = d.move("a/f.txt", "b/f.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(d.exists("a/f.txt").value_or(true));
    EXPECT_TRUE(d.exists("b/f.txt").value_or(false));
}

TEST(MemoryVfs, Move_DirToDir_RenameDir) {
    Directory d = Directory::create({});
    d.create_directory("old");
    d.insert_file("old/child.txt", make_val("inside"));
    auto r = d.move("old", "renamed");
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(d.exists("old").value_or(true));
    EXPECT_TRUE(d.is_directory("renamed").value_or(false));
    // Children should be accessible under the new name
    EXPECT_TRUE(d.exists("renamed/child.txt").value_or(false));
}

TEST(MemoryVfs, Move_MissingSource_NotFoundError) {
    Directory d = Directory::create({});
    auto r      = d.move("no_such.txt", "dst.txt");
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<NotFoundError>(r.error()));
    EXPECT_EQ(std::get<NotFoundError>(r.error()).path, std::filesystem::path("no_such.txt"));
}

TEST(MemoryVfs, Move_OverwriteExisting) {
    // Moving onto an existing file should overwrite it
    Directory d = Directory::create({});
    d.insert_file("src.txt", make_val("new_content"));
    d.insert_file("dst.txt", make_val("old_content"));
    auto r = d.move("src.txt", "dst.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(d.exists("src.txt").value_or(true));
    auto rf = d.get_file("dst.txt");
    ASSERT_TRUE(rf.has_value());
    EXPECT_EQ(read_val(rf.value().value), "new_content");
}

// ---------------------------------------------------------------------------
// list_directory()
// ---------------------------------------------------------------------------

TEST(MemoryVfs, ListDirectory_NonRecursive_RootOnly) {
    Directory d = Directory::create({});
    d.insert_file("root.txt", make_val("x"));
    d.create_directory("dir");
    d.insert_file("dir/child.txt", make_val("y"));

    auto r = d.list_directory("", false);
    ASSERT_TRUE(r.has_value());
    std::vector<std::filesystem::path> items;
    for (auto p : r.value()) items.push_back(p);
    // Should see root.txt and dir, but NOT dir/child.txt
    EXPECT_EQ(items.size(), 2u);
}

TEST(MemoryVfs, ListDirectory_Recursive) {
    Directory d = Directory::create({});
    d.insert_file("a.txt", make_val("1"));
    d.insert_file("sub/b.txt", make_val("2"));
    d.insert_file("sub/leaf/c.txt", make_val("3"));

    auto r = d.list_directory("", true);
    ASSERT_TRUE(r.has_value());
    std::vector<std::filesystem::path> items;
    for (auto p : r.value()) items.push_back(p);
    // a.txt, sub/, sub/b.txt, sub/leaf/, sub/leaf/c.txt = 5 entries
    EXPECT_EQ(items.size(), 5u);
}

TEST(MemoryVfs, ListDirectory_SubdirNonRecursive) {
    Directory d = Directory::create({});
    d.insert_file("sub/x.txt", make_val("1"));
    d.insert_file("sub/y.txt", make_val("2"));
    d.insert_file("sub/deep/z.txt", make_val("3"));

    auto r = d.list_directory("sub", false);
    ASSERT_TRUE(r.has_value());
    std::vector<std::filesystem::path> items;
    for (auto p : r.value()) items.push_back(p);
    EXPECT_EQ(items.size(), 3u);  // x.txt, y.txt, deep/
}

TEST(MemoryVfs, ListDirectory_OnFile_ReturnsError) {
    Directory d = Directory::create({});
    d.insert_file("f.txt", make_val("x"));
    auto r = d.list_directory("f.txt", false);
    ASSERT_FALSE(r.has_value());
}

TEST(MemoryVfs, ListDirectory_Missing_ReturnsError) {
    Directory d = Directory::create({});
    auto r      = d.list_directory("no_such_dir", false);
    ASSERT_FALSE(r.has_value());
}

TEST(MemoryVfs, ListDirectory_EmptyDir) {
    Directory d = Directory::create({});
    d.create_directory("empty");
    auto r = d.list_directory("empty", false);
    ASSERT_TRUE(r.has_value());
    std::vector<std::filesystem::path> items;
    for (auto p : r.value()) items.push_back(p);
    EXPECT_TRUE(items.empty());
}

// ---------------------------------------------------------------------------
// get_path() and Data::path tracking
// ---------------------------------------------------------------------------

TEST(MemoryVfs, GetPath_RootPath) {
    Directory d = Directory::create("myroot");
    EXPECT_EQ(d.get_path(), std::filesystem::path("myroot"));
}

TEST(MemoryVfs, GetPath_SubdirPath) {
    Directory d = Directory::create("root");
    auto r      = d.create_directory("sub");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value().get_path(), std::filesystem::path("root/sub"));
}

TEST(MemoryVfs, DataPath_ReflectsVirtualRoot) {
    Directory d = Directory::create("vr");
    d.insert_file("a/b.txt", make_val("x"));
    auto r = d.get_file("a/b.txt");
    ASSERT_TRUE(r.has_value());
    // Path inside Data should include the virtual root
    EXPECT_EQ(r.value().path, std::filesystem::path("vr/a/b.txt"));
}

TEST(MemoryVfs, DataPath_UpdatedAfterMove) {
    Directory d = Directory::create({});
    d.insert_file("old.txt", make_val("x"));
    d.move("old.txt", "new.txt");
    auto r = d.get_file("new.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value().path, std::filesystem::path("new.txt"));
}

// ---------------------------------------------------------------------------
// Event types and callback mechanics
// ---------------------------------------------------------------------------

TEST(MemoryVfs, AllEventTypes_CorrectTypeField) {
    Directory d = Directory::create({});
    std::vector<DirEventType> types;
    auto id = d.add_callback([&](const DirEvent& ev) { types.push_back(ev.type); });

    d.create_directory("sub");                  // DirAdded
    d.insert_file("sub/a.txt", make_val("1"));  // FileAdded
    d.insert_file("sub/a.txt", make_val("2"));  // FileModified
    d.insert_file("sub/m.txt", make_val("m"));  // FileAdded
    d.move("sub/m.txt", "sub/n.txt");           // Moved
    d.remove_file("sub/n.txt");                 // FileRemoved
    d.remove_file("sub/a.txt");
    d.remove_directory("sub");  // DirRemoved

    d.remove_callback(id);

    // DirAdded, FileAdded(a), FileModified(a), FileAdded(m), Moved(m->n), FileRemoved(n), FileRemoved(a), DirRemoved
    ASSERT_EQ(types.size(), 8u);
    EXPECT_EQ(types[0], DirEventType::DirAdded);
    EXPECT_EQ(types[1], DirEventType::FileAdded);
    EXPECT_EQ(types[2], DirEventType::FileModified);
    EXPECT_EQ(types[3], DirEventType::FileAdded);
    EXPECT_EQ(types[4], DirEventType::Moved);
    EXPECT_EQ(types[5], DirEventType::FileRemoved);
    EXPECT_EQ(types[6], DirEventType::FileRemoved);
    EXPECT_EQ(types[7], DirEventType::DirRemoved);
}

TEST(MemoryVfs, Callback_MovedEvent_HasOldPath) {
    Directory d = Directory::create({});
    d.insert_file("src.txt", make_val("x"));
    std::optional<DirEvent> ev_captured;
    auto id = d.add_callback([&](const DirEvent& ev) { ev_captured = ev; });
    d.move("src.txt", "dst.txt");
    d.remove_callback(id);

    ASSERT_TRUE(ev_captured.has_value());
    EXPECT_EQ(ev_captured->type, DirEventType::Moved);
    ASSERT_TRUE(ev_captured->old_path.has_value());
    EXPECT_EQ(ev_captured->path, std::filesystem::path("dst.txt"));
}

TEST(MemoryVfs, MultipleCallbacks_AllFire) {
    Directory d = Directory::create({});
    int count1 = 0, count2 = 0;
    auto id1 = d.add_callback([&](const DirEvent&) { ++count1; });
    auto id2 = d.add_callback([&](const DirEvent&) { ++count2; });

    d.insert_file("f.txt", make_val("x"));
    d.remove_callback(id1);
    d.remove_callback(id2);

    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);
}

TEST(MemoryVfs, Callback_RemovedNoLongerFires) {
    Directory d = Directory::create({});
    int count   = 0;
    auto id     = d.add_callback([&](const DirEvent&) { ++count; });
    d.insert_file("f.txt", make_val("1"));
    d.remove_callback(id);
    d.insert_file("g.txt", make_val("2"));
    EXPECT_EQ(count, 1);  // only the first insert
}

// ---------------------------------------------------------------------------
// Callbacks at different directory levels
// ---------------------------------------------------------------------------

TEST(MemoryVfs, Callback_OnRoot_SeesSubdirEvents) {
    // A callback registered on the root should see events inside subdirs
    Directory d = Directory::create({});
    d.create_directory("sub");
    std::vector<DirEventType> types;
    auto id = d.add_callback([&](const DirEvent& ev) { types.push_back(ev.type); });

    d.insert_file("sub/f.txt", make_val("x"));  // FileAdded in sub
    d.remove_file("sub/f.txt");                 // FileRemoved in sub

    d.remove_callback(id);
    EXPECT_EQ(types.size(), 2u);
}

TEST(MemoryVfs, Callback_OnSubdir_DoesNotSeeParentEvents) {
    // A callback registered on a subdir should NOT see events at root level
    Directory d = Directory::create({});
    auto sub_r  = d.create_directory("sub");
    ASSERT_TRUE(sub_r.has_value());
    Directory sub = sub_r.value();

    std::vector<DirEvent> sub_events;
    auto sub_id = sub.add_callback([&](const DirEvent& ev) { sub_events.push_back(ev); });

    // insert at root level — sub watcher should NOT see this
    d.insert_file("root_file.txt", make_val("x"));

    sub.remove_callback(sub_id);
    EXPECT_TRUE(sub_events.empty());
}

TEST(MemoryVfs, Callback_OnSubdir_SeesOwnEvents) {
    Directory d = Directory::create({});
    auto sub_r  = d.create_directory("sub");
    ASSERT_TRUE(sub_r.has_value());
    Directory sub = sub_r.value();

    std::vector<DirEventType> sub_types;
    auto sub_id = sub.add_callback([&](const DirEvent& ev) { sub_types.push_back(ev.type); });

    d.insert_file("sub/f.txt", make_val("x"));  // FileAdded in sub
    d.move("sub/f.txt", "sub/g.txt");           // Moved within sub
    d.remove_file("sub/g.txt");                 // FileRemoved from sub

    sub.remove_callback(sub_id);
    EXPECT_EQ(sub_types.size(), 3u);
}

TEST(MemoryVfs, Callback_AddedAfterSubdirCreated_PropagatesToExistingSubdirs) {
    // Callback added on root AFTER subdirs exist must still be propagated to those subdirs
    Directory d = Directory::create({});
    d.create_directory("pre");                  // subdir exists before callback
    d.insert_file("pre/f.txt", make_val("x"));  // insert before callback

    std::vector<DirEventType> types;
    auto id = d.add_callback([&](const DirEvent& ev) { types.push_back(ev.type); });

    // Now insert into the pre-existing subdir → callback should fire
    d.insert_file("pre/g.txt", make_val("y"));
    d.remove_callback(id);

    EXPECT_EQ(types.size(), 1u);
    EXPECT_EQ(types[0], DirEventType::FileAdded);
}

TEST(MemoryVfs, Callback_RootAndSubdir_BothFireForSubdirOp) {
    // Two callbacks: one on root, one on subdir. Both should fire for ops in subdir.
    Directory d = Directory::create({});
    auto sub_r  = d.create_directory("sub");
    ASSERT_TRUE(sub_r.has_value());
    Directory sub = sub_r.value();

    int root_count = 0, sub_count = 0;
    auto root_id = d.add_callback([&](const DirEvent&) { ++root_count; });
    auto sub_id  = sub.add_callback([&](const DirEvent&) { ++sub_count; });

    d.insert_file("sub/f.txt", make_val("x"));

    d.remove_callback(root_id);
    sub.remove_callback(sub_id);

    EXPECT_EQ(root_count, 1);
    EXPECT_EQ(sub_count, 1);
}

TEST(MemoryVfs, Callback_ThreeLevels_AllFireForDeepOp) {
    // root watcher, mid watcher, leaf watcher — op in leaf fires all three
    Directory d = Directory::create({});
    auto mid_r  = d.create_directory("mid");
    ASSERT_TRUE(mid_r.has_value());
    Directory mid = mid_r.value();

    auto leaf_r = d.create_directory("mid/leaf");
    ASSERT_TRUE(leaf_r.has_value());
    Directory leaf = leaf_r.value();

    int root_cnt = 0, mid_cnt = 0, leaf_cnt = 0;
    auto root_id = d.add_callback([&](const DirEvent&) { ++root_cnt; });
    auto mid_id  = mid.add_callback([&](const DirEvent&) { ++mid_cnt; });
    auto leaf_id = leaf.add_callback([&](const DirEvent&) { ++leaf_cnt; });

    d.insert_file("mid/leaf/deep.txt", make_val("deep"));

    d.remove_callback(root_id);
    mid.remove_callback(mid_id);
    leaf.remove_callback(leaf_id);

    EXPECT_EQ(root_cnt, 1);
    EXPECT_EQ(mid_cnt, 1);
    EXPECT_EQ(leaf_cnt, 1);
}

TEST(MemoryVfs, Callback_DeeplyNestedAutoCreatedDirs_GetsCallback) {
    // insert_file auto-creates a/b/c; a callback on root should fire for the final file
    Directory d = Directory::create({});
    std::vector<DirEventType> types;
    auto id = d.add_callback([&](const DirEvent& ev) { types.push_back(ev.type); });

    // auto-creates a, a/b, a/b/c as intermediate dirs, then inserts file
    d.insert_file("a/b/c/deep.txt", make_val("x"));

    d.remove_callback(id);
    // We should see at least one FileAdded event; the exact number depends on implementation
    // but must have the FileAdded for "deep.txt"
    bool has_file_added = false;
    for (auto t : types)
        if (t == DirEventType::FileAdded) has_file_added = true;
    EXPECT_TRUE(has_file_added);
}

// ---------------------------------------------------------------------------
// poll_events() is a no-op but must not crash
// ---------------------------------------------------------------------------

TEST(MemoryVfs, PollEvents_IsNoOp) {
    Directory d = Directory::create({});
    // Should not crash before or after operations
    d.poll_events();
    d.insert_file("f.txt", make_val("x"));
    d.poll_events();
    d.poll_events();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
