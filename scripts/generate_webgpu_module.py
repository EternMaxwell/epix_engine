#!/usr/bin/env python3
"""
Generate a C++20 module (.cppm) that re-exports entities from the webgpu.hpp header.

This script parses the generated webgpu.hpp header, extracts all public entity names
from the wgpu namespace, and produces a thin .cppm module wrapper that re-exports
each entity individually using `export using` declarations.

Usage:
    python3 generate_webgpu_module.py -i <webgpu.hpp> -o <webgpu.cppm>
"""

import argparse
import re
import sys
from pathlib import Path
from typing import List, Set


# Internal/helper names that should NOT be re-exported.
# These are either template helpers (SmallVec, NextInChain*) or nested types
# inside generated classes/structs/callbacks (CStruct, Control*, etc.).
INTERNAL_NAMES: Set[str] = {
    # Template helpers defined at namespace scope
    "SmallVec",
    "NextInChain",
    "NextInChainBase",
    "NextInChainImpl",
    "NextInChainNative",
    # Nested types inside callback structs
    "Control",
    "ControlImpl",
    "ControlNative",
    # Nested C struct mirror inside generated structs/handles
    "CStruct",
    # Friend macro names (not real types — #define WEBGPU_HANDLE_FRIENDS etc.)
    "WEBGPU_HANDLE_FRIENDS",
    "WEBGPU_RAII_FRIENDS",
    # False positives from template/internal code
    "base_type",
    "value_type",
    "wgpu_type",
    "C",
}

# C++ keywords that could be falsely matched by the function-name regex.
CPP_KEYWORDS: Set[str] = {
    "if", "while", "for", "switch", "return", "sizeof", "decltype",
    "static_cast", "reinterpret_cast", "dynamic_cast", "const_cast",
    "alignof", "alignas", "noexcept", "throw", "try", "catch",
    "new", "delete", "class", "struct", "enum", "namespace",
    "template", "typename", "using", "typedef", "friend",
}


def strip_comments(content: str) -> str:
    """Strip C and C++ style comments from source content."""
    # Remove /* ... */ block comments
    content = re.sub(r"/\*.*?\*/", "", content, flags=re.DOTALL)
    # Remove // line comments
    content = re.sub(r"//[^\n]*", "", content)
    return content


def extract_namespace_blocks(content: str, namespace_pattern: str) -> List[str]:
    """Extract the content of all top-level namespace blocks matching the pattern.

    Uses brace-depth tracking to correctly handle nested braces inside class/struct
    bodies.  Returns a list of block contents (text between the outer braces).
    """
    blocks: List[str] = []
    for m in re.finditer(namespace_pattern, content):
        start = m.end()
        depth = 1
        i = start
        while i < len(content) and depth > 0:
            if content[i] == "{":
                depth += 1
            elif content[i] == "}":
                depth -= 1
            i += 1
        block = content[start : i - 1]
        blocks.append(block)
    return blocks


def parse_entities_in_block(block: str) -> Set[str]:
    """Parse entity names from a single namespace-block body."""
    entities: Set[str] = set()

    # Type alias:  using Name = ...;
    for m in re.finditer(r"^\s*using\s+(\w+)\s*=", block, re.MULTILINE):
        entities.add(m.group(1))

    # Enum:  enum class Name { … };  or  enum class Name : Flags { … };
    for m in re.finditer(
        r"^\s*enum\s+class\s+(\w+)\s*(?::[^{]*)?\{", block, re.MULTILINE
    ):
        entities.add(m.group(1))

    # Class or struct (forward declaration, definition, or inheritance):
    #   class Name;
    #   class Name {
    #   class Name : public Base {
    for m in re.finditer(
        r"^\s*(?:class|struct)\s+(\w+)\s*(?:;|\{|:)", block, re.MULTILINE
    ):
        entities.add(m.group(1))

    # Function declarations:
    #   return_type func_name(params);
    # Handles qualified return types (e.g. wgpu::Adapter, std::string_view).
    for m in re.finditer(
        r"^\s*(?:[\w:]+(?:<[^>]*>)?[\s*&]+)+(\w+)\s*\([^)]*\)\s*;",
        block,
        re.MULTILINE,
    ):
        name = m.group(1)
        if name not in CPP_KEYWORDS:
            entities.add(name)

    return entities


def parse_header_entities(header_path: str):
    """Parse the generated webgpu.hpp header and return entities grouped by namespace.

    Returns dict: namespace -> set of entity names
    """
    content = Path(header_path).read_text(encoding="utf-8")
    content = strip_comments(content)

    ns_entities = {}

    # Parse wgpu namespace
    for block in extract_namespace_blocks(content, r"namespace\s+wgpu\s*\{"):
        entities = parse_entities_in_block(block)
        clean = entities - INTERNAL_NAMES
        if clean:
            ns_entities.setdefault("wgpu", set()).update(clean)

    # Parse wgpu::raw sub-namespace (supports both inline and nested syntax)
    # Note: entities may exist in BOTH wgpu and wgpu::raw with different types
    # Inline: namespace wgpu::raw { ... }
    for block in extract_namespace_blocks(content, r"namespace\s+wgpu\s*::\s*raw\s*\{"):
        entities = parse_entities_in_block(block)
        clean = entities - INTERNAL_NAMES
        if clean:
            ns_entities.setdefault("wgpu::raw", set()).update(clean)

    # Nested: inside namespace wgpu { ... namespace raw { ... } ... }
    for wgpu_block in extract_namespace_blocks(content, r"namespace\s+wgpu\s*\{"):
        for raw_block in extract_namespace_blocks(wgpu_block, r"namespace\s+raw\s*\{"):
            entities = parse_entities_in_block(raw_block)
            clean = entities - INTERNAL_NAMES
            if clean:
                ns_entities.setdefault("wgpu::raw", set()).update(clean)

    return ns_entities


def generate_module(
    header_path: str,
    output_path: str,
    module_name: str = "webgpu",
    header_include: str = "<webgpu/webgpu.hpp>",
) -> int:
    """Generate a .cppm module that re-exports entities from the header."""
    ns_entities = parse_header_entities(header_path)

    if not ns_entities:
        print(
            f"Warning: No entities found in {header_path}. "
            "Make sure the header has been generated.",
            file=sys.stderr,
        )
        return 0

    lines: List[str] = []
    lines.append("module;")
    lines.append(f"#include {header_include}")
    lines.append("")
    lines.append(f"export module {module_name};")
    lines.append("")

    total = 0
    for ns in sorted(ns_entities.keys(), key=lambda n: (n.count("::"), n)):
        entities = sorted(ns_entities[ns])
        lines.append(f"export namespace {ns} {{")
        for name in entities:
            lines.append(f"using {ns}::{name};")
        # Export operator| for flag types (BitwiseOr) — wgpu only
        if ns == "wgpu":
            lines.append(f"using {ns}::operator|;")
        lines.append(f"}} // namespace {ns}")
        lines.append("")
        total += len(entities)

    output = "\n".join(lines)
    Path(output_path).write_text(output, encoding="utf-8")

    print(f"Generated {output_path} with {total} re-exports in {len(ns_entities)} namespaces from {header_path}")
    return total


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate WebGPU module re-export from header"
    )
    parser.add_argument(
        "-i",
        "--input",
        required=True,
        help="Path to generated webgpu.hpp header",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="webgpu.cppm",
        help="Output path for the .cppm module file",
    )
    parser.add_argument(
        "-n",
        "--name",
        default="webgpu",
        help="Module name (default: webgpu)",
    )
    parser.add_argument(
        "--header-include",
        default="<webgpu/webgpu.hpp>",
        help="Include path for the header in the module (default: <webgpu/webgpu.hpp>)",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: Input header not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    count = generate_module(
        header_path=args.input,
        output_path=args.output,
        module_name=args.name,
        header_include=args.header_include,
    )

    if count == 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
