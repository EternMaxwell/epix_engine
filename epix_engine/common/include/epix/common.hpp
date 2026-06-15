#pragma once

// EPIX_CXX_MODULE — only defined in .cppm module wrapper files.
// EPIX_EXPORT  — "export" when the header is pulled into module purview,
//                empty otherwise.

#ifdef EPIX_CXX_MODULE
#define EPIX_EXPORT export
#else
#define EPIX_EXPORT
#endif
