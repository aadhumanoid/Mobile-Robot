# Robot Kendali Suara - ESP32 & Raspberry Pi 5

Sistem kendali robot menggunakan perintah suara dalam bahasa Indonesia. Robot dapat dikendalikan melalui aplikasi mobile yang berkomunikasi dengan ESP32, kemudian ESP32 meneruskan signal ke Raspberry Pi 5 untuk pemrosesan suara.

## Cara Kerja

```
Aplikasi Mobile → ESP32 → Raspberry Pi 5 → ESP32 → Robot
     (tombol)   VoiceON   (rekam 4 detik)  VoiceOFF  (gerak)
```

## Instalasi Cepat

```bash
# Download dan setup
git clone https://github.com/aadhumanoid/Mobile-Robot.git
cd Mobile-Robot

# Instalasi otomatis (10-15 menit)
./setup.sh

# Jalankan robot
./run_robot.sh
```

## Hardware yang Dibutuhkan

### Raspberry Pi 5
- Raspberry Pi 5 (4GB RAM minimum)
- MicroSD 32GB Class 10
- Microphone USB
- Speaker atau headphone

### ESP32 Robot
- ESP32 development board
- Motor driver (L298N)
- Motor DC dan chassis robot
- Kabel USB untuk koneksi ke Raspberry Pi

## Koneksi Hardware

**USB (Direkomendasikan)**
```
ESP32 ←── Kabel USB ──→ Raspberry Pi 5
```

**GPIO UART (Opsional)**
```
Raspberry Pi 5    ESP32
Pin 8 (GPIO 14)   → RX
Pin 10 (GPIO 15)  ← TX  
Pin 6 (GND)       ← GND
```

## Perintah Suara

### Aktivasi Sistem
- **"VOICECOMMANDON"** - Aktifkan mode suara
- **"VOICECOMMANDOFF"** - Matikan mode suara

### Perintah Gerak
- **"MAJU"** - Robot maju
- **"MUNDUR"** - Robot mundur  
- **"PUTARKANAN"** - Putar ke kanan
- **"PUTARKIRI"** - Putar ke kiri
- **"MAJUPELAN"** - Maju dengan kecepatan pelan
- **"BERHENTI"** - Stop semua gerakan

## Cara Penggunaan

1. **Persiapan**: Jalankan `./setup.sh` untuk instalasi awal
2. **Koneksi**: Hubungkan ESP32 ke Raspberry Pi via USB
3. **Upload**: Upload kode ESP32 menggunakan Arduino IDE  
4. **Jalankan**: Eksekusi `./run_robot.sh`
5. **Aplikasi**: Tekan tombol di aplikasi mobile
6. **Bicara**: Ucapkan perintah dalam 4 detik
7. **Eksekusi**: Robot akan melaksanakan perintah

## Struktur File

```
├── setup.sh               # Script instalasi lengkap
├── test_voice_ai.py        # Program utama kendali suara
├── requirements_raspy.txt  # Dependensi Python
└── README.md              # Dokumentasi
```

## Troubleshooting

### Masalah Audio
```bash
# Test microphone
arecord -l

# Test speaker
speaker-test -c 2
```

### Koneksi Serial
```bash
# Cek port ESP32
ls /dev/ttyUSB*
ls /dev/ttyACM*

# Test koneksi
./test_uart.sh
```

### Dependensi Python
```bash
# Aktivasi virtual environment
source venv/bin/activate

# Install ulang dependensi
pip install -r requirements_raspy.txt
```

## Performa

- **Waktu setup**: 10-15 menit
- **Respon suara**: 2-4 detik
- **Penggunaan RAM**: 1-2GB
- **Akurasi**: 85-90% (kondisi normal)

## Dukungan

Untuk masalah teknis:
1. Periksa koneksi hardware
2. Cek log error di terminal
3. Test komponen secara terpisah
4. Pastikan ESP32 sudah upload kode yang benar

---

**Siap membangun robot kendali suara Anda!** 🤖
ESP32> speed 75       # Set speed 75%
ESP32> status         # Check robot status
```

## 📁 File Structure

```
├── quick_start.sh              # Quick start guide
├── setup_raspy.sh              # Main setup script
├── run_raspy.sh                # Application launcher
├── main_raspy.py               # Main application
├── text_to_voice_raspy.py      # TTS module
├── serial_monitor.py           # Serial monitoring tool
├── setup_gpio_uart.sh          # GPIO UART setup
├── test_uart.sh                # UART testing
├── monitor_esp32.sh            # ESP32 monitor launcher
├── ESP32_Robot_RaspyPi5.ino    # Arduino code for ESP32
├── requirements_raspy.txt      # Python dependencies
└── README.md                   # This documentation
```

## 🔧 Troubleshooting

### Audio Issues
```bash
# Test audio output
speaker-test -t sine -f 1000 -l 1

# Check/restart audio services
sudo systemctl restart alsa-state
systemctl --user restart pulseaudio
```

### Serial Connection
```bash
# List ports
ls -la /dev/tty*

# Check permissions  
groups $USER  # Should include: dialout, audio

# Add permissions
sudo usermod -a -G dialout,audio $USER
# Logout & login again
```

### Ollama Issues
```bash
# Check/restart Ollama
sudo systemctl status ollama
sudo systemctl restart ollama
ollama list
```

### ESP32 Debug
```bash
# Stop main app first
pkill -f main_raspy.py

# Use serial monitor
./monitor_esp32.sh

# Or Arduino IDE Serial Monitor
```

## 🚦 Status Indicators

**GPIO LEDs (if connected):**
- **GPIO 18** - System active (ON/OFF)
- **GPIO 19** - Ready for commands (ON/OFF)

**Serial Messages:**
```
📡 PERINTAH DIKIRIM KE ESP32: FORWARD,5,meter
📥 RESPONS DARI ESP32: ✅ FORWARD command executed
```

## ⚙️ Performance Optimization

### Raspberry Pi 5 Specific
```bash
# Performance mode
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Memory optimization
sudo nano /etc/dphys-swapfile  # CONF_SWAPSIZE=2048
sudo dphys-swapfile setup && sudo dphys-swapfile swapon
```

### Configuration Tuning
Edit `main_raspy.py`:
```python
WHISPER_SIZE = "small"    # or "base" for better accuracy
AI_MODEL = "qwen2.5:1.5b" # or larger model if RAM allows
```

## 🔄 Development

### Custom Voice Commands
Edit `SYSTEM_PROMPT` in `main_raspy.py`:
```python
# Add new commands:
- DANCE = robot dance mode
- PATROL = autonomous patrol
```

### Multiple Robots
```python
# Support multiple ESP32s
SERIAL_PORTS = ['/dev/ttyUSB0', '/dev/ttyUSB1']
```

## 📊 Expected Performance

- **Setup time:** 10-15 minutes
- **Boot time:** 30-45 seconds  
- **Voice response:** 2-5 seconds
- **Memory usage:** ~1-2GB
- **AI processing:** ~2-3 seconds

## 🆘 Support

1. **Check logs:** `journalctl -u ollama -f`
2. **Test components individually**
3. **Verify hardware connections**
4. **Check troubleshooting section above**

## 📄 License

Educational and development use. Modify as needed.

---

**Ready to build your voice-controlled robot! 🤖🚀**

For detailed setup instructions, run: `./quick_start.sh`