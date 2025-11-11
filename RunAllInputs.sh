#!/bin/bash

# Directory containing input files
INPUT_DIR="input_files"

make all

# Check if the input directory exists
if [ ! -d "$INPUT_DIR" ]; then
    echo "Error: Directory '$INPUT_DIR' does not exist."
    exit 1
fi

# Iterate over all files in the input directory
for input_file in "$INPUT_DIR"/*; do
    if [ -f "$input_file" ]; then
        echo "Running e-eco algorithm with input file: $input_file"
        ./simulator -v 0 "$input_file"
        echo "----------------------------------------"
    fi
done