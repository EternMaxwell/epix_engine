# Parallel Slice

Parallel map over contiguous data using a `TaskPool`.

## Overview

Four free functions split a `std::span` into chunks and submit each chunk as a separate `Task` to a `TaskPool`. All tasks are awaited before the function returns, producing a `std::vector<R>` in chunk order.

| Function | Input | Chunking |
|----------|-------|----------|
| `par_chunk_map` | `span<const T>` | Fixed `chunk_size` |
| `par_splat_map` | `span<const T>` | Auto: `slice.size() / max_tasks` |
| `par_chunk_map_mut` | `span<T>` | Fixed `chunk_size` |
| `par_splat_map_mut` | `span<T>` | Auto: `slice.size() / max_tasks` |

The callback signature is `(std::size_t chunk_index, std::span<[const] T> chunk) -> R`.

## Usage

### Fixed chunk size (read-only)

```cpp
import epix.tasks;
using namespace epix::tasks;

TaskPool pool{4};
const std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};

// Sum each chunk of 2
auto results = par_chunk_map<int>(
    std::span<const int>(data), pool, /*chunk_size=*/2,
    [](std::size_t /*index*/, std::span<const int> chunk) {
        int sum = 0;
        for (auto v : chunk) sum += v;
        return sum;
    });
// results == {3, 7, 11, 15}  (order matches chunk order)
```

### Auto chunk size (read-only)

```cpp
// Spread across pool.thread_num() tasks by default
auto results = par_splat_map<int>(
    std::span<const int>(data), pool, /*max_tasks=*/std::nullopt,
    [](std::size_t, std::span<const int> chunk) {
        int sum = 0;
        for (auto v : chunk) sum += v;
        return sum;
    });

// Or cap the task count:
auto r2 = par_splat_map<int>(
    std::span<const int>(data), pool, std::size_t{2},
    [](std::size_t, std::span<const int> chunk) { /* ... */ return 0; });
```

### Mutable slice

```cpp
std::vector<int> data = {1, 2, 3, 4};

auto results = par_chunk_map_mut<int>(
    std::span<int>(data), pool, /*chunk_size=*/2,
    [](std::size_t, std::span<int> chunk) {
        int sum = 0;
        for (auto v : chunk) sum += v;
        return sum;
    });
// results == {3, 7}
```

## Constraints / Gotchas

- Results are in **chunk order** (not completion order) — the blocking collect loop preserves chunk index sequence.
- The callback receives a zero-based `chunk_index` (= `offset / chunk_size`) as the first argument.
- `par_splat_map` with `max_tasks = 0` is clamped to 1 task (the entire slice).
- The data must outlive all spawned tasks (tasks reference spans into the original buffer).
