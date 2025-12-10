#!/usr/bin/env python3
"""
Script to assist with converting Epix Engine headers to C++20 modules.
"""

import argparse
import re
from pathlib import Path
from typing import List, Dict

class ModuleConverter:
    def __init__(self, root_path: str):
        self.root = Path(root_path)
        self.include_pattern = re.compile(r'^\s*#include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
        
    def analyze_header(self, header_path: Path) -> Dict:
        """Analyze a header file for dependencies"""
        with open(header_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        includes = self.include_pattern.findall(content)
        std_includes = [i for i in includes if not i.startswith('epix/')]
        internal_includes = [i for i in includes if i.startswith('epix/')]
        
        namespace_pattern = re.compile(r'namespace\s+([\w:]+)\s*{', re.MULTILINE)
        namespaces = namespace_pattern.findall(content)
        
        class_pattern = re.compile(r'(?:class|struct)\s+(\w+)', re.MULTILINE)
        classes = class_pattern.findall(content)
        
        return {
            'path': header_path,
            'std_includes': std_includes,
            'internal_includes': internal_includes,
            'namespaces': list(set(namespaces)),
            'classes': classes,
        }
    
    def find_all_headers(self, module_path: Path) -> List[Path]:
        """Find all header files in a module"""
        headers = []
        include_dir = module_path / 'include'
        if include_dir.exists():
            headers.extend(include_dir.rglob('*.hpp'))
            headers.extend(include_dir.rglob('*.h'))
        return headers

def main():
    parser = argparse.ArgumentParser(description='Convert Epix Engine headers to C++20 modules')
    parser.add_argument('--root', default='.', help='Root directory')
    parser.add_argument('--module', help='Module to analyze')
    parser.add_argument('--analyze', action='store_true', help='Analyze headers')
    
    args = parser.parse_args()
    converter = ModuleConverter(args.root)
    
    if args.analyze and args.module:
        module_path = Path(args.root) / 'epix_engine' / args.module
        headers = converter.find_all_headers(module_path)
        print(f"Found {len(headers)} headers in {args.module} module")
        for h in sorted(headers):
            print(f"  {h.name}")

if __name__ == '__main__':
    main()
