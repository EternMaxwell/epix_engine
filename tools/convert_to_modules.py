#!/usr/bin/env python3
"""
C++20 Module Conversion Helper Tool

This tool helps with the conversion of C++ headers to module partitions.
Currently provides guidance and can be extended for automation.

Usage:
    python3 convert_to_modules.py <header_file> <partition_name> <module_name>

Example:
    python3 convert_to_modules.py epix_engine/core/include/epix/core/entities.hpp entities epix.core

Note: This is a helper tool. Manual review and adjustment of generated code is always required.
The conversion process for complex headers with templates and macros requires human judgment.

For now, use this as a reference for the conversion pattern:

1. Create module partition file: src/modules/<partition>.cppm
2. Add global module fragment with system headers
3. Add export module declaration
4. Import dependencies
5. Export namespace blocks with code from headers
6. Test and validate

See documentation/CPP20_MODULES_COMPLETION_GUIDE.md for detailed instructions.
"""

import sys

def main():
    print(__doc__)
    if len(sys.argv) > 1:
        print(f"\nRequested conversion: {' '.join(sys.argv[1:])}")
        print("\nRefer to completed examples in epix_engine/core/src/modules/")
        print("- fwd.cppm: Forward declarations pattern")
        print("- tick.cppm: C++23 conditional compilation pattern")
        print("- meta.cppm: Template and std::hash specialization pattern")
        print("- type_system.cppm: Complex types with macros pattern")

if __name__ == '__main__':
    main()
