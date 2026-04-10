import os
import re
import sys

# Regex patterns
INCLUDE_LOCAL_PATTERN = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)
INCLUDE_SYSTEM_PATTERN = re.compile(r'^\s*#\s*include\s+<([^>]+)>', re.MULTILINE)
PRAGMA_ONCE_PATTERN = re.compile(r'^\s*#\s*pragma\s+once', re.MULTILINE)

def process_file(input_path, include_dirs, processed_files, system_includes):
    abs_path = os.path.abspath(input_path)
    if abs_path in processed_files:
        return "" # Already handled
    
    processed_files.add(abs_path)

    if not os.path.exists(abs_path):
        return f"// Error: Could not find {input_path}\n"

    with open(abs_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # 1. Collect and remove system includes (e.g., <vector>)
    for match in INCLUDE_SYSTEM_PATTERN.finditer(content):
        system_includes.add(match.group(1))
    content = INCLUDE_SYSTEM_PATTERN.sub("", content)

    # 2. Remove #pragma once
    content = PRAGMA_ONCE_PATTERN.sub("", content)

    # 3. Recursively handle local includes
    def replace_local_include(match):
        include_file = match.group(1)
        found_path = None
        for d in include_dirs:
            potential_path = os.path.join(d, include_file)
            if os.path.exists(potential_path):
                found_path = potential_path
                break
        
        if found_path:
            return process_file(found_path, include_dirs, processed_files, system_includes)
        else:
            return match.group(0) # Keep it if not found

    return INCLUDE_LOCAL_PATTERN.sub(replace_local_include, content)

def main():
    if len(sys.argv) < 3:
        print("Usage: python amalgamate.py <entry_header> <output_header> [include_dir1, ...]")
        sys.exit(1)

    entry_header = sys.argv[1]
    output_header = sys.argv[2]
    include_paths = sys.argv[3:] if len(sys.argv) > 3 else [os.path.dirname(entry_header)]

    processed_files = set()
    system_includes = set()

    print(f"Amalgamating {entry_header}...")
    
    # Process the body
    body_content = process_file(entry_header, include_paths, processed_files, system_includes)

    # Build the final file
    final_output = "/* Auto Generated */\n"
    final_output += "/* Amalgamated Header */\n#pragma once\n\n"

    # Add sorted system includes at the top
    if system_includes:
        final_output += "// System Includes\n"
        for inc in sorted(list(system_includes)):
            final_output += f"#include <{inc}>\n"
        final_output += "\n"

    final_output += body_content

    # Write to file
    os.makedirs(os.path.dirname(os.path.abspath(output_header)), exist_ok=True)
    with open(output_header, 'w', encoding='utf-8') as f:
        f.write(final_output)
    
    print(f"Done! {len(system_includes)} system headers moved to top.")

if __name__ == "__main__":
    main()