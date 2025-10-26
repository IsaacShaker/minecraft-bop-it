#!/bin/bash

# Script to generate web_interface.h from separate HTML, CSS, and JS files
# Usage: ./generate_web_header.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HTML_FILE="$SCRIPT_DIR/index.html"
CSS_FILE="$SCRIPT_DIR/styles.css"
JS_FILE="$SCRIPT_DIR/script.js"
HEADER_FILE="$SCRIPT_DIR/web_interface.h"

# Check if all source files exist
for file in "$HTML_FILE" "$CSS_FILE" "$JS_FILE"; do
    if [ ! -f "$file" ]; then
        echo "Error: $(basename "$file") not found!"
        exit 1
    fi
done

echo "Generating $HEADER_FILE from HTML, CSS, and JS files..."

# Create the header file with include guards and PROGMEM string
cat > "$HEADER_FILE" << 'EOF'
// Auto-generated header file - DO NOT EDIT MANUALLY
// Generated from Web/index.html, styles.css, and script.js
// Run ./generate_web_header.sh to regenerate

#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(
EOF

# Process the HTML file and inline CSS and JS
while IFS= read -r line; do
    if [[ "$line" == *'<link rel="stylesheet" href="styles.css">'* ]]; then
        # Replace CSS link with inline styles
        echo "  <style>"
        cat "$CSS_FILE" | sed 's/^/    /'
        echo "  </style>"
    elif [[ "$line" == *'<script src="script.js"></script>'* ]]; then
        # Replace JS link with inline script
        echo "  <script>"
        cat "$JS_FILE" | sed 's/^/    /'
        echo "  </script>"
    else
        # Output the line as-is
        echo "$line"
    fi
done < "$HTML_FILE" >> "$HEADER_FILE"

# Close the string and header guard
cat >> "$HEADER_FILE" << 'EOF'
)HTML";

#endif // WEB_INTERFACE_H
EOF

echo "Header file generated successfully: $HEADER_FILE"
echo "Combined HTML, CSS, and JS into single embedded file"
echo "You can now #include \"web_interface.h\" in your Arduino sketch"