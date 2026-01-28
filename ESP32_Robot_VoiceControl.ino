//||======================||
//||--------EVOBOT--------||
//||======================||
//|| Updated for Raspi5   ||
//|| Voice Control        ||
//||======================||

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "driver/i2s.h"
#include "driver/ledc.h"
#include <Wire.h>
#include "DFRobotDFPlayerMini.h"
#include <Adafruit_INA219.h>

// ===== RASPI USB SERIAL =====
String raspiUSBBuffer = "";
String raspiColor = "";
String raspiDirection = "";
bool raspiDataReady = true;

//===== DFPlayer =====
DFRobotDFPlayerMini mp3;
//===== INA219 =====
Adafruit_INA219 ina219;

//===== TCA CONFIG =====
#define TCA_ADDR 0x70
#define INA_CHANNEL 3

//===== VOLTAGE READING CALIBRATION =====
#define V_GAIN   1.0084
#define V_OFFSET -0.0398

//===== UUID BLE UART =====
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// --- MOTOR 1 ---
#define M1_IN1 25
#define M1_IN2 33

// --- MOTOR 2 ---
#define M2_IN1 26
#define M2_IN2 27

// --- MOTOR 3 ---
#define M3_IN1 14
#define M3_IN2 12

// --- MOTOR 4 ---
#define M4_IN1 32
#define M4_IN2 23

//===== Ultrasonic =====
#define TRIG 5
#define ECHO 4

int PWM_SPEED = 255;
bool voiceMode = false;
unsigned long voiceCommandTime = 0;
#define VOICE_TIMEOUT 3000  // Stop after 3 seconds without command

BLECharacteristic *txCharacteristic;
String lastCommand = "S";
String received;

long lastDistance = 0;
unsigned long lastPing = 0;
bool deviceConnected = false;
unsigned long intervalRead = 0;
long lastsearching = 0;
unsigned long duration = 0;
bool greenMove = false;

enum RobotMode {
  MODE_MANUAL,
  MODE_COLOR1,
  MODE_COLOR2,
  MODE_COLOR3,
  MODE_COLOR4,
  MODE_OBSTACLE,
  MODE_AUTO
};
RobotMode currentMode = MODE_MANUAL;

//===== PWM setup for ESP32 =====
#define PWM_FREQ 1000
#define PWM_RES 8

int CH_M1_IN1 = 0;
int CH_M1_IN2 = 1;
int CH_M2_IN1 = 2;
int CH_M2_IN2 = 3;
int CH_M3_IN1 = 4;
int CH_M3_IN2 = 5;
int CH_M4_IN1 = 6;
int CH_M4_IN2 = 7;

//===== Forward Declarations =====
void allForward();
void allBackward();
void left();
void right();
void stopAllMotors();
void slow();
long readDistance();

//===== BLE Callbacks =====
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      Serial.println("BLE Device connected");
      deviceConnected = true;
    }

    void onDisconnect(BLEServer* pServer) {
      Serial.println("BLE Device disconnected");
      deviceConnected = false;
      BLEDevice::startAdvertising();
      stopAllMotors();
    }
};

class RxCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) {
    if (deviceConnected) {
      received = characteristic->getValue().c_str();
      Serial.println("BLE RX: " + received);
    }
  }
};

//===== SETUP =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== EVOBOT Starting ===");

  //===== BLE =====
  BLEDevice::init("EVOBOT");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *rxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  rxCharacteristic->setCallbacks(new RxCallback());

  txCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  txCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::getAdvertising()->start();
  Serial.println("BLE Ready");

  //===== SETUP DFPLAYER =====
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  delay(500);
  if (!mp3.begin(Serial2)) {
    Serial.println("DFPlayer init failed!");
  } else {
    mp3.volume(29);
    Serial.println("DFPlayer Ready");
  }
  
  //===== SETUP INA219 =====
  Wire.begin(21, 22);
  tcaSelect(INA_CHANNEL);
  if (!ina219.begin()) {
    Serial.println("INA219 not found!");
  } else {
    ina219.setCalibration_32V_2A();
    Serial.println("INA219 Ready");
  }

  //===== SETUP PWM =====
  int channels[] = {CH_M1_IN1, CH_M1_IN2, CH_M2_IN1, CH_M2_IN2, CH_M3_IN1, CH_M3_IN2, CH_M4_IN1, CH_M4_IN2};
  int pins[]     = {M1_IN1, M1_IN2, M2_IN1, M2_IN2, M3_IN1, M3_IN2, M4_IN1, M4_IN2};

  for (int i = 0; i < 8; i++) {
    ledcSetup(channels[i], PWM_FREQ, PWM_RES);
    ledcAttachPin(pins[i], channels[i]);
  }
  stopAllMotors();

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  Serial.println("=== EVOBOT Ready ===");
  Serial.println("Waiting for commands from Raspberry Pi...");
}


//===== LOOP =====
void loop() {
  // Read commands from BLE app
  receivedfromapp();
  
  // Read commands from Raspberry Pi USB Serial
  readRaspiUSB();
  
  // Handle voice mode
  if (voiceMode) {
    voicecommandMode();
  }
  else {
    // Handle other modes
    switch (currentMode) {
      case MODE_MANUAL:
        joystickMode();
        break;
      case MODE_COLOR1:
        greenForward();
        break;
      case MODE_COLOR2:
        yellowForward();
        break;
      case MODE_COLOR3:
        redForward();
        break;
      case MODE_COLOR4:
        searchingGreen();
        break;
      case MODE_OBSTACLE:
        obstacleMode();
        break;
      case MODE_AUTO:
        autoMode();
        break;
    }
  }
  
  // Battery monitoring
  if (millis() - intervalRead >= 1000) {
    batterryMonitoring();
    intervalRead = millis();
  }
}

void receivedfromapp() {
  if (received.length() > 0) {
    if (received.startsWith("V")) {
      int newSpeed = received.substring(1).toInt();
      PWM_SPEED = constrain(newSpeed * 2.55, 60, 255);
    }
    else if (received == "YisOFF" || received == "XisOFF") {
      setMode(MODE_MANUAL);
      Serial.println("STOPDETECTION");
    }
    else if (received == "F" || received == "B" || received == "L" || received == "R" || received == "S") {
      lastCommand = received;
    }
    else if (received == "001") mp3.playMp3Folder(1);
    else if (received == "002") mp3.playMp3Folder(2);
    else if (received == "003") mp3.playMp3Folder(3);
    else if (received == "004") mp3.playMp3Folder(4);
    else if (received == "BisOFF") mp3.stop();
    else if (received == "011") {
      duration = millis();
      txCharacteristic->setValue("SELESAI_U");
      txCharacteristic->notify();
      Serial.println("GREENFORWARD");
      setMode(MODE_COLOR1);
    }
    else if (received == "012") {
      duration = millis();
      txCharacteristic->setValue("SELESAI_U");
      txCharacteristic->notify();
      Serial.println("YELLOWFORWARD");
      setMode(MODE_COLOR2);
    }
    else if (received == "013") {
      duration = millis();
      txCharacteristic->setValue("SELESAI_U");
      txCharacteristic->notify();
      Serial.println("REDFORWARD");
      setMode(MODE_COLOR3);
    }
    else if (received == "014") {
      duration = millis();
      txCharacteristic->setValue("SELESAI_U");
      txCharacteristic->notify();
      Serial.println("GREENSEARCHING");
      setMode(MODE_COLOR4);
    }
    else if (received == "021") {
      txCharacteristic->setValue("SELESAI_C");
      txCharacteristic->notify();
      Serial.println("STOPDETECTION");
      setMode(MODE_OBSTACLE);
    }
    else if (received == "022") {
      duration = millis();
      txCharacteristic->setValue("SELESAI_C");
      txCharacteristic->notify();
      Serial.println("STOPDETECTION");
      setMode(MODE_AUTO);
    }
    received = "";
  }
}

void joystickMode() {
  if (lastCommand == "F") allForward();
  else if (lastCommand == "B") allBackward();
  else if (lastCommand == "L") left();
  else if (lastCommand == "R") right();
  else if (lastCommand == "S") stopAllMotors();
}

void greenForward() {
  if (raspiColor == "GREEN") {
    if (raspiDirection == "FORWARD") allForward();
    else if (raspiDirection == "LEFT") left();
    else if (raspiDirection == "RIGHT") right();
    else if (raspiDirection == "SLOW") slow();
    else if (raspiDirection == "STOP") {
      stopAllMotors();
      txCharacteristic->setValue("SELESAI_C");
      txCharacteristic->notify();
      setMode(MODE_MANUAL);
      Serial.println("STOPDETECTION");
      return;
    }
    else if (raspiDirection == "STOPMOMENTARY") stopAllMotors();
  }
  if (millis() - duration >= 60000) {
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
  }
}

void yellowForward() {
  if (raspiColor == "YELLOW") {
    if (raspiDirection == "FORWARD") allForward();
    else if (raspiDirection == "LEFT") left();
    else if (raspiDirection == "RIGHT") right();
    else if (raspiDirection == "SLOW") slow();
    else if (raspiDirection == "STOP") {
      stopAllMotors();
      txCharacteristic->setValue("SELESAI_C");
      txCharacteristic->notify();
      setMode(MODE_MANUAL);
      Serial.println("STOPDETECTION");
      return;
    }
    else if (raspiDirection == "STOPMOMENTARY") stopAllMotors();
  }
  if (millis() - duration >= 60000) {
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
  }
}

void redForward() {
  if (raspiColor == "RED") {
    if (raspiDirection == "FORWARD") allForward();
    else if (raspiDirection == "LEFT") left();
    else if (raspiDirection == "RIGHT") right();
    else if (raspiDirection == "SLOW") slow();
    else if (raspiDirection == "STOP") {
      stopAllMotors();
      txCharacteristic->setValue("SELESAI_C");
      txCharacteristic->notify();
      setMode(MODE_MANUAL);
      Serial.println("SELESAIDETECTION");
      return;
    }
    else if (raspiDirection == "STOPMOMENTARY") stopAllMotors();
  }
  if (millis() - duration >= 60000) {
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
  }
}

void searchingGreen() {
  if (millis() - lastsearching >= 3000) {
    lastsearching = millis();
    greenMove = !greenMove;
  }
  if (greenMove == 0) allForward();
  else if (greenMove == 1) right();

  if (raspiColor == "GREEN") {
    stopAllMotors();
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
    return;
  }
  if (millis() - duration >= 60000) {
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
  }
}

void autoMode() {
  if (millis() - lastPing >= 20) {
    long distance = readDistance();
    if (distance > 0) {
      lastDistance = distance;
      lastPing = millis();
    }
  }  
  if (lastDistance > 30) allForward();
  else right();
  if (millis() - duration >= 60000) {
    txCharacteristic->setValue("SELESAI_U");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
  }
}

void obstacleMode() {
  if (lastDistance <= 30 && lastCommand != "B" && lastCommand != "L" && lastCommand != "R") {
    stopAllMotors();
  }
  if (millis() - lastPing >= 20) {
    long distance = readDistance();
    if (distance > 0) {
      lastDistance = distance;
      lastPing = millis();
    }
  }  
  if (lastCommand == "F") {
    if (lastDistance > 30) allForward();
    else stopAllMotors();
  }
  else if (lastCommand == "B") allBackward();
  else if (lastCommand == "L") left();
  else if (lastCommand == "R") right();
  else if (lastCommand == "S") stopAllMotors();
}

// ===== VOICE COMMAND MODE =====
void voicecommandMode() {
  // Execute command based on raspiDirection
  if (raspiDirection == "MAJU") {
    allForward();
    voiceCommandTime = millis();
    Serial.println("VOICE: MAJU");
  }
  else if (raspiDirection == "MUNDUR") {
    allBackward();
    voiceCommandTime = millis();
    Serial.println("VOICE: MUNDUR");
  }
  else if (raspiDirection == "PUTARKANAN") {
    right();
    voiceCommandTime = millis();
    Serial.println("VOICE: PUTARKANAN");
  }
  else if (raspiDirection == "PUTARKIRI") {
    left();
    voiceCommandTime = millis();
    Serial.println("VOICE: PUTARKIRI");
  }
  else if (raspiDirection == "MAJUPELAN") {
    slow();
    voiceCommandTime = millis();
    Serial.println("VOICE: MAJUPELAN");
  }
  else if (raspiDirection == "BERHENTI") {
    stopAllMotors();
    voiceCommandTime = millis();
    Serial.println("VOICE: BERHENTI");
  }
  
  // Auto-stop if no command received within timeout
  if (millis() - voiceCommandTime >= VOICE_TIMEOUT) {
    stopAllMotors();
  }
  
  // Clear direction after processing
  raspiDirection = "";
}

// ===== BATTERY MONITORING =====
void tcaSelect(uint8_t tca_ch) {
  if (tca_ch > 7) return;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << tca_ch);
  Wire.endTransmission();
}

void batterryMonitoring() {
  tcaSelect(INA_CHANNEL);
  float busVoltage = ina219.getBusVoltage_V();
  float calibratedVoltage = (busVoltage * V_GAIN) + V_OFFSET;
  float batterryPercentage = fmap(calibratedVoltage, 0.0, 12.0, 0.0, 100.0);

  String msg = "BAT:" + String(batterryPercentage, 1);
  txCharacteristic->setValue(msg.c_str());
  txCharacteristic->notify();
}

float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// ===== READ FROM RASPBERRY PI USB SERIAL =====
void readRaspiUSB() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n') {
      parseRaspiData(raspiUSBBuffer);
      raspiUSBBuffer = "";
    } else if (c != '\r') {
      raspiUSBBuffer += c;
    }
  }
}

void parseRaspiData(String data) {
  data.trim();
  
  // Debug output
  Serial.print("RASPI RX: ");
  Serial.println(data);

  int commaIndex = data.indexOf(',');
  if (commaIndex == -1) {
    Serial.println("Invalid format (no comma)");
    return;
  }

  raspiColor = data.substring(0, commaIndex);
  raspiDirection = data.substring(commaIndex + 1);

  raspiColor.trim();
  raspiDirection.trim();
  
  // Convert to uppercase for consistency
  raspiColor.toUpperCase();
  raspiDirection.toUpperCase();

  raspiDataReady = true;
  
  // Handle voice mode activation/deactivation
  if (raspiDirection == "VOICECOMMANDON") {
    voiceMode = true;
    voiceCommandTime = millis();
    Serial.println(">>> VOICE MODE: ON <<<");
    // Send confirmation back to Raspberry Pi
    Serial.println("OK,VOICEMODE_ON");
  }
  else if (raspiDirection == "VOICECOMMANDOFF") {
    voiceMode = false;
    stopAllMotors();
    setMode(MODE_MANUAL);
    Serial.println(">>> VOICE MODE: OFF <<<");
    Serial.println("OK,VOICEMODE_OFF");
  }
  
  // Debug
  Serial.print("Color: ");
  Serial.print(raspiColor);
  Serial.print(", Direction: ");
  Serial.println(raspiDirection);
}

void setMode(RobotMode m) {
  stopAllMotors();
  currentMode = m;
}

//================= MOTOR FUNCTIONS =================
void allForward() {
  ledcWrite(CH_M1_IN1, PWM_SPEED); ledcWrite(CH_M1_IN2, 0);
  ledcWrite(CH_M2_IN1, PWM_SPEED); ledcWrite(CH_M2_IN2, 0);
  ledcWrite(CH_M3_IN1, PWM_SPEED); ledcWrite(CH_M3_IN2, 0);
  ledcWrite(CH_M4_IN1, PWM_SPEED); ledcWrite(CH_M4_IN2, 0);
}

void allBackward() {
  ledcWrite(CH_M1_IN1, 0); ledcWrite(CH_M1_IN2, PWM_SPEED);
  ledcWrite(CH_M2_IN1, 0); ledcWrite(CH_M2_IN2, PWM_SPEED);
  ledcWrite(CH_M3_IN1, 0); ledcWrite(CH_M3_IN2, PWM_SPEED);
  ledcWrite(CH_M4_IN1, 0); ledcWrite(CH_M4_IN2, PWM_SPEED);
}

void right() {
  ledcWrite(CH_M1_IN1, 0);         ledcWrite(CH_M1_IN2, PWM_SPEED);
  ledcWrite(CH_M4_IN1, PWM_SPEED); ledcWrite(CH_M4_IN2, 0);
  ledcWrite(CH_M2_IN1, PWM_SPEED); ledcWrite(CH_M2_IN2, 0);
  ledcWrite(CH_M3_IN1, 0);         ledcWrite(CH_M3_IN2, PWM_SPEED);
}

void left() {
  ledcWrite(CH_M1_IN1, PWM_SPEED); ledcWrite(CH_M1_IN2, 0);
  ledcWrite(CH_M3_IN1, PWM_SPEED); ledcWrite(CH_M3_IN2, 0);
  ledcWrite(CH_M2_IN1, 0);         ledcWrite(CH_M2_IN2, PWM_SPEED);
  ledcWrite(CH_M4_IN1, 0);         ledcWrite(CH_M4_IN2, PWM_SPEED);
}

void stopAllMotors() {
  for (int ch = 0; ch <= 7; ch++) ledcWrite(ch, 0);
}

void slow() {
  int s = PWM_SPEED * 0.5;
  ledcWrite(CH_M1_IN1, s); ledcWrite(CH_M1_IN2, 0);
  ledcWrite(CH_M2_IN1, s); ledcWrite(CH_M2_IN2, 0);
  ledcWrite(CH_M3_IN1, s); ledcWrite(CH_M3_IN2, 0);
  ledcWrite(CH_M4_IN1, s); ledcWrite(CH_M4_IN2, 0);
}

void resetPWM() {
  int channels[] = {CH_M1_IN1, CH_M1_IN2, CH_M2_IN1, CH_M2_IN2, CH_M3_IN1, CH_M3_IN2, CH_M4_IN1, CH_M4_IN2};
  int pins[]     = {M1_IN1, M1_IN2, M2_IN1, M2_IN2, M3_IN1, M3_IN2, M4_IN1, M4_IN2};
  for (int i = 0; i < 8; i++) {
    ledcDetachPin(pins[i]);
    ledcSetup(channels[i], PWM_FREQ, PWM_RES);
    ledcAttachPin(pins[i], channels[i]);
    ledcWrite(channels[i], 0);
  }
}

long readDistance() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  long d = pulseIn(ECHO, HIGH, 12000);
  if (d == 0) return lastDistance;
  return d * 0.01715;
}
