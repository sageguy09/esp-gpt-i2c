#!/usr/bin/env python3
# ArtNet Test Script for ESP32 LED Controller
# Usage: python artnet_test.py [IP_ADDRESS] [UNIVERSE] [EFFECT]
#   IP_ADDRESS: ESP32's IP address or network broadcast address (default: 192.168.50.244)
#   UNIVERSE: ArtNet universe to send to (default: 0)
#   EFFECT: Animation effect (rainbow, chase, random, solid) (default: rainbow)

import socket
import time
import random
import sys
import math
import colorsys

# Default settings
DEFAULT_IP = "192.168.50.244"  # Replace with your ESP32's IP
DEFAULT_UNIVERSE = 0
DEFAULT_NUM_LEDS = 144
DEFAULT_EFFECT = "rainbow"
ARTNET_PORT = 6454

# Parse command line arguments
target_ip = DEFAULT_IP
universe = DEFAULT_UNIVERSE
num_leds = DEFAULT_NUM_LEDS
effect = DEFAULT_EFFECT

if len(sys.argv) > 1:
    target_ip = sys.argv[1]
if len(sys.argv) > 2:
    universe = int(sys.argv[2])
if len(sys.argv) > 3:
    effect = sys.argv[3].lower()

# Create UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

print(f"ArtNet Test Script")
print(f"Target: {target_ip}:{ARTNET_PORT}")
print(f"Universe: {universe}")
print(f"LEDs: {num_leds}")
print(f"Effect: {effect}")
print("Press Ctrl+C to exit")

def create_artnet_packet(universe, dmx_data):
    """Create an ArtNet DMX packet."""
    # ArtNet header (Art-Net)
    packet = bytearray()
    packet.extend(b'Art-Net\0')
    
    # OpCode for ArtDMX (0x5000)
    packet.extend([0x00, 0x50])
    
    # Protocol version (14)
    packet.extend([0x00, 0x0e])
    
    # Sequence (0)
    packet.extend([0x00])
    
    # Physical port (0)
    packet.extend([0x00])
    
    # Universe (16-bit)
    packet.extend([universe & 0xFF, (universe >> 8) & 0xFF])
    
    # Length (16-bit, high byte first)
    length = len(dmx_data)
    packet.extend([(length >> 8) & 0xFF, length & 0xFF])
    
    # DMX data
    packet.extend(dmx_data)
    
    return packet

def hsv_to_rgb(h, s, v):
    """Convert HSV to RGB values (0-255)."""
    r, g, b = colorsys.hsv_to_rgb(h/255.0, s/255.0, v/255.0)
    return int(r * 255), int(g * 255), int(b * 255)

def rainbow_effect():
    """Generate a moving rainbow effect."""
    dmx_data = bytearray(num_leds * 3)
    offset = 0
    
    try:
        while True:
            for i in range(num_leds):
                # Calculate color (spread the hue across the strip)
                hue = (i * 5 + offset) % 256
                r, g, b = hsv_to_rgb(hue, 255, 255)
                
                # Set RGB values in DMX data
                dmx_data[i * 3] = r
                dmx_data[i * 3 + 1] = g
                dmx_data[i * 3 + 2] = b
            
            # Create and send ArtNet packet
            packet = create_artnet_packet(universe, dmx_data)
            sock.sendto(packet, (target_ip, ARTNET_PORT))
            
            # Increment offset for animation
            offset = (offset + 1) % 256
            
            # Frame rate control
            time.sleep(0.03)
    except KeyboardInterrupt:
        # Turn off all LEDs when exiting
        clear_leds()
        print("\nRainbow effect stopped")

def chase_effect():
    """Generate a chasing dot effect."""
    dmx_data = bytearray(num_leds * 3)
    position = 0
    colors = [
        (255, 0, 0),    # Red
        (0, 255, 0),    # Green
        (0, 0, 255),    # Blue
        (255, 255, 0),  # Yellow
        (255, 0, 255),  # Magenta
        (0, 255, 255),  # Cyan
    ]
    color_index = 0
    
    try:
        while True:
            # Clear all LEDs
            for i in range(num_leds * 3):
                dmx_data[i] = 0
            
            # Set the current positions
            # Main dot
            dmx_data[position * 3] = colors[color_index][0]
            dmx_data[position * 3 + 1] = colors[color_index][1]
            dmx_data[position * 3 + 2] = colors[color_index][2]
            
            # Trailing dots with decreasing brightness
            for trail in range(1, 10):
                trail_pos = (position - trail) % num_leds
                brightness = int(255 * (10 - trail) / 10)
                dmx_data[trail_pos * 3] = int(colors[color_index][0] * brightness / 255)
                dmx_data[trail_pos * 3 + 1] = int(colors[color_index][1] * brightness / 255)
                dmx_data[trail_pos * 3 + 2] = int(colors[color_index][2] * brightness / 255)
            
            # Create and send ArtNet packet
            packet = create_artnet_packet(universe, dmx_data)
            sock.sendto(packet, (target_ip, ARTNET_PORT))
            
            # Move position
            position = (position + 1) % num_leds
            
            # Change color every lap
            if position == 0:
                color_index = (color_index + 1) % len(colors)
            
            # Frame rate control
            time.sleep(0.03)
    except KeyboardInterrupt:
        # Turn off all LEDs when exiting
        clear_leds()
        print("\nChase effect stopped")

def random_effect():
    """Generate sparkling random colors."""
    dmx_data = bytearray(num_leds * 3)
    
    try:
        while True:
            # Fade existing colors by 10%
            for i in range(num_leds):
                dmx_data[i*3] = int(dmx_data[i*3] * 0.9)
                dmx_data[i*3+1] = int(dmx_data[i*3+1] * 0.9)
                dmx_data[i*3+2] = int(dmx_data[i*3+2] * 0.9)
            
            # Add new random sparks
            for _ in range(int(num_leds * 0.05)):  # 5% of LEDs get new sparks
                i = random.randint(0, num_leds - 1)
                dmx_data[i*3] = random.randint(180, 255)
                dmx_data[i*3+1] = random.randint(180, 255)
                dmx_data[i*3+2] = random.randint(180, 255)
            
            # Create and send ArtNet packet
            packet = create_artnet_packet(universe, dmx_data)
            sock.sendto(packet, (target_ip, ARTNET_PORT))
            
            # Frame rate control
            time.sleep(0.05)
    except KeyboardInterrupt:
        # Turn off all LEDs when exiting
        clear_leds()
        print("\nRandom effect stopped")

def solid_color():
    """Set a solid color that cycles through the spectrum."""
    dmx_data = bytearray(num_leds * 3)
    hue = 0
    
    try:
        while True:
            # Convert HSV to RGB
            r, g, b = hsv_to_rgb(hue, 255, 255)
            
            # Set all LEDs to the same color
            for i in range(num_leds):
                dmx_data[i*3] = r
                dmx_data[i*3+1] = g
                dmx_data[i*3+2] = b
            
            # Create and send ArtNet packet
            packet = create_artnet_packet(universe, dmx_data)
            sock.sendto(packet, (target_ip, ARTNET_PORT))
            
            # Change color
            hue = (hue + 1) % 256
            
            # Frame rate control
            time.sleep(0.05)
    except KeyboardInterrupt:
        # Turn off all LEDs when exiting
        clear_leds()
        print("\nSolid color effect stopped")

def clear_leds():
    """Turn off all LEDs."""
    dmx_data = bytearray(num_leds * 3)  # All zeros
    packet = create_artnet_packet(universe, dmx_data)
    sock.sendto(packet, (target_ip, ARTNET_PORT))
    print("LEDs cleared")

# Run the selected effect
if effect == "rainbow":
    rainbow_effect()
elif effect == "chase":
    chase_effect()
elif effect == "random":
    random_effect()
elif effect == "solid":
    solid_color()
else:
    print(f"Unknown effect: {effect}")
    print("Available effects: rainbow, chase, random, solid")
    clear_leds()
