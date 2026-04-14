---
applyTo: "**"
description: "Use when creating any new file, especially large ones (100+ lines). Covers the split create-then-patch workflow to avoid silent truncation failures."
---
# Large File Authoring — Create Empty First, Then Patch

## The Problem

The `create_file` tool silently fails (truncates output) after approximately 200 lines. The agent receives no error and does not know the file is incomplete. **Never write a large file in a single `create_file` call.**

## Mandatory Workflow for Any New File

> This applies to ALL new files — even small ones, to keep a consistent habit.

### Step 1 — Create an empty file

Use `create_file` with only the minimal skeleton (e.g., a module declaration, `#pragma once`, or an empty body):

```
create_file(path, "")          // or a 1–5 line stub
```

### Step 2 — Patch content in small chunks

Use `replace_string_in_file` or `multi_replace_string_in_file` to insert content in blocks of **≤ 80 lines** each.

- Anchor each patch on existing content (the stub or previously inserted block).
- Prefer `multi_replace_string_in_file` to batch independent non-overlapping patches in one call.
- Work top-to-bottom so earlier patches provide anchors for later ones.

### Step 3 — Verify

After all patches are applied, read back a range of the file to confirm the content was written correctly and the file is complete.

---

## Example — Writing a 300-line C++ module

```
// 1. Create stub
create_file("foo.cppm", "export module epix.foo;\n")

// 2. Patch section 1 (lines 1–80)
replace_string_in_file("foo.cppm",
    old = "export module epix.foo;\n",
    new = "export module epix.foo;\n\n// ... first 80 lines ...")

// 3. Patch section 2 (lines 81–160) — anchor on last line of previous patch
replace_string_in_file(...)

// 4. Patch section 3 (lines 161–300)
replace_string_in_file(...)

// 5. Verify
read_file("foo.cppm", startLine=1, endLine=50)
read_file("foo.cppm", startLine=250, endLine=300)
```

---

## Rules Summary

| Rule | Detail |
|------|--------|
| Max lines per `create_file` | **≤ 50 lines** (stub only) |
| Max lines per patch block | **≤ 80 lines** |
| Batch independent patches | Use `multi_replace_string_in_file` |
| After writing | Read back the end of the file to confirm completeness |
| On patch failure | Do NOT retry the same string — re-read the file first to get the actual current content |
