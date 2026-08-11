# Shader implementation reference

This file is a compact implementation-status note. The previous Bevy-derived
snapshot duplicated public documentation and had drifted from the current
shader asset and processing pipeline.

## Current baseline

- `Shader` supports WGSL, SPIR-V, Slang source, and processed Slang module data.
- `ShaderLoader` handles `wgsl`, `spv`, `slang`, and `slang-module` and returns
  `STDEXEC::task<std::expected<Shader, ShaderLoaderError>>`.
- File imports are registered as asset dependencies by `LoadContext`.
- `ShaderProcessor` can preprocess WGSL/Slang and compile Slang source into its
  processed IR representation.
- `ShaderPlugin` registers the asset and loader, synchronizes dependency-ready
  or modified shader assets into an existing `ShaderCache`, and registers
  processing only when `AssetProcessor` is present.
- `ShaderCache` owns dependency/import relationships and invalidates affected
  pipeline IDs when a shader changes or is removed.

The maintained API reference is [`documentation/shader`](../shader/quick.md).
Implementation details live in `epix_engine/shader/include/epix/shader` and
`epix_engine/shader/src`; those files are authoritative when extending parity.
