#pragma once

/**
 * @file core.hpp
 * @brief Compatibility header for epix core module.
 * 
 * This header imports the epix.core C++20 module and makes all its
 * symbols available for backward compatibility with code that uses
 * #include <epix/core.hpp>
 * 
 * Original header archived at: epix_engine/archived_headers/core/core.hpp.20251216-022250
 */

// Import the epix.core module
import epix.core;

// All symbols are now available through the import
// The module exports all namespaces: epix::core, epix::core::prelude, epix, epix::prelude