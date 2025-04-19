# Circuit Playground Express I2C Monitor for ESP-GPT-I2C
# This script monitors I2C communication with ESP devices and
# provides status visualization using the CircuitPlayground's LEDs

import time
import board
import busio
import digitalio
from adafruit_circuitplayground import cp

# I2C Configuration - adjust addresses based on your ESP device
I2C_ADDR = 0x08  # Default ESP32 I2C address in esp-gpt-i2c project
I2C_FREQ = 100000  # 100kHz for stability

# Status LED configuration
NUM_PIXELS = 10  # CPX has 10 NeoPixels
LED_BRIGHTNESS = 0.2  # Lower brightness to save power

# Color definitions
COLOR_IDLE = (0, 0, 10)      # Blue - system is idle/waiting
COLOR_ACTIVE = (0, 10, 0)    # Green - active communication
COLOR_ERROR = (10, 0, 0)     # Red - communication error
COLOR_ALERT = (10, 10, 0)    # Yellow - alert condition
COLOR_OFF = (0, 0, 0)        # Off

# Button definitions
button_a = digitalio.DigitalInOut(board.BUTTON_A)
button_a.direction = digitalio.Direction.INPUT
button_a.pull = digitalio.Pull.DOWN

button_b = digitalio.DigitalInOut(board.BUTTON_B)
button_b.direction = digitalio.Direction.INPUT
button_b.pull = digitalio.Pull.DOWN

# Setup I2C
i2c = busio.I2C(board.SCL, board.SDA, frequency=I2C_FREQ)

# Define communication functions
def send_command(command, timeout=1.0):
    """Send a command to the ESP device via I2C"""
    try:
        i2c.try_lock()
        # Convert command to bytes if it's a string
        if isinstance(command, str):
            command = bytes(command, 'utf-8')
            
        i2c.writeto(I2C_ADDR, command)
        i2c.unlock()
        set_pixel_status(COLOR_ACTIVE)
        return True
    except Exception as e:
        print(f"Send error: {e}")
        set_pixel_status(COLOR_ERROR)
        return False

def receive_data(timeout=1.0):
    """Receive data from the ESP device via I2C"""
    try:
        result = bytearray(32)  # Adjust buffer size as needed
        i2c.try_lock()
        i2c.readfrom_into(I2C_ADDR, result)
        i2c.unlock()
        set_pixel_status(COLOR_ACTIVE)
        # Convert from bytearray to string and strip nulls
        return result.decode('utf-8').strip('\x00')
    except Exception as e:
        print(f"Receive error: {e}")
        set_pixel_status(COLOR_ERROR)
        return None

# Define LED status functions
def set_pixel_status(color):
    """Set all pixels to a specific color to indicate status"""
    for i in range(NUM_PIXELS):
        cp.pixels[i] = color
    time.sleep(0.2)  # Brief flash
    
def pulse_pixel(position, color, duration=0.5):
    """Pulse a single pixel at given position with color"""
    original_color = cp.pixels[position]
    cp.pixels[position] = color
    time.sleep(duration)
    cp.pixels[position] = original_color

def scan_i2c_devices():
    """Scan for I2C devices and print their addresses"""
    devices = []
    try:
        i2c.try_lock()
        for addr in range(0x00, 0x80):
            try:
                if i2c.scan().count(addr) > 0:
                    devices.append(addr)
                    print(f"I2C device found at address: 0x{addr:02X}")
            except Exception:
                pass
        i2c.unlock()
    except Exception as e:
        print(f"I2C scan error: {e}")
    
    # Display result on LEDs
    if devices:
        pulse_animation()
        return devices
    else:
        error_animation()
        return []

def pulse_animation():
    """Success animation"""
    for _ in range(2):
        for i in range(NUM_PIXELS):
            cp.pixels[i] = COLOR_ACTIVE
            time.sleep(0.05)
            cp.pixels[i] = COLOR_OFF
    # Reset to idle
    for i in range(NUM_PIXELS):
        cp.pixels[i] = COLOR_IDLE

def error_animation():
    """Error animation"""
    for _ in range(3):
        for i in range(NUM_PIXELS):
            cp.pixels[i] = COLOR_ERROR
        time.sleep(0.2)
        for i in range(NUM_PIXELS):
            cp.pixels[i] = COLOR_OFF
        time.sleep(0.2)
    # Reset to idle
    for i in range(NUM_PIXELS):
        cp.pixels[i] = COLOR_IDLE

# Main application logic
def main():
    print("Circuit Playground Express I2C Monitor")
    print("For ESP-GPT-I2C Project")
    print("------------------------------")
    
    # Initialize NeoPixels
    cp.pixels.brightness = LED_BRIGHTNESS
    set_pixel_status(COLOR_IDLE)
    
    # Initial I2C scan
    print("Scanning for I2C devices...")
    devices = scan_i2c_devices()
    
    if I2C_ADDR not in devices:
        print(f"Warning: ESP device at address 0x{I2C_ADDR:02X} not found!")
        set_pixel_status(COLOR_ALERT)
        time.sleep(2)
    
    print("Monitoring I2C communication...")
    
    try:
        while True:
            # Button A: Scan for devices
            if button_a.value:
                print("Scanning for devices...")
                devices = scan_i2c_devices()
                time.sleep(0.5)  # Debounce
            
            # Button B: Request status from ESP
            if button_b.value:
                print("Requesting status from ESP...")
                if send_command("STATUS"):
                    time.sleep(0.1)  # Give ESP time to respond
                    response = receive_data()
                    if response:
                        print(f"ESP Status: {response}")
                time.sleep(0.5)  # Debounce
            
            # Regular status polling (every 5 seconds)
            try:
                if i2c.try_lock():
                    # Just check if we can see the device
                    if I2C_ADDR in i2c.scan():
                        # Device is present, show idle color
                        set_pixel_status(COLOR_IDLE)
                    else:
                        # Device not found, show alert
                        set_pixel_status(COLOR_ALERT)
                    i2c.unlock()
            except Exception as e:
                print(f"Polling error: {e}")
                set_pixel_status(COLOR_ERROR)
            
            # Process any incoming data
            # This would be expanded based on your ESP-GPT-I2C protocol
            
            # Visual feedback through the CPX's LEDs
            # (could represent different states of your ESP application)
            
            # Light show based on accelerometer
            x, y, z = cp.acceleration
            if abs(x) > 9 or abs(y) > 9 or abs(z) > 11:
                # Device was shaken - run diagnostic
                print("Running diagnostic...")
                send_command("DIAG")
                time.sleep(0.1)
                response = receive_data()
                if response:
                    print(f"Diagnostic: {response}")
            
            time.sleep(0.1)  # Small delay to prevent CPU hogging
            
    except KeyboardInterrupt:
        print("Monitoring stopped")
        set_pixel_status(COLOR_OFF)

if __name__ == "__main__":
    main()