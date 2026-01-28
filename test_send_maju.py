#!/usr/bin/env python3
"""
Test Script: Kirim Perintah MAJU ke ESP32
==========================================
Script sederhana untuk mengirim perintah "MAJU" ke ESP32 via USB Serial.

Usage:
    python3 test_send_maju.py
"""

import serial
import serial.tools.list_ports
import time
import sys
import os

# Configuration
BAUD_RATE = 115200
TIMEOUT = 1

def find_esp32_port():
    """Auto-detect ESP32 port"""
    ports = serial.tools.list_ports.comports()
    
    # Priority ports untuk Raspberry Pi
    priority_ports = ['/dev/ttyACM0', '/dev/ttyACM1', '/dev/ttyUSB0', '/dev/ttyUSB1']
    
    print("🔍 Mencari ESP32...")
    
    # Cek USB ports dulu (prioritas)
    for port_name in priority_ports:
        for port in ports:
            if port.device == port_name:
                print(f"✅ Ditemukan USB port: {port.device}")
                return port.device
    
    # Cek jika ada ttyACM atau ttyUSB
    for port in ports:
        if 'ttyACM' in port.device or 'ttyUSB' in port.device:
            print(f"✅ Ditemukan port: {port.device}")
            return port.device
    
    # Fallback ke GPIO UART (ttyAMA10 untuk Raspberry Pi 5)
    gpio_ports = ['/dev/ttyAMA10', '/dev/ttyAMA0', '/dev/ttyS0']
    for gpio_port in gpio_ports:
        if os.path.exists(gpio_port):
            print(f"⚠️  ESP32 USB tidak ditemukan!")
            print(f"   Menggunakan GPIO UART: {gpio_port}")
            print(f"   Pastikan kabel UART sudah terhubung:")
            print(f"   - ESP32 TX → GPIO 15 (RXD - Pin 10)")
            print(f"   - ESP32 RX → GPIO 14 (TXD - Pin 8)")
            print(f"   - GND → GND")
            return gpio_port
    
    return None

def read_all_responses(ser, timeout=0.5):
    """Baca semua response dari ESP32"""
    responses = []
    start = time.time()
    while (time.time() - start) < timeout:
        if ser.in_waiting:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    responses.append(line)
                    print(f"   📥 ESP32: {line}")
            except:
                pass
        time.sleep(0.01)
    return responses

def send_maju_command():
    """Kirim perintah MAJU ke ESP32"""
    
    print("=" * 50)
    print("🤖 TEST: Kirim Perintah MAJU ke ESP32")
    print("=" * 50)
    print()
    
    # Find port
    port = find_esp32_port()
    
    if not port:
        print("❌ Tidak ada port serial ditemukan!")
        print("   Pastikan ESP32 terhubung via USB.")
        return False
    
    try:
        # Connect to ESP32
        print(f"\n🔌 Menghubungkan ke {port}...")
        ser = serial.Serial(
            port=port,
            baudrate=BAUD_RATE,
            timeout=TIMEOUT
        )
        
        # Wait for ESP32 to be ready (lebih lama untuk reset)
        print("   Menunggu ESP32 ready...")
        time.sleep(3)
        print("✅ Terhubung ke ESP32!")
        
        # Clear any pending data dan baca startup messages
        print("\n📥 Startup messages dari ESP32:")
        read_all_responses(ser, timeout=1.0)
        
        # Send VOICE mode ON first
        print("\n" + "=" * 40)
        print("📡 [1] Mengirim VOICECOMMANDON...")
        command_on = "VOICE,VOICECOMMANDON\n"
        ser.write(command_on.encode('utf-8'))
        ser.flush()
        print(f"   SENT: {command_on.strip()}")
        read_all_responses(ser, timeout=1.0)
        
        # Send MAJU command
        print("\n" + "=" * 40)
        print("📡 [2] Mengirim perintah MAJU...")
        command_maju = "VOICE,MAJU\n"
        ser.write(command_maju.encode('utf-8'))
        ser.flush()
        print(f"   SENT: {command_maju.strip()}")
        read_all_responses(ser, timeout=1.0)
        
        # Robot bergerak selama 3 detik
        print("\n⏳ Robot bergerak selama 3 detik...")
        time.sleep(3)
        
        # Send STOP
        print("\n" + "=" * 40)
        print("📡 [3] Mengirim BERHENTI...")
        command_stop = "VOICE,BERHENTI\n"
        ser.write(command_stop.encode('utf-8'))
        ser.flush()
        print(f"   SENT: {command_stop.strip()}")
        read_all_responses(ser, timeout=1.0)
        
        # Close connection
        ser.close()
        print("\n" + "=" * 50)
        print("✅ Test selesai!")
        print("🔌 Koneksi ditutup.")
        print()
        print("⚠️  Jika robot tidak bergerak, cek:")
        print("   1. TEST_MODE = false di ESP32 code")
        print("   2. Motor driver terhubung dengan benar")
        print("   3. Power supply motor cukup")
        print("   4. Pin motor sesuai dengan definisi di code")
        return True
        
    except serial.SerialException as e:
        print(f"❌ Serial error: {e}")
        return False
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def send_custom_command(command):
    """Kirim custom command ke ESP32"""
    
    port = find_esp32_port()
    if not port:
        print("❌ Port tidak ditemukan!")
        return False
    
    try:
        ser = serial.Serial(port=port, baudrate=BAUD_RATE, timeout=TIMEOUT)
        time.sleep(2)
        
        cmd = f"VOICE,{command}\n"
        ser.write(cmd.encode('utf-8'))
        ser.flush()
        print(f"📡 SENT: {cmd.strip()}")
        
        time.sleep(0.5)
        while ser.in_waiting:
            response = ser.readline().decode('utf-8', errors='ignore').strip()
            if response:
                print(f"📥 ESP32: {response}")
        
        ser.close()
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

if __name__ == "__main__":
    if len(sys.argv) > 1:
        # Custom command dari argument
        command = sys.argv[1].upper()
        print(f"🎯 Mengirim command: {command}")
        send_custom_command(command)
    else:
        # Default: test MAJU
        send_maju_command()
