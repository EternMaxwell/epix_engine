# EPIX ENGINE ASSETS MODULE

This module provides interfaces for managing assets in epix engine and for loading assets from the filesystem.

## Core parts of the module

- **`Assets<T>`**: A resource that manages a collection of assets of type `T`.
- **`Handle<T>`**: A unique identifier for an asset of type `T`.
- **`AssetServer`**: A resource that provides methods for loading assets from the filesystem.
- **`AssetPlugin`**: A plugin that registers the `AssetServer` and `Assets<T>` resources.

## What we want to achieve

### In `Assets<T>`
- **Add**: Add an asset to the collection.
- **Get**: Retrieve an asset by handle.
- **Remove**: Remove an asset from the collection.
- **Modify**: Modify an asset in the collection.

### In `AssetServer`

AssetServer manages loading of all registered asset types. You can register asset loaders for specific type and file extensions. When calling `load` method, it will return a `Handle<T>` of strong reference to the asset regardless of whether the asset is already loaded or not. If it is not loaded, then the server will load it from the source asynchronously. This means that the `load` operation will return immediately, and the asset will be added to the `Assets<T>` collection when it is loaded through the `handle_internal_events` system that is added by the `AssetPlugin`.

- **Load**: Load an asset from the filesystem. There will be four functions, `load<T>(path)`, `load_untyped(path)`, `load_untracked<T>(path)` and `load_untracked_untyped(path)`.
  - `load<T>(path)`: Load an asset of type `T` from the given path. This will return a strong `Handle<T>` to the asset.
  - `load_untyped(path)`: Load an asset from the given path without specifying the type. This will return a strong `Handle<Asset>` to the asset, and the type will be determined by the registered loaders based on the file extension.
  - `load_untracked<T>(path)`: Load an asset of type `T` from the given path without tracking it in the `AssetServer`. This will return a strong `Handle<T>` to the asset, but it will not be tracked in the `AssetServer`, which means that this function will not check if the asset is already loaded or not, and will just load it again.
  - `load_untracked_untyped(path)`: The untyped version of `load_untracked<T>(path)`. Type be determined by the registered loaders based on the file extension.
- **Custom Loaders**: Register custom loaders for specific asset types and file extensions.
- **Reload**: Reload an asset from the filesystem. This is a manual operation that requires the user to call it explicitly. However, this will only work for tracked assets.
- **Hot reload**: Watching file changes and reloading assets automatically when they change. This is not the current focus of the module, but it is a future goal.
- **Get Handle**: Get a handle to an asset by its path. This will attempt to get a strong handle if possible, or nullptr if the asset is already deferenced and destructed.
- **No Get Asset**: The `AssetServer` should work with `Assets<T>`, which means that the loaded asset will be added to the `Assets<T>` collection, but not stored in the `AssetServer` itself. `AssetServer` only handle `path`s and `Handle<T>`s, not the actual assets. This is to avoid duplication of assets in memory and to ensure that the `Assets<T>` collection is the single source of truth for assets.

### How the loading works

When you call `load` on the `AssetServer`, it will:
- Check if the asset is already loaded in the `Assets<T>` collection.
- If it is loaded, return the existing `Handle<T>`.
- If it is not loaded, it search for a registered loader for the asset type and file extension.
  - If a loader is found, it will load the asset asynchronously and return a reserved `Handle<T>` that point to a slot in `Asset<T>` in which the asset will be inserted. The asynchronous loading operation will send an internal event `AssetLoadedEvent` which contains type erased container for the asset to the internal event queue.
  - If no loader is found, it will still return a `Handle<T>` that is reserved for the asset, and this path will be cached in the `AssetServer` for future use.

And for each frame, the `handle_internal_events` system will process the internal events and insert the actual asset into the `Assets<T>` collection, and sending user-side events if necessary.