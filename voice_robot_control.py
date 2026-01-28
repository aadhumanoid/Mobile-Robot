#!/usr/bin/env python3
"""
EVOBOT Voice Control System - ESP32 Integration
=================================================
Voice Recognition → ESP32 USB Serial Control

Flow:
1. User bicara "VOICECOMMANDON" → Aktifkan voice mode di ESP32
2. User bicara perintah (MAJU, MUNDUR, dll) → Kirim ke ESP32
3. User bicara "VOICECOMMANDOFF" → Matikan voice mode

Format ke ESP32: "VOICE,COMMAND\n"
Contoh: "VOICE,MAJU\n", "VOICE,VOICECOMMANDON\n"

Usage:
    python3 voice_robot_control.py
"""

import os
import sys
import warnings
import glob
import re

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
import time
import serial

# Check dependencies
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
    print("⚠️  ollama tidak terinstall (optional)")

# ===== CONFIGURATION =====
RECORD_SECONDS = 4          # Durasi rekaman
WHISPER_MODEL = "small"     # Model Whisper
AI_MODEL = "qwen2.5:0.5b"   # Model Ollama (optional)

# Whisper Settings
WHISPER_BEAM_SIZE = 3
WHISPER_BEST_OF = 2
WHISPER_TEMPERATURE = 0.0

# Audio Configuration
NATIVE_RATE = 48000
TARGET_RATE = 16000
CHANNELS = 2
CHUNK = 4096
FORMAT = pyaudio.paInt32

# ESP32 USB Serial Configuration
ESP32_BAUD_RATE = 115200
ESP32_TIMEOUT = 1

# ===== VOICE COMMANDS (semua UPPERCASE) =====
# Mapping: kata yang mungkin diucapkan → command yang dikirim ke ESP32
VOICE_COMMANDS = {
    # Aktivasi voice mode
    "VOICECOMMANDON": "VOICECOMMANDON",
    "VOICE COMMAND ON": "VOICECOMMANDON",
    "VOICE ON": "VOICECOMMANDON",
    "AKTIFKAN VOICE": "VOICECOMMANDON",
    "AKTIFKAN SUARA": "VOICECOMMANDON",
    "MULAI VOICE": "VOICECOMMANDON",
    "MULAI": "VOICECOMMANDON",
    
    # Deaktivasi voice mode  
    "VOICECOMMANDOFF": "VOICECOMMANDOFF",
    "VOICE COMMAND OFF": "VOICECOMMANDOFF",
    "VOICE OFF": "VOICECOMMANDOFF",
    "MATIKAN VOICE": "VOICECOMMANDOFF",
    "MATIKAN SUARA": "VOICECOMMANDOFF",
    "SELESAI": "VOICECOMMANDOFF",
    "BERHENTI VOICE": "VOICECOMMANDOFF",
    
    # Gerakan - MAJU
    "MAJU": "MAJU",
    "MAJU KEDEPAN": "MAJU",
    "KEDEPAN": "MAJU",
    "KE DEPAN": "MAJU",
    "FORWARD": "MAJU",
    "JALAN": "MAJU",
    "JALAN MAJU": "MAJU",
    
    # Gerakan - MUNDUR
    "MUNDUR": "MUNDUR",
    "MUNDUR KEBELAKANG": "MUNDUR",
    "KEBELAKANG": "MUNDUR",
    "KE BELAKANG": "MUNDUR",
    "BACKWARD": "MUNDUR",
    "BACK": "MUNDUR",
    
    # Gerakan - PUTAR KANAN
    "PUTARKANAN": "PUTARKANAN",
    "PUTAR KANAN": "PUTARKANAN",
    "BELOK KANAN": "PUTARKANAN",
    "KANAN": "PUTARKANAN",
    "RIGHT": "PUTARKANAN",
    "KE KANAN": "PUTARKANAN",
    
    # Gerakan - PUTAR KIRI
    "PUTARKIRI": "PUTARKIRI",
    "PUTAR KIRI": "PUTARKIRI",
    "BELOK KIRI": "PUTARKIRI",
    "KIRI": "PUTARKIRI",
    "LEFT": "PUTARKIRI",
    "KE KIRI": "PUTARKIRI",
    
    # Gerakan - MAJU PELAN
    "MAJUPELAN": "MAJUPELAN",
    "MAJU PELAN": "MAJUPELAN",
    "PELAN": "MAJUPELAN",
    "LAMBAT": "MAJUPELAN",
    "SLOW": "MAJUPELAN",
    "PELAN PELAN": "MAJUPELAN",
    
    # Gerakan - BERHENTI
    "BERHENTI": "BERHENTI",
    "STOP": "BERHENTI",
    "DIAM": "BERHENTI",
    "HALT": "BERHENTI",
    "BRENTI": "BERHENTI",
}


class VoiceRobotControl:
    def __init__(self):
        self.whisper_model = None
        self.audio = None
        self.device_index = None
        
        # ESP32 Serial
        self.esp32_serial = None
        self.esp32_connected = False
        
        # Voice Mode State
        self.voice_mode_active = False
        
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
    
    def init_esp32(self):
        """Initialize ESP32 USB Serial connection"""
        print("\n🔌 Mencari ESP32...")
        
        ports = self.find_esp32_port()
        
        if not ports:
            print("⚠️  Tidak ada ESP32 ditemukan via USB!")
            print("   Mode simulasi: command akan ditampilkan tapi tidak dikirim.")
            return False
        
        print(f"   Ditemukan port: {ports}")
        
        for port in ports:
            try:
                self.esp32_serial = serial.Serial(
                    port=port,
                    baudrate=ESP32_BAUD_RATE,
                    timeout=ESP32_TIMEOUT
                )
                time.sleep(2)  # Wait for ESP32 to reset
                
                print(f"✅ ESP32 terhubung di {port}")
                self.esp32_connected = True
                return True
                
            except Exception as e:
                print(f"   ❌ Gagal konek ke {port}: {e}")
                continue
        
        print("⚠️  Gagal terhubung ke ESP32. Mode simulasi aktif.")
        return False
    
    def send_to_esp32(self, command):
        """Send command to ESP32 via USB Serial
        Format: VOICE,COMMAND\n
        """
        message = f"VOICE,{command}\n"
        
        if self.esp32_connected and self.esp32_serial:
            try:
                self.esp32_serial.write(message.encode('utf-8'))
                print(f"   📡 SENT TO ESP32: {message.strip()}")
                return True
            except Exception as e:
                print(f"   ❌ Gagal kirim ke ESP32: {e}")
                return False
        else:
            print(f"   📡 [SIMULATED] → {message.strip()}")
            return True
    
    def init_audio(self):
        """Initialize PyAudio dan find microphone"""
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
            print("❌ Tidak ada microphone yang ditemukan!")
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
        """Record audio dari microphone"""
        dev = self.audio.get_device_info_by_index(self.device_index)
        sample_rate = int(dev['defaultSampleRate'])
        channels = min(int(dev['maxInputChannels']), 2)
        
        print(f"\n🎤 Recording... ({duration} detik)")
        print("   Silakan bicara sekarang!")
        
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
            
            print("✅ Recording selesai!")
            return self._convert_to_wav(frames, sample_rate, channels)
            
        except Exception as e:
            print(f"❌ Recording error: {e}")
            return None
    
    def _convert_to_wav(self, frames, sample_rate, channels):
        """Convert raw audio to WAV file"""
        print("🔄 Converting audio...")
        
        raw_data = b''.join(frames)
        
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
        
        # Check signal level
        signal_std = arr.std()
        
        # NOISE DETECTION
        if signal_std > 1000000000:
            print("   ❌ NOISE TERDETEKSI! Cek koneksi microphone.")
            return None
        
        if signal_std < 100000:
            print("   ⚠️ Signal sangat lemah!")
        
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
        
        print(f"✅ Audio converted: {len(samples_16)} samples")
        return temp_file
    
    def transcribe(self, audio_file):
        """Transcribe audio to text using Whisper"""
        if not self.whisper_model:
            return None
        
        print("\n📝 Transcribing...")
        
        try:
            segments, info = self.whisper_model.transcribe(
                audio_file,
                beam_size=WHISPER_BEAM_SIZE,
                best_of=WHISPER_BEST_OF,
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
                no_speech_threshold=0.5
            )
            
            text = " ".join([segment.text for segment in segments]).strip()
            
            # Cleanup temp file
            try:
                os.remove(audio_file)
            except:
                pass
            
            if text:
                # CONVERT TO UPPERCASE
                text = text.upper()
                print(f"✅ Hasil: \"{text}\"")
                return text
            else:
                print("⚠️ Tidak ada suara terdeteksi")
                return None
                
        except Exception as e:
            print(f"❌ Transcription error: {e}")
            return None
    
    def parse_command(self, text):
        """Parse voice text to ESP32 command
        Returns: (command, is_activation, is_deactivation)
        """
        if not text:
            return None, False, False
        
        text = text.upper().strip()
        
        # Cek exact match dulu
        if text in VOICE_COMMANDS:
            cmd = VOICE_COMMANDS[text]
            is_on = cmd == "VOICECOMMANDON"
            is_off = cmd == "VOICECOMMANDOFF"
            return cmd, is_on, is_off
        
        # Cek partial match
        for phrase, cmd in VOICE_COMMANDS.items():
            if phrase in text:
                is_on = cmd == "VOICECOMMANDON"
                is_off = cmd == "VOICECOMMANDOFF"
                return cmd, is_on, is_off
        
        return None, False, False
    
    def ask_ai(self, text, command):
        """Optional: Get AI response for the command"""
        if not OLLAMA_AVAILABLE:
            return None
        
        try:
            response = ollama.chat(
                model=AI_MODEL,
                messages=[
                    {
                        'role': 'system',
                        'content': '''Kamu adalah robot EVOBOT. Jawab SINGKAT (1 kalimat) untuk konfirmasi perintah.
Contoh:
- "MAJU" → "Baik, saya akan maju."
- "MUNDUR" → "Baik, mundur."
- "PUTARKANAN" → "Memutar ke kanan."
- "BERHENTI" → "Berhenti."
- "VOICECOMMANDON" → "Mode suara aktif, silakan berikan perintah."
- "VOICECOMMANDOFF" → "Mode suara dimatikan."'''
                    },
                    {
                        'role': 'user',
                        'content': f"Perintah: {command}"
                    }
                ],
                options={
                    'temperature': 0.3,
                    'num_predict': 50
                }
            )
            return response['message']['content']
        except:
            return None
    
    def cleanup(self):
        """Cleanup resources"""
        if self.esp32_serial:
            # Send stop command before closing
            try:
                self.esp32_serial.write(b"VOICE,BERHENTI\n")
                self.esp32_serial.close()
            except:
                pass
        
        if self.audio:
            self.audio.terminate()


def main():
    print("=" * 60)
    print("🤖 EVOBOT VOICE CONTROL - ESP32 Integration")
    print("=" * 60)
    print("""
┌────────────────────────────────────────────────────────────┐
│                    CARA PENGGUNAAN                         │
├────────────────────────────────────────────────────────────┤
│  1. Bicara "VOICECOMMANDON" untuk mengaktifkan             │
│  2. Berikan perintah gerakan:                              │
│     • "MAJU"       - Robot maju                            │
│     • "MUNDUR"     - Robot mundur                          │
│     • "PUTARKANAN" - Putar ke kanan                        │
│     • "PUTARKIRI"  - Putar ke kiri                         │
│     • "MAJUPELAN"  - Maju pelan                            │
│     • "BERHENTI"   - Robot berhenti                        │
│  3. Bicara "VOICECOMMANDOFF" untuk mematikan               │
└────────────────────────────────────────────────────────────┘

Format ke ESP32: VOICE,COMMAND
Contoh: VOICE,MAJU atau VOICE,VOICECOMMANDON
""")
    
    controller = VoiceRobotControl()
    
    try:
        # Initialize ESP32
        controller.init_esp32()
        
        # Initialize audio
        if not controller.init_audio():
            print("❌ Gagal inisialisasi audio!")
            return
        
        # Initialize Whisper
        if not controller.init_whisper():
            print("❌ Gagal inisialisasi Whisper!")
            return
        
        print("\n" + "=" * 60)
        print("✅ SISTEM SIAP!")
        print("=" * 60)
        
        if controller.voice_mode_active:
            print("🟢 Voice Mode: AKTIF")
        else:
            print("🔴 Voice Mode: TIDAK AKTIF")
            print("   Bicara 'VOICECOMMANDON' untuk mengaktifkan")
        
        print("\nTekan ENTER untuk recording, atau 'q' untuk keluar.\n")
        
        while True:
            try:
                # Show current state
                if controller.voice_mode_active:
                    state = "🟢 AKTIF"
                else:
                    state = "🔴 NONAKTIF"
                
                user_input = input(f"\n[{state}] 👉 Tekan ENTER untuk bicara: ").strip().lower()
                
                if user_input == 'q':
                    print("\n👋 Sampai jumpa!")
                    # Send stop and deactivate
                    controller.send_to_esp32("BERHENTI")
                    controller.send_to_esp32("VOICECOMMANDOFF")
                    break
                
                # Record audio
                audio_file = controller.record_audio(duration=4)
                if not audio_file:
                    print("❌ Gagal merekam audio")
                    continue
                
                # Transcribe to text
                text = controller.transcribe(audio_file)
                if not text:
                    print("⚠️ Tidak ada teks yang terdeteksi. Coba lagi.")
                    continue
                
                print("\n" + "-" * 50)
                print(f"🎤 TERDETEKSI: {text}")
                print("-" * 50)
                
                # Parse command
                command, is_activation, is_deactivation = controller.parse_command(text)
                
                if command:
                    print(f"📡 COMMAND: {command}")
                    
                    # Handle activation/deactivation
                    if is_activation:
                        controller.voice_mode_active = True
                        controller.send_to_esp32(command)
                        print("🟢 Voice Mode DIAKTIFKAN!")
                        
                    elif is_deactivation:
                        controller.voice_mode_active = False
                        controller.send_to_esp32("BERHENTI")  # Stop motors first
                        controller.send_to_esp32(command)
                        print("🔴 Voice Mode DIMATIKAN!")
                        
                    else:
                        # Movement command
                        if controller.voice_mode_active:
                            controller.send_to_esp32(command)
                            print(f"✅ Perintah '{command}' dikirim ke ESP32")
                        else:
                            print("⚠️ Voice Mode belum aktif!")
                            print("   Ucapkan 'VOICECOMMANDON' terlebih dahulu.")
                    
                    # Optional AI response
                    ai_response = controller.ask_ai(text, command)
                    if ai_response:
                        print(f"\n🤖 AI: {ai_response}")
                else:
                    print("⚠️ Perintah tidak dikenali.")
                    print("   Perintah valid: MAJU, MUNDUR, PUTARKANAN, PUTARKIRI, BERHENTI, MAJUPELAN")
                
            except KeyboardInterrupt:
                print("\n\n👋 Dihentikan oleh user.")
                controller.send_to_esp32("BERHENTI")
                break
                
    finally:
        controller.cleanup()
        print("\n✅ Cleanup selesai.")


if __name__ == "__main__":
    main()
