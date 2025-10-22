# Central Server (ESP32)

This directory contains the central server code for the Block Party multiplayer Bop-It game.

## File Structure

- `central.ino` - Main Arduino sketch for ESP32
- `web_interface.h` - Auto-generated header containing the web interface HTML
- `data/index.html` - Source HTML file for the web interface
- `generate_web_header.sh` - Script to generate web_interface.h from index.html

## Building

Before compiling the Arduino sketch, make sure to generate the web interface header:

```bash
./generate_web_header.sh
```

This will create `web_interface.h` from `data/index.html`.

## Development Workflow

1. Edit the web interface in `data/index.html`
2. Run `./generate_web_header.sh` to update the header
3. Compile and upload `central.ino` to your ESP32

## Web Interface

The web interface is accessible at `http://192.168.4.1` when the ESP32 is running in AP mode.

Default WiFi credentials:
- SSID: `BlockParty`
- Password: `craft123`