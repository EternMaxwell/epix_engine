# Documentation TODO — Cross-module tracker

This page tracks project-wide directions. Concrete implementation gaps live beside
their modules so that each item can be checked against the current code:

- [ECS](ecs/todo.md)
- [Task](task/todo.md)
- [Assets](assets/todo.md)
- [Time](time/todo.md)
- [Input](input/todo.md)
- [Window](window/todo.md)
- [Shader](shader/todo.md)
- [Render](render/render/todo.md)

## Repository-text integrity: never route file text through a shell

Every file here is UTF-8, while the shell on this machine defaults to code page GBK. Any tool that
decodes or re-encodes text through it corrupts the file: PowerShell script files without a BOM,
`Get-Content`/`Set-Content`, and captured native-command output all mangle non-ASCII characters,
and worse, a UTF-8 sequence followed by an ASCII letter makes the decoder swallow that letter
(`bevy_image` becomes `evy_image`, `from_dynamic` becomes a tab plus `rom_dynamic`,
`block_dimensions` becomes `lock_dimensions`). `documentation/image-bevy-0.18-mismatch-tracker.md`
and `documentation/mesh-core-graph-bevy-0.18-match-status.md` both carry this damage.

Rule: file text is only ever read or written with the file tools (`read`/`edit`/`write`), which are
UTF-8 end to end. Shell commands are for building, running, measuring and searching only.

Repair work outstanding:

- `image-bevy-0.18-mismatch-tracker.md`: the vocabulary damage is repaired (emoji, eaten leading
  letters, swallowed spaces). Re-read the file once end to end to confirm no remaining losses.
- `mesh-core-graph-bevy-0.18-match-status.md`: still corrupted across roughly ninety lines - the
  status emoji (`✅` for the tick, `??` for the block sign, `❌` for the cross, `⚠️` for the
  warning sign), the punctuation (an em dash, `→[` for an arrow plus a swallowed letter, `×` for
  the multiplication sign), and the letters swallowed inside those sequences. Repair each distinct
  corrupted sequence with `edit` (replace-all) against the intended character, restoring swallowed
  letters from context.

## Error-type direction: a `std::exception` base

Every public Epix error type is a value type returned through `std::expected`, which mirrors
Bevy's `Result`. What Bevy also gives every one of them is `std::error::Error` + `Display`,
and the C++ analogue of that polymorphic error base is `std::exception` with `what()`:
Epix already produces the exact `Display` text in its `to_string` overloads. Today the types
do not derive from `std::exception`, and the consequences are visible:

- the generic `epix::utils::to_exception_ptr` (`utils/include/epix/async_task.hpp`) can only
  build an `exception_ptr` from something derived from `std::exception`; for anything else it
  returns `std::runtime_error("sender completed with non-exception error")`, i.e. **the message
  is lost**;
- because of that, modules grew bespoke overloads that format the message into a
  `runtime_error` (`image::to_exception_ptr(const TextureError&)`,
  `image::to_exception_ptr(const ImageLoaderError&)`, `shader::to_exception_ptr(...)`, ...),
  which are redundant next to `std::make_exception_ptr`.

### Agreed design

```cpp
/** @brief The text what() returns: a `to_string` result a `std::string` can be built from. */
template <typename Derived>
concept DisplayableError =
    std::constructible_from<std::string, decltype(to_string(std::declval<const Derived&>()))>;

/** @brief True only for a computed message (a std::string prvalue, e.g. std::format). */
template <DisplayableError Derived>
inline constexpr bool owns_message_v =
    std::same_as<std::remove_cvref_t<decltype(to_string(std::declval<const Derived&>()))>, std::string> &&
    !std::is_reference_v<decltype(to_string(std::declval<const Derived&>()))>;

template <bool Owns> struct ErrorStorage {};                                  // empty base (EBO)
template <> struct ErrorStorage<true> { mutable std::string text; mutable std::once_flag once; };

/** @brief The polymorphic error base Bevy's std::error::Error + Display pair maps to. */
template <DisplayableError Derived>
class Error : public std::exception, private ErrorStorage<owns_message_v<Derived>> {
   public:
    const char* what() const noexcept override;
};
```

- The constraint lives on the **type** (the concept), not inside `what()`, so a missing or
  wrong `to_string` is diagnosed where the error type is declared
  (`struct TextureError : Error<TextureError>, std::variant<...>`).
- `to_string` is found by ADL, so it must be declared in `Derived`'s own namespace or as a
  hidden friend inside `Derived`; the `static_cast<const Derived&>` in `what()` keeps
  `std::to_string` and `std::variant`-taking overloads from winning via the variant base.
- `what()` returns the view directly when `to_string` returns `std::string_view`,
  `const char*`, `char[N]` or a reference (no member, no allocation, no `call_once`); only
  a computed `std::string` prvalue is copied once into the owning storage, guarded by
  `std::call_once` because `what()` is `const` and may be called from several threads.
  Initialisation must be lazy: a base subobject is constructed before `Derived`, so an eager
  base constructor would read an unconstructed object.
- Contract on the no-copy branch: the view must be **NUL-terminated** and must outlive the error
  object (a literal, or a member of `Derived`), because `what()` hands out `data()`.

### Scope and acceptance criteria

- Add `DisplayableError`/`Error`/`ErrorStorage` (`epix/common.hpp` or
  `utils/include/epix/error.hpp`), inherit `Error<Self>` on every public error type in
  `image`, `mesh`, `sprite`, `render`, `assets`, `shader` and `text`, switch the
  `to_string` overloads whose message is a literal to `std::string_view`, and delete the
  per-type `to_exception_ptr` overloads so the generic one is used.
- No message text changes: `what()` must equal the existing `to_string` byte for byte (the
  trackers record those strings, including the parts that mirror Rust's `Debug`).
- Tests: `catch (const std::exception& e)` around a thrown/`make_exception_ptr`-ed error
  returns the `Display` text, a `string_view`-returning error carries no storage, and a
  computed message survives a copy of the error object.
- One commit for review, because it spans parent modules.

### Follow-up (same direction, separate batch)

Where a Rust `panic!`/`unwrap` is currently mirrored as
`std::invalid_argument`/`std::runtime_error`, the module's own typed error can be thrown
instead, so both the type and the message survive.

## Language and standard-library direction

The engine currently targets C++23. Future migrations may use standardized static
reflection and execution facilities once the required compiler and standard-library
support is practical across the supported toolchains.

Static reflection could reduce the boilerplate around ECS traits such as `Bundle`
and component metadata. Standard execution facilities could eventually replace
parts of the task module's Asio-specific implementation and make coroutine
interoperability easier. These are directions rather than committed release
requirements; public APIs should continue to describe what the repository supports
today.

Longer-term language proposals such as compile-time code injection, interface-class
syntax, pattern matching, and uniform function-call syntax may also affect API
design. They should not be treated as current engine features until they are
standardized and supported by the project's toolchains.
