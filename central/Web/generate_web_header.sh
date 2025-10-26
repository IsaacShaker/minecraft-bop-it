#!/bin/bash

# Script to generate web_interface.h from index.html
# Usage: ./generate_web_header.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HTML_FILE="$SCRIPT_DIR/index.html"
HEADER_FILE="$SCRIPT_DIR/web_interface.h"

# Check if HTML file exists
if [ ! -f "$HTML_FILE" ]; then
    echo "Error: $HTML_FILE not found!"
    exit 1
fi

echo "Generating $HEADER_FILE from $HTML_FILE..."

# Create the header file with include guards and PROGMEM string
cat > "$HEADER_FILE" << 'EOF'
// Auto-generated header file - DO NOT EDIT MANUALLY
// Generated from Web/index.html
// Run ./generate_web_header.sh to regenerate

#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(
EOF

# Append the HTML content
cat "$HTML_FILE" >> "$HEADER_FILE"

# Close the string and header guard
cat >> "$HEADER_FILE" << 'EOF'
)HTML";

#endif // WEB_INTERFACE_H
EOF

echo "Header file generated successfully: $HEADER_FILE"
echo "You can now #include \"web_interface.h\" in your Arduino sketch"