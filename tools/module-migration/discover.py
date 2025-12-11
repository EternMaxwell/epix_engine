#!/usr/bin/env python3
"""
Discovery script for C++20 module migration.
Analyzes header/source dependencies in epix_engine/ directory.
"""

import os
import re
import json
from pathlib import Path
from collections import defaultdict, Counter
from typing import Dict, List, Set, Tuple

# Root directory of the engine
ENGINE_ROOT = Path(__file__).parent.parent.parent / "epix_engine"

# File extensions to analyze
HEADER_EXTENSIONS = {'.h', '.hpp', '.hh', '.hxx', '.inl', '.ipp'}
SOURCE_EXTENSIONS = {'.cpp', '.cc', '.cxx', '.c'}
ALL_EXTENSIONS = HEADER_EXTENSIONS | SOURCE_EXTENSIONS


def find_all_files():
    """Find all header and source files in epix_engine/"""
    headers = []
    sources = []
    
    for root, dirs, files in os.walk(ENGINE_ROOT):
        # Skip test directories
        if 'test' in root or 'tests' in root:
            continue
            
        for file in files:
            ext = Path(file).suffix
            if ext in HEADER_EXTENSIONS:
                headers.append(str(Path(root) / file))
            elif ext in SOURCE_EXTENSIONS:
                sources.append(str(Path(root) / file))
    
    return sorted(headers), sorted(sources)


def extract_includes(filepath: str) -> List[str]:
    """Extract all #include directives from a file"""
    includes = []
    include_pattern = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
    
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                match = include_pattern.match(line)
                if match:
                    includes.append(match.group(1))
    except Exception as e:
        print(f"Warning: Could not read {filepath}: {e}")
    
    return includes


def analyze_includes(headers: List[str], sources: List[str]):
    """Analyze include dependencies between files"""
    all_files = headers + sources
    includes_map = {}
    include_edges = []
    include_counts = Counter()
    
    # Build a map of filenames to full paths for quick lookup
    filename_to_path = {}
    for filepath in all_files:
        filename = Path(filepath).name
        if filename not in filename_to_path:
            filename_to_path[filename] = []
        filename_to_path[filename].append(filepath)
    
    # Also map by relative path from epix_engine
    for filepath in all_files:
        try:
            rel_path = Path(filepath).relative_to(ENGINE_ROOT)
            rel_str = str(rel_path)
            if rel_str not in filename_to_path:
                filename_to_path[rel_str] = []
            filename_to_path[rel_str].append(filepath)
        except ValueError:
            pass
    
    # Extract includes from each file
    for filepath in all_files:
        includes = extract_includes(filepath)
        includes_map[filepath] = includes
        
        for inc in includes:
            include_counts[inc] += 1
            
            # Try to resolve the include to an actual file
            # First try exact match
            if inc in filename_to_path:
                for target in filename_to_path[inc]:
                    include_edges.append({
                        "from": filepath,
                        "to": target,
                        "include": inc
                    })
            else:
                # Try basename match
                inc_basename = Path(inc).name
                if inc_basename in filename_to_path:
                    for target in filename_to_path[inc_basename]:
                        include_edges.append({
                            "from": filepath,
                            "to": target,
                            "include": inc
                        })
    
    return includes_map, include_edges, include_counts


def detect_cycles(include_edges: List[Dict]) -> List[List[str]]:
    """Detect cycles in the include graph using DFS"""
    # Build adjacency list
    graph = defaultdict(set)
    for edge in include_edges:
        graph[edge["from"]].add(edge["to"])
    
    cycles = []
    visited = set()
    rec_stack = set()
    
    def dfs(node, path):
        visited.add(node)
        rec_stack.add(node)
        path.append(node)
        
        for neighbor in graph[node]:
            if neighbor not in visited:
                dfs(neighbor, path.copy())
            elif neighbor in rec_stack:
                # Found a cycle
                cycle_start = path.index(neighbor)
                cycle = path[cycle_start:] + [neighbor]
                # Simplify to just filenames for readability
                simple_cycle = [Path(f).name for f in cycle]
                if simple_cycle not in cycles:
                    cycles.append(simple_cycle)
        
        rec_stack.remove(node)
    
    for node in list(graph.keys()):
        if node not in visited:
            dfs(node, [])
    
    return cycles


def analyze_templates_and_macros(files: List[str]) -> Tuple[List[str], List[str]]:
    """Identify files with heavy template or macro usage"""
    template_heavy = []
    macro_heavy = []
    
    template_pattern = re.compile(r'\btemplate\s*<')
    macro_pattern = re.compile(r'^\s*#\s*define\s+')
    
    for filepath in files:
        try:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                
            template_count = len(template_pattern.findall(content))
            macro_count = len(macro_pattern.findall(content, re.MULTILINE))
            
            # Threshold for "heavy" usage
            if template_count > 5:
                template_heavy.append(filepath)
            if macro_count > 10:
                macro_heavy.append(filepath)
        except Exception as e:
            print(f"Warning: Could not analyze {filepath}: {e}")
    
    return template_heavy, macro_heavy


def categorize_by_module(files: List[str]) -> Dict[str, List[str]]:
    """Categorize files by their module (subdirectory under epix_engine/)"""
    modules = defaultdict(list)
    
    for filepath in files:
        try:
            rel_path = Path(filepath).relative_to(ENGINE_ROOT)
            parts = rel_path.parts
            
            if len(parts) > 0:
                module_name = parts[0]
                modules[module_name].append(filepath)
        except ValueError:
            modules["unknown"].append(filepath)
    
    return dict(modules)


def main():
    print("Starting discovery process...")
    print(f"Engine root: {ENGINE_ROOT}")
    
    # Find all files
    print("\n1. Finding all header and source files...")
    headers, sources = find_all_files()
    print(f"   Found {len(headers)} headers and {len(sources)} sources")
    
    # Analyze includes
    print("\n2. Analyzing include dependencies...")
    includes_map, include_edges, include_counts = analyze_includes(headers, sources)
    print(f"   Found {len(include_edges)} include relationships")
    
    # Detect cycles
    print("\n3. Detecting include cycles...")
    cycles = detect_cycles(include_edges)
    print(f"   Found {len(cycles)} potential cycles")
    
    # Analyze templates and macros
    print("\n4. Analyzing template and macro usage...")
    template_heavy, macro_heavy = analyze_templates_and_macros(headers + sources)
    print(f"   Found {len(template_heavy)} template-heavy files")
    print(f"   Found {len(macro_heavy)} macro-heavy files")
    
    # Categorize by module
    print("\n5. Categorizing files by module...")
    header_modules = categorize_by_module(headers)
    source_modules = categorize_by_module(sources)
    print(f"   Found {len(header_modules)} header modules")
    print(f"   Found {len(source_modules)} source modules")
    
    # Build discovery data
    discovery_data = {
        "headers": [str(Path(h).relative_to(ENGINE_ROOT.parent)) for h in headers],
        "sources": [str(Path(s).relative_to(ENGINE_ROOT.parent)) for s in sources],
        "includes": [
            {
                "from": str(Path(e["from"]).relative_to(ENGINE_ROOT.parent)),
                "to": str(Path(e["to"]).relative_to(ENGINE_ROOT.parent)),
                "include": e["include"]
            }
            for e in include_edges
        ],
        "include_counts": [
            {"file": inc, "count": count}
            for inc, count in include_counts.most_common(50)
        ],
        "include_cycles": cycles[:20],  # Limit to top 20
        "templates_heavy": [str(Path(f).relative_to(ENGINE_ROOT.parent)) for f in template_heavy],
        "macros_heavy": [str(Path(f).relative_to(ENGINE_ROOT.parent)) for f in macro_heavy],
        "header_modules": {k: len(v) for k, v in header_modules.items()},
        "source_modules": {k: len(v) for k, v in source_modules.items()}
    }
    
    # Write JSON
    output_dir = Path(__file__).parent
    json_path = output_dir / "discovery.json"
    print(f"\n6. Writing discovery.json to {json_path}...")
    with open(json_path, 'w') as f:
        json.dump(discovery_data, f, indent=2)
    
    print("\nDiscovery complete!")
    return discovery_data, header_modules, source_modules


if __name__ == "__main__":
    main()
