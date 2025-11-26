import sys
import os

def file_to_header(input_file, output_file, array_name):
    # Read the input file as binary
    with open(input_file, 'rb') as f:
        data = f.read()
    
    # Generate the header file content
    header_content = f"""// Auto-generated header file from {input_file}
// File size: {len(data)} bytes

#ifndef {array_name.upper()}_H
#define {array_name.upper()}_H

const unsigned char {array_name}[] = {{
"""
    
    # Format bytes in rows of 16 for readability
    bytes_per_line = 16
    for i in range(0, len(data), bytes_per_line):
        line_bytes = data[i:i + bytes_per_line]
        hex_bytes = [f"0x{b:02X}" for b in line_bytes]
        header_content += "    " + ", ".join(hex_bytes)
        if i + bytes_per_line < len(data):
            header_content += ","
        header_content += "\n"
    
    header_content += f"""}};

const unsigned int {array_name}_size = sizeof({array_name});

#endif // {array_name.upper()}_H
"""
    
    # Write the header file
    with open(output_file, 'w') as f:
        f.write(header_content)
    
    print(f"Generated {output_file} with {len(data)} bytes")

def main():
    if len(sys.argv) < 2:
        print("Usage: python file_to_header.py <input_file> [output_file] [array_name]")
        return
    
    input_file = sys.argv[1]
    
    # Set default output filename
    if len(sys.argv) > 2:
        output_file = sys.argv[2]
    else:
        base_name = os.path.splitext(input_file)[0]
        output_file = f"{base_name}.h"
    
    # Set default array name
    if len(sys.argv) > 3:
        array_name = sys.argv[3]
    else:
        base_name = os.path.splitext(os.path.basename(input_file))[0]
        array_name = f"{base_name}_data"
    
    # Validate input file exists
    if not os.path.exists(input_file):
        print(f"Error: Input file '{input_file}' not found")
        return
    
    try:
        file_to_header(input_file, output_file, array_name)
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()