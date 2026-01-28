//||======================================||
//||--------EVOBOT RASPBERRY PI 5--------||
//||======================================||
//
// ESP32 Robot Controller untuk Voice Command dari Raspberry Pi 5
// Communication: Serial USB dengan format command JSON
// Compatible dengan AI Voice Assistant di Raspberry Pi 5

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "driver/i2s.h"

//===== UUID BLE UART (untuk mobile app jika perlu) =====
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

//===== Motor Driver 1 (Depan) =====
#define ENA1 25  // Enable A1
#define IN1  26  // Input 1
#define IN2  27  // Input 2

#define ENB1 13  // Enable B1
#define IN3  14  // Input 3
#define IN4  12  // Input 4

//===== Motor Driver 2 (Belakang) =====
#define ENA2 32  // Enable A2
#define IN5  33  // Input 5
#define IN6  4   // Input 6

#define ENB2 15  // Enable B2
#define IN7  5   // Input 7
#define IN8  18  // Input 8

//===== Sensors =====
#define BUZZER 23 // Buzzer pin
#define TRIG 19   // Ultrasonic trigger
#define ECHO 21   // Ultrasonic echo

//===== LED Status (Optional untuk debugging) =====
#define LED_BUILTIN 2  // Built-in LED ESP32

//===== Robot Configuration =====
int PWM_SPEED = 100;   // Kecepatan default (0–255)
bool TEST_MODE = false;  // Set true untuk testing, false untuk mode normal
bool RASPBERRY_PI_MODE = true;  // Mode khusus untuk Raspberry Pi

// Variabel untuk kontrol gerakan
String currentMovement = "STOP";
float targetDistance = 0;  // dalam meter
float traveledDistance = 0;
unsigned long movementStartTime = 0;
float SPEED_MPS = 0.5;  // Kecepatan robot meter per detik (kalibrasi sesuai robot)

// Variabel BLE (backup untuk mobile app)
BLECharacteristic *txCharacteristic;
bool deviceConnected = false;
String received;
long lastDistance = 0;
unsigned long lastPing = 0;

//===== VOICE COMMAND MODE =====
bool voiceMode = false;           // Voice command mode active?
unsigned long voiceDuration = 0;  // Last voice command timestamp
String raspiColor = "";           // Color/mode from Raspberry Pi
String raspiDirection = "";       // Direction command from Raspberry Pi
String raspiUSBBuffer = "";       // Buffer for incoming serial data

//===== Function Prototypes =====
void forward();
void backward();
void turnLeft();
void turnRight();
void stopRobot();
long readDistance();
void processVoiceCommand(String command, float value, String unit);
void sendStatusToRaspberryPi();
void handleSerialCommand();
void parseRaspiData(String data);
void voiceCommandMode();

//===== BLE Callbacks (untuk mobile app backup) =====
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      Serial.println("📱 Mobile device connected (BLE)");
      deviceConnected = true;
    }

    void onDisconnect(BLEServer* pServer) {
      Serial.println("📱 Mobile device disconnected");
      deviceConnected = false;
      BLEDevice::startAdvertising();
      received = "S";
    }
};

class RxCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) {
    if (deviceConnected) {
      received = characteristic->getValue().c_str();
      Serial.println("📱 BLE Command: " + received);
    }
  }
};

//===== Setup =====
void setup() {
  // Initialize Serial communication dengan Raspberry Pi
  Serial.begin(115200);
  delay(2000);  // Tunggu serial connection stable
  
  Serial.println("========================================");
  Serial.println("🤖 EVOBOT - ESP32 Robot Controller");
  Serial.println("🍓 Raspberry Pi 5 Voice Command Mode");
  Serial.println("========================================");
  
  if (TEST_MODE) {
    Serial.println("⚠️  TEST MODE AKTIF - Motor tidak bergerak");
    Serial.println("    Hanya print perintah ke Serial Monitor");
  } else {
    Serial.println("✅ NORMAL MODE - Motor akan bergerak");
  }
  
  // Setup BLE (sebagai backup untuk mobile control)
  if (!RASPBERRY_PI_MODE) {
    setupBLE();
  }
  
  // Setup GPIO pins
  setupMotorPins();
  setupSensorPins();
  
  // Startup sequence
  startupSequence();
  
  Serial.println("========================================");
  Serial.println("📡 Siap menerima perintah dari Raspberry Pi 5");
  Serial.println("----------------------------------------");
  Serial.println("📄 Format 1 (Extended): COMMAND,VALUE,UNIT");
  Serial.println("   Contoh: FORWARD,5,meter");
  Serial.println("           LEFT,90,degree");
  Serial.println("           SPEED,75,percent");
  Serial.println("----------------------------------------");
  Serial.println("📄 Format 2 (Voice): COLOR,DIRECTION");
  Serial.println("   Contoh: VOICE,MAJU");
  Serial.println("           VOICE,MUNDUR");
  Serial.println("           VOICE,PUTARKANAN");
  Serial.println("           VOICE,PUTARKIRI");
  Serial.println("           VOICE,BERHENTI");
  Serial.println("           VOICE,VOICECOMMANDON");
  Serial.println("           VOICE,VOICECOMMANDOFF");
  Serial.println("========================================");
  
  // Send ready signal
  Serial.println("READY_ESP32");
  delay(100);
}

//===== Main Loop =====
void loop() {
  // Primary: Handle Serial commands dari Raspberry Pi
  handleSerialCommand();
  
  // Voice mode timeout check (stop after 3 seconds without command)
  if (voiceMode && (millis() - voiceDuration >= 3000)) {
    // Auto-stop if no new command received
    if (currentMovement != "STOP") {
      stopRobot();
      Serial.println("⏰ Voice command timeout - auto stop");
    }
  }
  
  // Secondary: Handle movement dengan jarak terukur
  handleTargetedMovement();
  
  // Tertiary: Handle BLE jika ada
  if (!RASPBERRY_PI_MODE && deviceConnected && received.length() > 0) {
    handleBLECommand();
  }
  
  // Sensor reading (ultrasonic)
  if (millis() - lastPing >= 100) {  // Read setiap 100ms
    long distance = readDistance();
    if (distance > 0) {
      lastDistance = distance;
      lastPing = millis();
    }
  }
  
  // Send periodic status jika perlu
  static unsigned long lastStatusUpdate = 0;
  if (millis() - lastStatusUpdate >= 5000) {  // Setiap 5 detik
    if (RASPBERRY_PI_MODE) {
      sendStatusToRaspberryPi();
      lastStatusUpdate = millis();
    }
  }
}

//===== Handle Serial Commands dari Raspberry Pi =====
void handleSerialCommand() {
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n') {
      raspiUSBBuffer.trim();
      if (raspiUSBBuffer.length() > 0) {
        Serial.print("📥 Received: ");
        Serial.println(raspiUSBBuffer);
        
        // Parse data (supports both formats)
        parseRaspiData(raspiUSBBuffer);
      }
      raspiUSBBuffer = "";
    } else {
      raspiUSBBuffer += c;
    }
  }
}

//===== Parse Raspberry Pi Data =====
// Supports two formats:
// 1. Simple: COLOR,DIRECTION (e.g., "VOICE,MAJU")
// 2. Extended: COMMAND,VALUE,UNIT (e.g., "FORWARD,5,meter")
void parseRaspiData(String data) {
  data.trim();
  
  int firstComma = data.indexOf(',');
  int secondComma = data.indexOf(',', firstComma + 1);
  
  if (firstComma == -1) return; // Invalid format
  
  // Check if it's the simple two-part format (VOICE commands)
  if (secondComma == -1) {
    // Format: COLOR,DIRECTION
    raspiColor = data.substring(0, firstComma);
    raspiDirection = data.substring(firstComma + 1);
    
    raspiColor.trim();
    raspiDirection.trim();
    raspiColor.toUpperCase();
    raspiDirection.toUpperCase();
    
    Serial.print("🎤 Voice Mode - Color: ");
    Serial.print(raspiColor);
    Serial.print(", Direction: ");
    Serial.println(raspiDirection);
    
    // Handle voice mode activation/deactivation
    if (raspiDirection == "VOICECOMMANDON") {
      voiceMode = true;
      voiceDuration = millis();
      Serial.println("🟢 VOICE COMMAND MODE: ACTIVATED");
      return;
    }
    if (raspiDirection == "VOICECOMMANDOFF") {
      voiceMode = false;
      stopRobot();
      Serial.println("🔴 VOICE COMMAND MODE: DEACTIVATED");
      return;
    }
    
    // Process voice command if in voice mode or immediate command
    if (raspiColor == "VOICE") {
      voiceDuration = millis();  // Reset timeout
      voiceCommandMode();
    }
  } else {
    // Format: COMMAND,VALUE,UNIT
    String command = data.substring(0, firstComma);
    float value = data.substring(firstComma + 1, secondComma).toFloat();
    String unit = data.substring(secondComma + 1);
    
    command.toUpperCase();
    processVoiceCommand(command, value, unit);
  }
}

//===== Voice Command Mode Handler =====
void voiceCommandMode() {
  Serial.print("🎮 Executing voice command: ");
  Serial.println(raspiDirection);
  
  if (raspiDirection == "MAJU") {
    forward();
    Serial.println("   → Moving FORWARD");
  }
  else if (raspiDirection == "MUNDUR") {
    backward();
    Serial.println("   → Moving BACKWARD");
  }
  else if (raspiDirection == "PUTARKANAN") {
    turnRight();
    Serial.println("   → Turning RIGHT");
  }
  else if (raspiDirection == "PUTARKIRI") {
    turnLeft();
    Serial.println("   → Turning LEFT");
  }
  else if (raspiDirection == "MAJUPELAN") {
    // Slow forward - reduce speed temporarily
    int originalSpeed = PWM_SPEED;
    PWM_SPEED = PWM_SPEED / 2;
    forward();
    PWM_SPEED = originalSpeed;
    Serial.println("   → Moving FORWARD (slow)");
  }
  else if (raspiDirection == "BERHENTI") {
    stopRobot();
    Serial.println("   → STOPPED");
  }
  else {
    Serial.print("   ⚠️ Unknown voice command: ");
    Serial.println(raspiDirection);
  }
}

//===== Process Voice Command dari AI Raspberry Pi =====
void processVoiceCommand(String command, float value, String unit) {
  Serial.println("========================================");
  Serial.println("📥 PROCESSING COMMAND FROM RASPBERRY PI:");
  Serial.print("   🎯 Command: "); Serial.println(command);
  Serial.print("   🔢 Value: "); Serial.println(value);
  Serial.print("   📏 Unit: "); Serial.println(unit);
  Serial.println("========================================");
  
  // Blink built-in LED untuk indikasi processing
  digitalWrite(LED_BUILTIN, HIGH);
  
  if (command == "FORWARD") {
    if (value == -1) {
      Serial.println("🚗 EXECUTING: Maju terus (continuous forward)");
      if (!TEST_MODE) forward();
      currentMovement = "FORWARD";
      targetDistance = 0;  // Continuous
    } else {
      Serial.print("🚗 EXECUTING: Maju sejauh "); 
      Serial.print(value); 
      Serial.print(" "); 
      Serial.println(unit);
      
      if (!TEST_MODE) forward();
      currentMovement = "FORWARD";
      targetDistance = value;
      movementStartTime = millis();
      traveledDistance = 0;
    }
    Serial.println("✅ FORWARD command executed");
  }
  
  else if (command == "BACKWARD") {
    if (value == -1) {
      Serial.println("🚗 EXECUTING: Mundur terus (continuous backward)");
      if (!TEST_MODE) backward();
      currentMovement = "BACKWARD";
      targetDistance = 0;  // Continuous
    } else {
      Serial.print("🚗 EXECUTING: Mundur sejauh "); 
      Serial.print(value); 
      Serial.print(" "); 
      Serial.println(unit);
      
      if (!TEST_MODE) backward();
      currentMovement = "BACKWARD";  
      targetDistance = value;
      movementStartTime = millis();
      traveledDistance = 0;
    }
    Serial.println("✅ BACKWARD command executed");
  }
  
  else if (command == "LEFT") {
    Serial.print("🚗 EXECUTING: Belok KIRI "); 
    Serial.print(value); 
    Serial.println(" derajat");
    
    if (!TEST_MODE) {
      turnLeft();
      // Hitung durasi belok berdasarkan derajat (kalibrasi)
      int turnDuration = map(value, 0, 360, 0, 2000);  // 2 detik untuk 360 derajat
      delay(turnDuration);
      stopRobot();
    }
    currentMovement = "STOP";
    Serial.println("✅ LEFT turn completed");
  }
  
  else if (command == "RIGHT") {
    Serial.print("🚗 EXECUTING: Belok KANAN "); 
    Serial.print(value); 
    Serial.println(" derajat");
    
    if (!TEST_MODE) {
      turnRight();
      // Hitung durasi belok berdasarkan derajat (kalibrasi)
      int turnDuration = map(value, 0, 360, 0, 2000);  // 2 detik untuk 360 derajat
      delay(turnDuration);
      stopRobot();
    }
    currentMovement = "STOP";
    Serial.println("✅ RIGHT turn completed");
  }
  
  else if (command == "STOP") {
    Serial.println("🛑 EXECUTING: BERHENTI");
    if (!TEST_MODE) stopRobot();
    currentMovement = "STOP";
    targetDistance = 0;
    Serial.println("✅ STOP command executed");
  }
  
  else if (command == "SPEED") {
    int newSpeed = constrain(value * 2.55, 0, 255);  // Convert percentage to PWM
    PWM_SPEED = newSpeed;
    Serial.print("⚡ EXECUTING: Kecepatan diatur ke "); 
    Serial.print(value); 
    Serial.println("%");
    Serial.print("   PWM Value: "); 
    Serial.println(PWM_SPEED);
    Serial.println("✅ SPEED command executed");
  }
  
  else if (command == "STATUS") {
    Serial.println("📊 EXECUTING: Status check request");
    sendStatusToRaspberryPi();
    Serial.println("✅ STATUS command executed");
  }
  
  else {
    Serial.print("❌ UNKNOWN COMMAND: "); 
    Serial.println(command);
    Serial.println("   Available commands: FORWARD, BACKWARD, LEFT, RIGHT, STOP, SPEED, STATUS");
  }
  
  // Turn off LED setelah processing
  digitalWrite(LED_BUILTIN, LOW);
  
  // Kirim konfirmasi ke Raspberry Pi
  Serial.println("OK_COMMAND_PROCESSED");
  Serial.println("========================================");
}

//===== Handle Targeted Movement (dengan jarak) =====
void handleTargetedMovement() {
  if (currentMovement != "STOP" && targetDistance > 0) {
    unsigned long elapsed = millis() - movementStartTime;
    traveledDistance = (elapsed / 1000.0) * SPEED_MPS;  // Calculate distance
    
    // Debug info setiap 500ms
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug >= 500) {
      Serial.print("📏 Progress: ");
      Serial.print(traveledDistance);
      Serial.print("/");
      Serial.print(targetDistance);
      Serial.println(" meter");
      lastDebug = millis();
    }
    
    // Check jika target tercapai
    if (traveledDistance >= targetDistance) {
      if (!TEST_MODE) stopRobot();
      currentMovement = "STOP";
      targetDistance = 0;
      Serial.println("🎯 Target distance reached - Robot stopped");
      Serial.println("TARGET_REACHED");
    }
  }
}

//===== Send Status to Raspberry Pi =====
void sendStatusToRaspberryPi() {
  Serial.println("📊 ROBOT_STATUS_START");
  Serial.print("   Movement: "); Serial.println(currentMovement);
  Serial.print("   Speed_PWM: "); Serial.println(PWM_SPEED);
  Serial.print("   Speed_Percent: "); Serial.println(PWM_SPEED / 2.55);
  Serial.print("   Distance_CM: "); Serial.println(lastDistance);
  Serial.print("   Target_Distance: "); Serial.println(targetDistance);
  Serial.print("   Traveled_Distance: "); Serial.println(traveledDistance);
  Serial.print("   Test_Mode: "); Serial.println(TEST_MODE ? "ON" : "OFF");
  Serial.print("   Raspberry_Pi_Mode: "); Serial.println(RASPBERRY_PI_MODE ? "ON" : "OFF");
  Serial.print("   Millis: "); Serial.println(millis());
  Serial.println("📊 ROBOT_STATUS_END");
}

//===== Setup Functions =====
void setupBLE() {
  Serial.println("🔵 Initializing BLE...");
  BLEDevice::init("EVOBOT_RPI5");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *rxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  rxCharacteristic->setCallbacks(new RxCallback());

  txCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
  Serial.println("✅ BLE initialized and advertising");
}

void setupMotorPins() {
  Serial.println("⚙️ Setting up motor pins...");
  
  // Motor driver pins sebagai OUTPUT
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(IN5, OUTPUT); pinMode(IN6, OUTPUT);
  pinMode(IN7, OUTPUT); pinMode(IN8, OUTPUT);
  pinMode(ENA1, OUTPUT); pinMode(ENB1, OUTPUT);
  pinMode(ENA2, OUTPUT); pinMode(ENB2, OUTPUT);

  // Initialize semua pin LOW
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  digitalWrite(IN5, LOW); digitalWrite(IN6, LOW);
  digitalWrite(IN7, LOW); digitalWrite(IN8, LOW);
  
  Serial.println("✅ Motor pins configured");
}

void setupSensorPins() {
  Serial.println("📡 Setting up sensor pins...");
  
  pinMode(BUZZER, OUTPUT);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  
  digitalWrite(BUZZER, LOW);
  digitalWrite(LED_BUILTIN, LOW);
  
  Serial.println("✅ Sensor pins configured");
}

void startupSequence() {
  Serial.println("🔊 Starting up...");
  
  // Beep startup sequence
  for (int i = 0; i < 3; i++) {
    tone(BUZZER, 1000 + (i * 200));
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);
    noTone(BUZZER);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }
  
  // Final long beep
  tone(BUZZER, 1500);
  delay(500);
  noTone(BUZZER);
  
  Serial.println("✅ Startup sequence complete");
}

//===== Motor Control Functions =====
void forward() {
  if (TEST_MODE) {
    Serial.println("🔧 [TEST] FORWARD motion (motors not activated)");
    return;
  }
  
  motorLeftForward();
  motorRightForward();
  Serial.println("⏩ Motors: FORWARD");
}

void backward() {
  if (TEST_MODE) {
    Serial.println("🔧 [TEST] BACKWARD motion (motors not activated)");
    return;
  }
  
  motorLeftBackward();
  motorRightBackward();
  Serial.println("⏪ Motors: BACKWARD");
}

void turnLeft() {
  if (TEST_MODE) {
    Serial.println("🔧 [TEST] LEFT turn (motors not activated)");
    return;
  }
  
  motorTurnLeft();
  Serial.println("⏪ Motors: TURN LEFT");
}

void turnRight() {
  if (TEST_MODE) {
    Serial.println("🔧 [TEST] RIGHT turn (motors not activated)");
    return;
  }
  
  motorTurnRight();
  Serial.println("⏩ Motors: TURN RIGHT");
}

void stopRobot() {
  if (TEST_MODE) {
    Serial.println("🔧 [TEST] STOP (motors not activated)");
    return;
  }
  
  motorLeftStop();
  motorRightStop();
  Serial.println("🛑 Motors: STOP");
}

//===== Individual Motor Functions =====
// LEFT SIDE = ENA1 + ENB1
void motorLeftForward() {
  analogWrite(ENA1, PWM_SPEED);
  analogWrite(ENB1, PWM_SPEED);
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
}

void motorLeftBackward() {
  analogWrite(ENA1, PWM_SPEED);
  analogWrite(ENB1, PWM_SPEED);
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}

void motorLeftStop() {
  analogWrite(ENA1, 0);
  analogWrite(ENB1, 0);
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}

// RIGHT SIDE = ENA2 + ENB2
void motorRightForward() {
  analogWrite(ENA2, PWM_SPEED);
  analogWrite(ENB2, PWM_SPEED);
  digitalWrite(IN5, HIGH); digitalWrite(IN6, LOW);
  digitalWrite(IN7, HIGH); digitalWrite(IN8, LOW);
}

void motorRightBackward() {
  analogWrite(ENA2, PWM_SPEED);
  analogWrite(ENB2, PWM_SPEED);
  digitalWrite(IN5, LOW);  digitalWrite(IN6, HIGH);
  digitalWrite(IN7, LOW);  digitalWrite(IN8, HIGH);
}

void motorRightStop() {
  analogWrite(ENA2, 0);
  analogWrite(ENB2, 0);
  digitalWrite(IN5, LOW); digitalWrite(IN6, LOW);
  digitalWrite(IN7, LOW); digitalWrite(IN8, LOW);
}

void motorTurnLeft() {
  // Putar kiri: motor kiri mundur, motor kanan maju
  analogWrite(ENA1, PWM_SPEED); analogWrite(ENB1, PWM_SPEED);
  analogWrite(ENA2, PWM_SPEED); analogWrite(ENB2, PWM_SPEED);
  
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);   // Left backward
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  
  digitalWrite(IN5, HIGH); digitalWrite(IN6, LOW);   // Right forward
  digitalWrite(IN7, LOW);  digitalWrite(IN8, HIGH);
}

void motorTurnRight() {
  // Putar kanan: motor kiri maju, motor kanan mundur
  analogWrite(ENA1, PWM_SPEED); analogWrite(ENB1, PWM_SPEED);
  analogWrite(ENA2, PWM_SPEED); analogWrite(ENB2, PWM_SPEED);
  
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);   // Left forward
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  
  digitalWrite(IN5, LOW);  digitalWrite(IN6, HIGH);  // Right backward
  digitalWrite(IN7, HIGH); digitalWrite(IN8, LOW);
}

//===== Ultrasonic Sensor =====
long readDistance() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  long duration = pulseIn(ECHO, HIGH, 8000);  // Timeout 8ms
  if (duration == 0) return 0;  // No echo received
  
  long distance = duration * 0.01715;  // Convert to cm
  return distance;
}

//===== Handle BLE Commands (backup untuk mobile app) =====
void handleBLECommand() {
  // Simple mobile app commands
  if (received == "F") forward();
  else if (received == "B") backward();
  else if (received == "L") turnLeft();
  else if (received == "R") turnRight();
  else if (received == "S") stopRobot();
  else if (received.startsWith("V")) {
    int speed = received.substring(1).toInt();
    PWM_SPEED = constrain(speed * 2.55, 0, 255);
    Serial.print("📱 BLE Speed set to: "); Serial.println(speed);
  }
  
  received = "";  // Clear command
}