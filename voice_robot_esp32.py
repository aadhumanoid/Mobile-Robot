#!/usr/bin/env python3
"""
EVOBOT Voice Control System - ESP32 Integration
================================================
Voice Recognition + AI + ESP32 USB Serial Control

Integrasi dengan ESP32 Arduino code.
Format Serial: "COLOR,DIRECTION\n"

Contoh:
- "VOICE,MAJU\n"       → Robot maju
- "VOICE,MUNDUR\n"     → Robot mundur
- "VOICE,PUTARKANAN\n" → Robot belok kanan
- "VOICE,PUTARKIRI\n"  → Robot belok kiri
- "VOICE,BERHENTI\n"   → Robot berhenti
- "NONE,VOICECOMMANDON\n"  → Aktifkan voice mode
- "NONE,VOICECOMMANDOFF\n" → Nonaktifkan voice mode

Usage:
    python3 voice_robot_esp32.py
"""

import os
import sys
import warnings
import glob
import re
import time
import threading

# Suppress warnings
warnings.filterwarnings('ignore')
os.environ['PYGAME_HIDE_SUPPORT_PROMPT'] = '1'

# Suppress ALSA/JACK error messages
from ctypes import *
try:
    ERROR_HANDLER_FUNC = CFUNCTYPE(None, c_char_p, c_int, c_char_p, c_int, c_char_p)
    def py_error_handler(filename, line, function, err, fmt):
        pass
    c_error_handler = ERROR_HANDLER_FUNC(py_error_handler)
    asound = cdll.LoadLibrary('libasound.so.2')
    asound.snd_lib_error_set_handler(c_error_handler)
except:
    pass

import pyaudio
import wave
import struct
import numpy as np
import tempfile

try:
    import serial
    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False
    print("⚠️  pyserial tidak terinstall. Install dengan: pip install pyserial")

try:
    from faster_whisper import WhisperModel
    WHISPER_AVAILABLE = True
except ImportError:
    WHISPER_AVAILABLE = False
    print("⚠️  faster-whisper tidak terinstall. Install dengan: pip install faster-whisper")

try:
    import ollama
    OLLAMA_AVAILABLE = True
except ImportError:
    OLLAMA_AVAILABLE = False
    print("⚠️  ollama tidak terinstall. Install dengan: pip install ollama")

# ===== CONFIGURATION =====
RECORD_SECONDS = 3          # Durasi rekaman (pendek untuk responsif)
WHISPER_MODEL = "small"     # Model Whisper
AI_MODEL = "qwen2.5:0.5b"   # Model Ollama

# Whisper Settings
WHISPER_BEAM_SIZE = 3
WHISPER_BEST_OF = 2
WHISPER_PATIENCE = 1.0
WHISPER_TEMPERATURE = 0.0

# Audio Configuration (I2S GoogleVoiceHAT)
NATIVE_RATE = 48000
TARGET_RATE = 16000
CHANNELS = 2
CHUNK = 4096
FORMAT = pyaudio.paInt32

# ESP32 Serial Configuration
ESP32_BAUD_RATE = 115200
ESP32_TIMEOUT = 1

# ===== COMMAND MAPPINGS untuk ESP32 =====
# Format: keyword -> (ESP32_COMMAND, DESCRIPTION)
COMMAND_MAP = {
    # Maju
    "maju": ("VOICE,MAJU", "Maju ke depan"),
    "ke depan": ("VOICE,MAJU", "Maju ke depan"),
    "kedepan": ("VOICE,MAJU", "Maju ke depan"),
    "forward": ("VOICE,MAJU", "Maju ke depan"),
    "jalan": ("VOICE,MAJU", "Maju ke depan"),
    
    # Mundur
    "mundur": ("VOICE,MUNDUR", "Mundur ke belakang"),
    "kebelakang": ("VOICE,MUNDUR", "Mundur ke belakang"),
    "ke belakang": ("VOICE,MUNDUR", "Mundur ke belakang"),
    "backward": ("VOICE,MUNDUR", "Mundur ke belakang"),
    "back": ("VOICE,MUNDUR", "Mundur ke belakang"),
    
    # Kanan
    "kanan": ("VOICE,PUTARKANAN", "Putar ke kanan"),
    "belok kanan": ("VOICE,PUTARKANAN", "Belok ke kanan"),
    "putar kanan": ("VOICE,PUTARKANAN", "Putar ke kanan"),
    "right": ("VOICE,PUTARKANAN", "Putar ke kanan"),
    
    # Kiri
    "kiri": ("VOICE,PUTARKIRI", "Putar ke kiri"),
    "belok kiri": ("VOICE,PUTARKIRI", "Belok ke kiri"),
    "putar kiri": ("VOICE,PUTARKIRI", "Putar ke kiri"),
    "left": ("VOICE,PUTARKIRI", "Putar ke kiri"),
    
    # Berhenti
    "berhenti": ("VOICE,BERHENTI", "Berhenti"),
    "stop": ("VOICE,BERHENTI", "Berhenti"),
    "diam": ("VOICE,BERHENTI", "Berhenti"),
    "halt": ("VOICE,BERHENTI", "Berhenti"),
    
    # Pelan
    "pelan": ("VOICE,MAJUPELAN", "Maju pelan"),
    "lambat": ("VOICE,MAJUPELAN", "Maju pelan"),
    "slow": ("VOICE,MAJUPELAN", "Maju pelan"),
}


class ESP32Controller:
    """Handle ESP32 serial communication"""
    
    def __init__(self):
        self.serial = None
        self.connected = False
        self.port = None
        self.voice_mode_active = False
        self.last_command_time = 0
        self.command_timeout = 3  # ESP32 expects command every 3 seconds in voice mode
        
    def find_esp32_port(self):
        """Find ESP32 USB serial port"""
        patterns = [
            '/dev/ttyUSB*',
            '/dev/ttyACM*',
        ]
        
        ports = []
        for pattern in patterns:
            ports.extend(glob.glob(pattern))
        
        return sorted(ports)
    
    def connect(self):
        """Connect to ESP32"""
        if not SERIAL_AVAILABLE:
            print("❌ pyserial tidak tersedia")
            return False
            
        ports = self.find_esp32_port()
        
        if not ports:
            print("❌ ESP32 tidak ditemukan!")
            print("   Pastikan ESP32 terhubung via USB")
            return False
        
        print(f"📡 Ditemukan port: {ports}")
        
        for port in ports:
            try:
                self.serial = serial.Serial(
                    port=port,
                    baudrate=ESP32_BAUD_RATE,
                    timeout=ESP32_TIMEOUT
                )
                time.sleep(2)  # Wait for ESP32 reset
                
                # Test connection
                self.serial.write(b"NONE,TEST\n")
                self.port = port
                self.connected = True
                print(f"✅ Terhubung ke ESP32 di {port}")
                return True
                
            except Exception as e:
                print(f"⚠️ Gagal koneksi ke {port}: {e}")
                continue
        
        return False
    
    def send_command(self, command):
        """Send command to ESP32"""
        if not self.connected or not self.serial:
            print("⚠️ ESP32 tidak terhubung")
            return False
        
        try:
            # Format: "COLOR,DIRECTION\n"
            cmd = f"{command}\n"
            self.serial.write(cmd.encode())
            self.last_command_time = time.time()
            print(f"📤 Sent to ESP32: {command}")
            return True
        except Exception as e:
            print(f"❌ Error sending command: {e}")
            self.connected = False
            return False
    
    def activate_voice_mode(self):
        """Activate voice command mode on ESP32"""
        if self.send_command("NONE,VOICECOMMANDON"):
            self.voice_mode_active = True
            print("🎤 Voice mode AKTIF di ESP32")
            return True
        return False
    
    def deactivate_voice_mode(self):
        """Deactivate voice command mode on ESP32"""
        if self.send_command("NONE,VOICECOMMANDOFF"):
            self.voice_mode_active = False
            print("🔇 Voice mode NONAKTIF di ESP32")
            return True
        return False
    
    def read_response(self):
        """Read response from ESP32"""
        if not self.connected or not self.serial:
            return None
        
        try:
            if self.serial.in_waiting > 0:
                response = self.serial.readline().decode().strip()
                return response
        except:
            pass
        return None
    
    def disconnect(self):
        """Disconnect from ESP32"""
        if self.voice_mode_active:
            self.deactivate_voice_mode()
        
        if self.serial:
            self.serial.close()
            self.connected = False
            print("📴 Disconnected from ESP32")


class VoiceController:
    """Voice recognition and command processing"""
    
    def __init__(self, esp32_controller):
        self.esp32 = esp32_controller
        self.whisper_model = None
        self.audio = None
        self.device_index = None
        
    def init_audio(self):
        """Initialize PyAudio and find microphone"""
        print("\n🎤 Mencari microphone...")
        self.audio = pyaudio.PyAudio()
        
        for i in range(self.audio.get_device_count()):
            dev = self.audio.get_device_info_by_index(i)
            if dev['maxInputChannels'] > 0:
                name = dev['name'].lower()
                
                if any(k in name for k in ['i2s', 'googlevoicehat', 'snd_rpi']):
                    self.device_index = i
                    print(f"   ✅ Selected: I2S device [{i}] {dev['name']}")
                elif 'pulse' in name and self.device_index is None:
                    self.device_index = i
                elif 'default' in name and self.device_index is None:
                    self.device_index = i
        
        if self.device_index is None:
            for i in range(self.audio.get_device_count()):
                dev = self.audio.get_device_info_by_index(i)
                if dev['maxInputChannels'] > 0:
                    self.device_index = i
                    break
        
        if self.device_index is not None:
            dev = self.audio.get_device_info_by_index(self.device_index)
            print(f"✅ Menggunakan: [{self.device_index}] {dev['name']}")
            return True
        else:
            print("❌ Tidak ada microphone!")
            return False
    
    def init_whisper(self):
        """Initialize Whisper model"""
        if not WHISPER_AVAILABLE:
            return False
        
        print(f"\n🧠 Loading Whisper model '{WHISPER_MODEL}'...")
        
        try:
            self.whisper_model = WhisperModel(
                WHISPER_MODEL,
                device="cpu",
                compute_type="int8"
            )
            print("✅ Whisper model loaded!")
            return True
        except Exception as e:
            print(f"❌ Gagal load Whisper: {e}")
            return False
    
    def record_audio(self, duration=RECORD_SECONDS):
        """Record audio from microphone"""
        dev = self.audio.get_device_info_by_index(self.device_index)
        sample_rate = int(dev['defaultSampleRate'])
        channels = min(int(dev['maxInputChannels']), 2)
        
        print(f"\n🎤 Recording... ({duration} detik)")
        
        try:
            stream = self.audio.open(
                format=FORMAT,
                channels=channels,
                rate=sample_rate,
                input=True,
                input_device_index=self.device_index,
                frames_per_buffer=CHUNK
            )
            
            frames = []
            num_chunks = int(sample_rate / CHUNK * duration)
            
            for i in range(num_chunks):
                try:
                    data = stream.read(CHUNK, exception_on_overflow=False)
                    frames.append(data)
                    progress = int((i + 1) / num_chunks * 20)
                    print(f"\r   [{'█' * progress}{'░' * (20 - progress)}] {int((i+1)/num_chunks*100)}%", end='', flush=True)
                except Exception as e:
                    continue
            
            print()
            stream.stop_stream()
            stream.close()
            
            return self._convert_to_wav(frames, sample_rate, channels)
            
        except Exception as e:
            print(f"❌ Recording error: {e}")
            return None
    
    def _convert_to_wav(self, frames, sample_rate, channels):
        """Convert raw audio to WAV file"""
        raw_data = b''.join(frames)
        
        # Parse stereo 32-bit, extract LEFT channel
        samples_left = []
        bytes_per_frame = 4 * channels
        
        for i in range(0, len(raw_data), bytes_per_frame):
            if i + 4 <= len(raw_data):
                left = struct.unpack('<i', raw_data[i:i+4])[0]
                samples_left.append(left)
        
        arr = np.array(samples_left, dtype=np.float64)
        
        # Remove DC offset
        dc_offset = arr.mean()
        arr = arr - dc_offset
        
        # Check for noise
        signal_std = arr.std()
        if signal_std > 1000000000:
            print("   ❌ NOISE TERDETEKSI! Cek koneksi microphone.")
            return None
        
        # Normalize
        signal_max = max(abs(arr.max()), abs(arr.min()))
        if signal_max > 0:
            arr = arr / signal_max * 32000
        
        samples_16 = np.clip(arr, -32768, 32767).astype(np.int16)
        
        # Resample to 16kHz
        if sample_rate != TARGET_RATE:
            resample_ratio = TARGET_RATE / sample_rate
            new_length = int(len(samples_16) * resample_ratio)
            x_old = np.linspace(0, 1, len(samples_16))
            x_new = np.linspace(0, 1, new_length)
            samples_resampled = np.interp(x_new, x_old, samples_16.astype(np.float32))
            samples_16 = np.clip(samples_resampled, -32768, 32767).astype(np.int16)
        
        # Save to temp file
        temp_file = tempfile.mktemp(suffix='.wav')
        with wave.open(temp_file, 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(TARGET_RATE)
            wf.writeframes(samples_16.tobytes())
        
        return temp_file
    
    def transcribe(self, audio_file):
        """Transcribe audio to text"""
        if not self.whisper_model:
            return None
        
        try:
            segments, info = self.whisper_model.transcribe(
                audio_file,
                beam_size=WHISPER_BEAM_SIZE,
                best_of=WHISPER_BEST_OF,
                patience=WHISPER_PATIENCE,
                temperature=WHISPER_TEMPERATURE,
                language="id",
                vad_filter=True,
                vad_parameters=dict(
                    threshold=0.4,
                    min_silence_duration_ms=300,
                    speech_pad_ms=250,
                    min_speech_duration_ms=100
                ),
                word_timestamps=False,
                condition_on_previous_text=True,
                no_speech_threshold=0.5
            )
            
            text = " ".join([segment.text for segment in segments]).strip()
            
            try:
                os.remove(audio_file)
            except:
                pass
            
            return text if text else None
                
        except Exception as e:
            print(f"❌ Transcription error: {e}")
            return None
    
    def parse_command(self, text):
        """Parse voice command and return ESP32 command"""
        text_lower = text.lower().strip()
        
        # Check each command keyword
        for keyword, (esp32_cmd, description) in COMMAND_MAP.items():
            if keyword in text_lower:
                return esp32_cmd, description
        
        return None, None
    
    def process_voice_command(self, text):
        """Process transcribed text and send command to ESP32"""
        esp32_cmd, description = self.parse_command(text)
        
        if esp32_cmd:
            print(f"📡 Command: {description}")
            self.esp32.send_command(esp32_cmd)
            return True
        else:
            print("⚠️ Perintah tidak dikenali")
            return False
    
    def cleanup(self):
        """Cleanup resources"""
        if self.audio:
            self.audio.terminate()


def main():
    print("=" * 60)
    print("🤖 EVOBOT VOICE CONTROL - ESP32 Integration")
    print("=" * 60)
    print("""
┌────────────────────────────────────────────────────────────┐
│              PERINTAH YANG DIDUKUNG                        │
├────────────────────────────────────────────────────────────┤
│  🔹 "Maju" / "Ke depan"       → Robot maju                 │
│  🔹 "Mundur" / "Ke belakang"  → Robot mundur               │
│  🔹 "Kanan" / "Belok kanan"   → Putar ke kanan             │
│  🔹 "Kiri" / "Belok kiri"     → Putar ke kiri              │
│  🔹 "Berhenti" / "Stop"       → Robot berhenti             │
│  🔹 "Pelan" / "Lambat"        → Maju pelan                 │
└────────────────────────────────────────────────────────────┘
""")
    
    # Initialize ESP32 controller
    esp32 = ESP32Controller()
    
    # Try to connect to ESP32
    print("🔌 Mencari ESP32...")
    if esp32.connect():
        print("✅ ESP32 terhubung!")
    else:
        print("⚠️ ESP32 tidak terhubung. Mode simulasi aktif.")
    
    # Initialize voice controller
    voice = VoiceController(esp32)
    
    try:
        # Initialize audio
        if not voice.init_audio():
            print("❌ Gagal inisialisasi audio!")
            return
        
        # Initialize Whisper
        if not voice.init_whisper():
            print("❌ Gagal inisialisasi Whisper!")
            return
        
        # Activate voice mode on ESP32
        if esp32.connected:
            esp32.activate_voice_mode()
        
        print("\n" + "=" * 60)
        print("✅ SISTEM SIAP!")
        print("=" * 60)
        print("\nTekan ENTER untuk mulai recording, atau 'q' untuk keluar.")
        print(f"Durasi recording: {RECORD_SECONDS} detik per sesi.\n")
        
        while True:
            try:
                user_input = input("\n👉 Tekan ENTER untuk bicara (atau 'q' untuk keluar): ").strip().lower()
                
                if user_input == 'q':
                    print("\n👋 Sampai jumpa!")
                    break
                
                # Record audio
                audio_file = voice.record_audio(duration=RECORD_SECONDS)
                if not audio_file:
                    print("❌ Gagal merekam audio")
                    continue
                
                # Transcribe to text
                print("📝 Transcribing...")
                text = voice.transcribe(audio_file)
                
                if not text:
                    print("⚠️ Tidak ada suara terdeteksi. Coba lagi.")
                    continue
                
                print("\n" + "-" * 40)
                print(f"👤 ANDA: {text}")
                print("-" * 40)
                
                # Parse and send command
                voice.process_voice_command(text)
                
                # Read ESP32 response
                if esp32.connected:
                    response = esp32.read_response()
                    if response:
                        print(f"📥 ESP32: {response}")
                
            except KeyboardInterrupt:
                print("\n\n👋 Dihentikan oleh user.")
                break
                
    finally:
        # Cleanup
        if esp32.connected:
            esp32.deactivate_voice_mode()
        esp32.disconnect()
        voice.cleanup()
        print("\n✅ Cleanup selesai.")


if __name__ == "__main__":
    main()
