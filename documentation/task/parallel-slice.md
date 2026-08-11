# Parallel Ranges and Slices

`epix.task` provides eager parallel algorithms over contiguous ranges.

## Range Wrapper

`make_par(range)` creates a non-owning `par_range`; the source must outlive the
operation.

```cpp
std::vector<int> values{1, 2, 3, 4};
TaskPool pool;

make_par(values).for_each(pool, [](int& value) { value *= 2; });
auto strings = make_par(values).transform<std::string>(
    pool, [](int value) { return std::to_string(value); });
auto sum = make_par(values).sum(pool);
```

Other eager operations include `filter`, `reduce`, `fold`, `any`, `all`,
`product`, and `collect<C>()`. Transform and filter preserve source order.

## Slice Functions

```cpp
auto chunk_sums = par_chunk_map<int>(
    pool,
    std::span<const int>{values},
    2,
    [](std::size_t, std::span<const int> chunk) {
        return std::ranges::fold_left(chunk, 0, std::plus{});
    });

auto automatic = par_splat_map<int>(
    pool,
    std::span<const int>{values},
    std::nullopt,
    [](std::size_t, std::span<const int> chunk) { return chunk.size(); });
```

`par_chunk_map` uses a fixed maximum chunk size. `par_splat_map` derives the
chunk size from `pool.thread_num()` and an optional maximum task count. Both
return results in the input chunk order and return an empty vector for empty
input.
