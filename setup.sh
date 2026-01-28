#!/bin/bash
# ========================================
# Script Setup Lengkap - Robot Kendali Suara
# Raspberry Pi 5 - Instalasi Satu Perintah
# ========================================

echo "=============================================="
echo "🤖 AI Voice Robot Setup - Raspberry Pi 5"
echo "=============================================="
echo ""

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function untuk print dengan warna
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check apakah running sebagai root
if [[ $EUID -eq 0 ]]; then
   print_error "Script ini tidak boleh dijalankan sebagai root!"
   echo "Jalankan dengan: ./setup.sh"
   exit 1
fi

# [1] Check Raspberry Pi 5
print_info "Checking Raspberry Pi model..."
if [ -f /proc/device-tree/model ]; then
    PI_MODEL=$(cat /proc/device-tree/model)
    echo "   Detected: $PI_MODEL"
    
    if [[ "$PI_MODEL" == *"Raspberry Pi 5"* ]]; then
        print_success "Raspberry Pi 5 confirmed"
    else
        print_warning "Not Raspberry Pi 5, but continuing..."
    fi
else
    print_warning "Cannot detect Pi model, continuing..."
fi

echo ""

# [2] Update System
print_info "Updating system packages..."
sudo apt update && sudo apt upgrade -y

# [3] Install Python and Dependencies
print_info "Installing Python and system dependencies..."
sudo apt install -y \
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    portaudio19-dev \
    libasound2-dev \
    libportaudio2 \
    libportaudiocpp0 \
    ffmpeg \
    git \
    curl \
    wget \
    build-essential \
    cmake

# [4] Create Virtual Environment
print_info "Creating Python virtual environment..."
if [ -d "venv" ]; then
    print_warning "Virtual environment exists, removing old one..."
    rm -rf venv
fi

python3 -m venv venv
source venv/bin/activate
print_success "Virtual environment created and activated"

# [5] Install Python Packages
print_info "Installing Python packages..."
pip install --upgrade pip

# Install requirements from requirements_raspy.txt if exists
if [ -f "requirements_raspy.txt" ]; then
    print_info "Installing from requirements_raspy.txt..."
    pip install -r requirements_raspy.txt
else
    print_info "Installing essential packages manually..."
    pip install \
        pyaudio \
        numpy \
        faster-whisper \
        pyserial \
        ollama
fi

# [6] Test Audio System
print_info "Testing audio system..."
python3 -c "
import pyaudio
import sys

print('Testing PyAudio...')
try:
    p = pyaudio.PyAudio()
    device_count = p.get_device_count()
    print(f'Found {device_count} audio devices')
    
    input_devices = 0
    for i in range(device_count):
        dev = p.get_device_info_by_index(i)
        if dev['maxInputChannels'] > 0:
            input_devices += 1
            print(f'  Input Device [{i}]: {dev[\"name\"]}')
    
    if input_devices > 0:
        print('✅ Audio input devices found')
    else:
        print('⚠️  No audio input devices found')
    
    p.terminate()
    
except Exception as e:
    print(f'❌ PyAudio test failed: {e}')
    sys.exit(1)
"

# [7] Setup GPIO UART (Optional)
echo ""
read -p "🔧 Setup GPIO UART for ESP32 communication? [y/N]: " setup_uart
if [[ "$setup_uart" =~ ^[Yy]$ ]]; then
    print_info "Setting up GPIO UART..."
    
    # Enable UART in config
    if ! grep -q "enable_uart=1" /boot/firmware/config.txt; then
        echo "enable_uart=1" | sudo tee -a /boot/firmware/config.txt
        print_success "UART enabled in config.txt"
    fi
    
    # Disable serial console
    sudo raspi-config nonint do_serial 1
    sudo raspi-config nonint set_config_var enable_uart 1 /boot/firmware/config.txt
    
    print_success "GPIO UART configured"
    print_warning "Reboot required for UART changes to take effect"
fi

# [8] Test Dependencies
print_info "Testing all dependencies..."

# Test faster-whisper
print_info "Testing faster-whisper..."
python3 -c "
try:
    from faster_whisper import WhisperModel
    print('✅ faster-whisper OK')
except ImportError as e:
    print(f'❌ faster-whisper failed: {e}')
"

# Test serial
print_info "Testing pyserial..."
python3 -c "
try:
    import serial
    print('✅ pyserial OK')
except ImportError as e:
    print(f'❌ pyserial failed: {e}')
"

# Test main script
if [ -f "test_voice_ai.py" ]; then
    print_info "Testing main script syntax..."
    python3 -m py_compile test_voice_ai.py
    if [ $? -eq 0 ]; then
        print_success "test_voice_ai.py syntax OK"
    else
        print_error "test_voice_ai.py has syntax errors"
    fi
fi

# [9] Create Run Script
print_info "Creating run script..."
cat > run_robot.sh << 'EOF'
#!/bin/bash

echo "🤖 Starting AI Voice Robot..."

# Check if virtual environment exists
if [ ! -d "venv" ]; then
    echo "❌ Virtual environment not found!"
    echo "   Run ./setup.sh first"
    exit 1
fi

# Activate virtual environment
source venv/bin/activate

# Check if main script exists
if [ ! -f "test_voice_ai.py" ]; then
    echo "❌ test_voice_ai.py not found!"
    exit 1
fi

echo "✅ Starting voice control system..."
python3 test_voice_ai.py
EOF

chmod +x run_robot.sh
print_success "Run script created: ./run_robot.sh"

# [10] Create Test UART Script
print_info "Creating UART test script..."
cat > test_uart.sh << 'EOF'
#!/bin/bash

echo "🔌 Testing UART Communication..."

# Check if device exists
if [ -e /dev/ttyAMA0 ]; then
    echo "✅ /dev/ttyAMA0 found"
elif [ -e /dev/serial0 ]; then
    echo "✅ /dev/serial0 found"  
else
    echo "❌ No UART device found"
    echo "   Try USB connection instead"
    exit 1
fi

# Test USB serial devices
echo ""
echo "📋 Available USB serial devices:"
ls -la /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "   No USB serial devices found"

echo ""
echo "🔧 To test communication:"
echo "   1. Connect ESP32 via USB or GPIO UART"
echo "   2. Run: ./run_robot.sh"
EOF

chmod +x test_uart.sh
print_success "UART test script created: ./test_uart.sh"

# [11] Cleanup and Final Instructions
echo ""
echo "================================================"
print_success "🎉 Setup Complete!"
echo "================================================"
echo ""
echo "📋 Next steps:"
echo "   1. Connect ESP32:"
echo "      • USB Cable (recommended): Plug ESP32 to Pi via USB"
echo "      • GPIO UART: Connect ESP32 TX->GPIO14, RX->GPIO15"
echo ""
echo "   2. Upload ESP32 code (if needed):"
echo "      • Use Arduino IDE with ESP32_Robot code"
echo ""
echo "   3. Run the robot:"
echo "      ./run_robot.sh"
echo ""
echo "   4. Test UART (optional):"
echo "      ./test_uart.sh"
echo ""

if [[ "$setup_uart" =~ ^[Yy]$ ]]; then
    print_warning "⚠️  UART was configured - REBOOT REQUIRED!"
    echo ""
    read -p "🔄 Reboot now? [y/N]: " reboot_now
    if [[ "$reboot_now" =~ ^[Yy]$ ]]; then
        print_info "Rebooting in 3 seconds..."
        sleep 3
        sudo reboot
    else
        print_info "Remember to reboot later for UART to work"
    fi
fi

print_success "Setup completed successfully!"
echo "🚀 Ready to use: ./run_robot.sh"