# Zaunkoenig Latency Tester Firmware

This is a fork of the official [Zaunkoenig Firmware](https://github.com/zaunkoenig/zaunkoenig-firmware) modified specifically for measuring latency with Reflex Latency Analyzer. 

## How It Works
The firmware listens for a physical Middle Mouse Button (MMB) press and executes the following sequence:
1. Sends an initial `Middle Click` to start the recording software.
2. Sends a `Left Click` every 500ms for exactly 60 seconds (120 total clicks).
3. Sends a final `Middle Click` to stop the recording software.
4. Returns to idle, ready to run again.

## Usage
1. Flash the compiled firmware to your mouse.
2. Open your latency measurement tool.
3. Press the **Middle Mouse Button** once to initiate the 60-second test. 
