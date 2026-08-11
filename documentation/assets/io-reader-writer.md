# Asset Reader, Writer, and Watcher

```cpp
import epix.assets;
```

Asset I/O is asynchronous and sender/coroutine based. `Reader` and `Writer` are
byte-stream interfaces; `AssetReader` and `AssetWriter` open streams and manage
paths.

## Byte Streams

```cpp
struct Reader {
    virtual STDEXEC::task<std::expected<std::size_t, std::error_code>>
        read_to_end(std::vector<std::uint8_t>& destination) = 0;
};

struct Writer {
    virtual STDEXEC::task<std::expected<std::size_t, std::error_code>>
        write(std::span<const std::uint8_t> bytes) = 0;
    virtual STDEXEC::task<std::expected<void, std::error_code>> flush() = 0;
};
```

`VecReader` and `VecWriter` provide in-memory implementations.

## AssetReader

```cpp
struct AssetReader {
    virtual STDEXEC::task<std::expected<std::unique_ptr<Reader>, AssetReaderError>>
        read(const std::filesystem::path&) const = 0;
    virtual STDEXEC::task<std::expected<std::unique_ptr<Reader>, AssetReaderError>>
        read_meta(const std::filesystem::path&) const = 0;
    virtual STDEXEC::task<std::expected<utils::input_iterable<std::filesystem::path>,
                                          AssetReaderError>>
        read_directory(const std::filesystem::path&) const = 0;
    virtual STDEXEC::task<std::expected<bool, AssetReaderError>>
        is_directory(const std::filesystem::path&) const = 0;
};
```

`read_meta_bytes()` reads a sidecar into a byte vector. `last_modified()` is an
optional synchronous optimization; readers that cannot report it return
`nullopt`, causing processing to fall back to content hashing.

## AssetWriter

`AssetWriter` asynchronously opens asset/meta `Writer` streams and exposes
`remove`, `rename`, `create_directory`, `remove_directory`, and
`clear_directory`. Convenience methods `write_bytes` and `write_meta_bytes`
open, write, and flush a stream.

Every method returns `STDEXEC::task<std::expected<..., AssetWriterError>>` and
is `const`, allowing sources to share reader/writer objects safely.

## Watchers and Events

`AssetWatcher` is a polymorphic lifetime base. Concrete file and memory watchers
publish `AssetSourceEvent` through the source's async channel. The event variant
covers added, modified, removed, and renamed assets/meta files plus directory
changes and unknown removals.

The filesystem reader uses asynchronous platform paths where available (Asio on
Windows, io_uring on Linux with fallback) and returns the same public `Reader`
interface as memory and embedded sources.
