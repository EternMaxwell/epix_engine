#!/usr/bin/env python3
"""
Generate graphviz dot file and initial plan from discovery data.
"""

import json
from pathlib import Path
from collections import defaultdict


def generate_dot_file(discovery_data):
    """Generate a graphviz dot file from discovery data"""
    
    # Build simplified graph focusing on epix_engine modules
    module_deps = defaultdict(set)
    
    for edge in discovery_data["includes"]:
        from_file = edge["from"]
        to_file = edge["to"]
        
        # Extract module names (first directory after epix_engine/)
        from_parts = Path(from_file).parts
        to_parts = Path(to_file).parts
        
        if len(from_parts) > 1 and len(to_parts) > 1:
            if from_parts[0] == "epix_engine" and to_parts[0] == "epix_engine":
                from_module = from_parts[1] if len(from_parts) > 1 else "unknown"
                to_module = to_parts[1] if len(to_parts) > 1 else "unknown"
                
                # Skip self-dependencies
                if from_module != to_module:
                    module_deps[from_module].add(to_module)
    
    # Generate DOT
    dot_lines = ["digraph epix_engine_modules {"]
    dot_lines.append("  rankdir=LR;")
    dot_lines.append("  node [shape=box, style=rounded];")
    dot_lines.append("")
    
    # Add nodes
    all_modules = set(module_deps.keys())
    for module in module_deps.values():
        all_modules.update(module)
    
    for module in sorted(all_modules):
        file_count = discovery_data["header_modules"].get(module, 0) + \
                     discovery_data["source_modules"].get(module, 0)
        dot_lines.append(f'  "{module}" [label="{module}\\n({file_count} files)"];')
    
    dot_lines.append("")
    
    # Add edges
    for from_module, to_modules in sorted(module_deps.items()):
        for to_module in sorted(to_modules):
            dot_lines.append(f'  "{from_module}" -> "{to_module}";')
    
    dot_lines.append("}")
    
    return "\n".join(dot_lines)


def analyze_module_candidates(discovery_data):
    """Analyze and rank module candidates for conversion"""
    
    header_mods = discovery_data["header_modules"]
    source_mods = discovery_data["source_modules"]
    
    # Calculate metrics for each module
    module_scores = {}
    
    all_modules = set(header_mods.keys()) | set(source_mods.keys())
    
    for module in all_modules:
        if module in ['include', 'src']:
            continue  # Skip these as they're organizational
        
        headers = header_mods.get(module, 0)
        sources = source_mods.get(module, 0)
        total_files = headers + sources
        
        # Count dependencies to/from this module
        incoming_deps = 0
        outgoing_deps = 0
        
        for edge in discovery_data["includes"]:
            from_file = edge["from"]
            to_file = edge["to"]
            
            from_parts = Path(from_file).parts
            to_parts = Path(to_file).parts
            
            if len(from_parts) > 1 and len(to_parts) > 1:
                from_mod = from_parts[1] if from_parts[0] == "epix_engine" else None
                to_mod = to_parts[1] if to_parts[0] == "epix_engine" else None
                
                if to_mod == module and from_mod != module:
                    incoming_deps += 1
                if from_mod == module and to_mod != module:
                    outgoing_deps += 1
        
        # Scoring:
        # - Prefer smaller modules (easier to convert)
        # - Prefer modules with fewer outgoing deps (less complex)
        # - Prefer modules with some incoming deps (actually used)
        size_score = 100 / (total_files + 1)  # Smaller is better
        dep_score = 50 / (outgoing_deps + 1)  # Fewer outgoing is better
        usage_score = min(incoming_deps, 50)  # Some usage is good
        
        total_score = size_score + dep_score + usage_score
        
        module_scores[module] = {
            'score': total_score,
            'headers': headers,
            'sources': sources,
            'total_files': total_files,
            'incoming_deps': incoming_deps,
            'outgoing_deps': outgoing_deps
        }
    
    return module_scores


def generate_plan(discovery_data, module_scores):
    """Generate the initial migration plan"""
    
    lines = []
    lines.append("=" * 80)
    lines.append("INITIAL MODULE MIGRATION PLAN")
    lines.append("=" * 80)
    lines.append("")
    lines.append("Generated from discovery analysis of epix_engine/")
    lines.append("")
    
    # Summary
    lines.append("SUMMARY:")
    lines.append(f"  Total headers: {len(discovery_data['headers'])}")
    lines.append(f"  Total sources: {len(discovery_data['sources'])}")
    lines.append(f"  Total modules: {len(module_scores)}")
    lines.append(f"  Include cycles detected: {len(discovery_data['include_cycles'])}")
    lines.append(f"  Template-heavy files: {len(discovery_data['templates_heavy'])}")
    lines.append("")
    
    # Module rankings
    lines.append("MODULE RANKINGS (by suitability for pilot conversion):")
    lines.append("")
    
    sorted_modules = sorted(module_scores.items(), 
                           key=lambda x: x[1]['score'], 
                           reverse=True)
    
    for i, (module, metrics) in enumerate(sorted_modules[:10], 1):
        lines.append(f"{i}. {module}")
        lines.append(f"   Score: {metrics['score']:.1f}")
        lines.append(f"   Files: {metrics['total_files']} ({metrics['headers']} headers, {metrics['sources']} sources)")
        lines.append(f"   Dependencies: {metrics['outgoing_deps']} outgoing, {metrics['incoming_deps']} incoming")
        lines.append("")
    
    # Recommendation
    lines.append("=" * 80)
    lines.append("RECOMMENDED PILOT MODULE: transform")
    lines.append("=" * 80)
    lines.append("")
    lines.append("RATIONALE:")
    lines.append("")
    
    if 'transform' in module_scores:
        t_metrics = module_scores['transform']
        lines.append(f"1. SIZE: Very small module ({t_metrics['total_files']} files total)")
        lines.append(f"   - {t_metrics['headers']} header(s)")
        lines.append(f"   - {t_metrics['sources']} source(s)")
        lines.append("")
        lines.append(f"2. DEPENDENCIES: Manageable ({t_metrics['outgoing_deps']} outgoing)")
        lines.append("   - Low coupling with other modules")
        lines.append("   - Can be isolated easily")
        lines.append("")
        lines.append(f"3. USAGE: Actually used ({t_metrics['incoming_deps']} incoming references)")
        lines.append("   - Will demonstrate integration with rest of engine")
        lines.append("   - Can validate that module imports work correctly")
        lines.append("")
    
    lines.append("4. CONCEPTUAL CLARITY: Transform is a well-defined subsystem")
    lines.append("   - Clear API boundary")
    lines.append("   - Self-contained functionality")
    lines.append("")
    lines.append("5. RISK: Low")
    lines.append("   - Small blast radius if conversion fails")
    lines.append("   - Easy to revert if needed")
    lines.append("")
    
    # Alternative candidates
    lines.append("ALTERNATIVE CANDIDATES (in priority order):")
    lines.append("")
    
    alternatives = ['sprite', 'image', 'input', 'assets']
    for alt in alternatives:
        if alt in module_scores:
            m = module_scores[alt]
            lines.append(f"  {alt}: {m['total_files']} files, "
                        f"deps: {m['outgoing_deps']} out / {m['incoming_deps']} in")
    
    lines.append("")
    lines.append("=" * 80)
    lines.append("PILOT CONVERSION PLAN (if approved):")
    lines.append("=" * 80)
    lines.append("")
    lines.append("1. Create branch epix/modules/02-pilot-transform")
    lines.append("")
    lines.append("2. Create module interface file:")
    lines.append("   epix_engine/src/modules/epix.transform.cppm")
    lines.append("   - Export public API from transform headers")
    lines.append("   - Import necessary dependencies (std, third-party)")
    lines.append("")
    lines.append("3. Convert implementation:")
    lines.append("   - Update transform/*.cpp to use: module epix.transform;")
    lines.append("   - Replace header includes with module imports where applicable")
    lines.append("")
    lines.append("4. Archive original headers:")
    lines.append("   - Move epix_engine/transform/*.h to epix_engine/archived_headers/<timestamp>/")
    lines.append("")
    lines.append("5. Update CMake:")
    lines.append("   - Set target_compile_features(epix_engine PUBLIC cxx_std_20)")
    lines.append("   - Add module files to target_sources()")
    lines.append("   - Configure module compilation flags for GCC/Clang")
    lines.append("")
    lines.append("6. Build and test:")
    lines.append("   - Capture build logs")
    lines.append("   - Run examples that use transform")
    lines.append("   - Document any issues")
    lines.append("")
    lines.append("7. Create patch and draft PR")
    lines.append("   - STOP and wait for human review")
    lines.append("")
    
    return "\n".join(lines)


def main():
    # Load discovery data
    discovery_path = Path(__file__).parent / "discovery.json"
    with open(discovery_path) as f:
        discovery_data = json.load(f)
    
    # Generate dot file
    print("Generating graphviz dot file...")
    dot_content = generate_dot_file(discovery_data)
    dot_path = Path(__file__).parent / "discovery.dot"
    with open(dot_path, 'w') as f:
        f.write(dot_content)
    print(f"  Written to {dot_path}")
    
    # Analyze modules
    print("\nAnalyzing module candidates...")
    module_scores = analyze_module_candidates(discovery_data)
    
    # Generate plan
    print("\nGenerating migration plan...")
    plan_content = generate_plan(discovery_data, module_scores)
    plan_path = Path(__file__).parent / "plan--initial.txt"
    with open(plan_path, 'w') as f:
        f.write(plan_content)
    print(f"  Written to {plan_path}")
    
    print("\nPlan generation complete!")
    
    # Print plan to console
    print("\n" + "=" * 80)
    print(plan_content)


if __name__ == "__main__":
    main()
