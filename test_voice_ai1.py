#!/usr/bin/env python3
"""
EVOBOT Voice Control System - ESP32 Integration
=================================================
ESP32 Triggered Single Session Voice Recognition

Flow:
1. Aplikasi → ESP32: "VoiceON"
2. ESP32 → Raspberry Pi 5: "VoiceON" 
3. Raspberry Pi 5: Recording 4 detik + transcribe + send command
4. Raspberry Pi 5 → ESP32: "VOICE,COMMAND" + "VoiceOFF"
5. ESP32 → Aplikasi: "VoiceOFF" (matikan indikator)

Format dari ESP32: "VoiceON"
Format ke ESP32: "VOICE,COMMAND\n" + "VoiceOFF\n"

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
        Format: VOICE,COMMAND\n untuk voice commands
                VoiceOFF\n untuk control signal
        """
        if command == "VoiceOFF":
            message = f"{command}\n"  # VoiceOFF tanpa prefix
        else:
            message = f"VOICE,{command}\n"  # Voice commands dengan prefix
        
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
    
    def read_from_esp32(self):
        """Read command from ESP32 via USB Serial
        Returns: command string or None
        """
        if self.esp32_connected and self.esp32_serial:
            try:
                if self.esp32_serial.in_waiting > 0:
                    line = self.esp32_serial.readline().decode('utf-8').strip()
                    if line:
                        print(f"   📥 RECEIVED FROM ESP32: {line}")
                        return line
            except Exception as e:
                print(f"   ❌ Gagal baca dari ESP32: {e}")
        return None
    
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
        Returns: command string or None
        """
        if not text:
            return None
        
        text = text.upper().strip()
        
        # Cek exact match dulu
        if text in VOICE_COMMANDS:
            return VOICE_COMMANDS[text]
        
        # Cek partial match
        for phrase, cmd in VOICE_COMMANDS.items():
            if phrase in text:
                return cmd
        
        return None
    
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
    print()
    print("+------------------------------------------------------------+")
    print("|                    CARA PENGGUNAAN                         |")
    print("+------------------------------------------------------------+")
    print("|  1. Aplikasi tekan tombol -> ESP32 kirim \"VoiceON\"        |")
    print("|  2. Raspberry Pi 5 mulai recording 4 detik                |")
    print("|  3. User bicara perintah selama recording:                |")
    print("|     * \"VOICECOMMANDON\" - Aktifkan robot                   |")
    print("|     * \"MAJU\"       - Robot maju                           |")
    print("|     * \"MUNDUR\"     - Robot mundur                         |")
    print("|     * \"PUTARKANAN\" - Putar ke kanan                       |")
    print("|     * \"PUTARKIRI\"  - Putar ke kiri                        |")
    print("|     * \"MAJUPELAN\"  - Maju pelan                           |")
    print("|     * \"BERHENTI\"   - Robot berhenti                       |")
    print("|     * \"VOICECOMMANDOFF\" - Matikan robot                   |")
    print("|  4. Setelah 4 detik, otomatis kirim command + \"VoiceOFF\"  |")
    print("|  5. ESP32 teruskan \"VoiceOFF\" -> Aplikasi matikan indikator|")
    print("+------------------------------------------------------------+")
    print()
    print("Workflow: Aplikasi -> ESP32 -> Raspberry Pi 5 -> ESP32 -> Aplikasi")
    print("Single Session: 1 tombol = 1x recording 4 detik")
    print()
    
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
        print("\n🎧 Menunggu signal dari ESP32...")
        print("   - 'VoiceON'  → Mulai recording")
        print("   - Setelah recording, langsung bicara perintah:")
        print("     * MAJU, MUNDUR, KANAN, KIRI, BERHENTI")
        print("\nTekan Ctrl+C untuk keluar.\n")
        
        recording_mode = False
        
        while True:
            try:
                # Read command from ESP32
                esp32_command = controller.read_from_esp32()
                
                if esp32_command:
                    if esp32_command == "VoiceON":
                        print("\n🟢 ESP32 SIGNAL: VoiceON - Mulai recording")
                        recording_mode = True
                        
                    elif esp32_command == "VoiceOFF":
                        print("\n🔴 ESP32 SIGNAL: VoiceOFF - Stop")
                        recording_mode = False
                        controller.send_to_esp32("BERHENTI")
                        print("   Kembali ke mode menunggu...")
                        continue
                
                # Jika dalam recording mode, lakukan recording
                if recording_mode:
                    # Record audio
                    print("\n🎤 Recording dimulai...")
                    audio_file = controller.record_audio(duration=4)
                    if not audio_file:
                        print("❌ Gagal merekam audio")
                        # Send VoiceOFF to ESP32 and exit recording mode
                        controller.send_to_esp32("VoiceOFF")
                        recording_mode = False
                        continue
                    
                    # Transcribe to text
                    text = controller.transcribe(audio_file)
                    if not text:
                        print("⚠️ Tidak ada teks terdeteksi")
                        controller.send_to_esp32("VoiceOFF")
                        recording_mode = False
                        continue
                    
                    print("\n" + "-" * 50)
                    print(f"🎤 TERDETEKSI: {text}")
                    print("-" * 50)
                
                    # Parse command
                    command = controller.parse_command(text)
                    
                    if command:
                        print(f"📡 COMMAND: {command}")
                        
                        # Langsung kirim command ke ESP32 (tanpa cek voice_mode_active)
                        controller.send_to_esp32(command)
                        print(f"✅ Perintah '{command}' dikirim ke ESP32")
                        
                        # Optional AI response
                        ai_response = controller.ask_ai(text, command)
                        if ai_response:
                            print(f"\n🤖 AI: {ai_response}")
                    else:
                        print("⚠️ Perintah tidak dikenali.")
                        print("   Perintah valid: MAJU, MUNDUR, PUTARKANAN, PUTARKIRI, BERHENTI, MAJUPELAN")
                    
                    # Selesai recording, kirim VoiceOFF dan kembali ke mode menunggu
                    controller.send_to_esp32("VoiceOFF")
                    recording_mode = False
                    print("\n🎧 Menunggu signal VoiceON berikutnya...\n")
                
                else:
                    # Not in recording mode, just wait for ESP32 signal
                    time.sleep(0.1)
                
            except KeyboardInterrupt:
                print("\n\n👋 Dihentikan oleh user.")
                controller.send_to_esp32("BERHENTI")
                break
                
    finally:
        controller.cleanup()
        print("\n✅ Cleanup selesai.")


if __name__ == "__main__":
    main()