#include <BLEDevice.h>
#include <BLEClient.h>
#include <ESP32Servo.h>
#include <esp_log.h>
#include "esp_gap_ble_api.h"   // needed for ESP_BLE_SM_SET_STATIC_PASSKEY

#define SERVICE_UUID        "19B10000-E8F2-537E-4F6C-D104768A1214"
#define CHARACTERISTIC_UUID "19B10001-E8F2-537E-4F6C-D104768A1214"

// ---- Security ----
// Must match the SERVER exactly. This is a pre-shared static passkey used for
// a real "Passkey Entry" MITM-protected pairing (not Just Works).
#define BLE_STATIC_PASSKEY 123456

static BLEUUID serviceUUID(SERVICE_UUID);
static BLEUUID charUUID(CHARACTERISTIC_UUID);

BLEClient* pClient = NULL;
BLERemoteCharacteristic* pChar = NULL;

bool connected = false;
bool isBonded = false;
unsigned long lastPacketTime = 0;
bool ledState = false;
unsigned long lastBlinkTime = 0;
uint32_t packetRxCount = 0;

Servo servo1;
Servo servo2;
Servo servo3;

const int SERVO_PIN_1 = 3;
const int SERVO_PIN_2 = 4;
const int SERVO_PIN_3 = 5;

struct SensorData {
  uint8_t angle1;
  uint8_t angle2;
  uint8_t angle3;
};

void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  if (length == sizeof(SensorData)) {
    SensorData* data = (SensorData*)pData;

    servo1.write(data->angle1);
    servo2.write(data->angle2);
    servo3.write(data->angle3);

    packetRxCount++;
    lastPacketTime = millis();

    Serial.printf("[RX #%04u] Servos -> S1: %3d° | S2: %3d° | S3: %3d° | BONDED: %s\n",
                  packetRxCount, data->angle1, data->angle2, data->angle3, isBonded ? "YES" : "NO");
  }
}

class ClientCB : public BLEClientCallbacks {
  void onConnect(BLEClient* p) override { Serial.println("[CLIENT] ---> Connected to Server"); }
  void onDisconnect(BLEClient* p) override {
    connected = false;
    isBonded = false;
    Serial.println("[CLIENT] ---> Disconnected from Server");
  }
};

class SecurityCB : public BLESecurityCallbacks {
  // This device has "Keyboard" capability (ESP_IO_CAP_IN). The stack asks it
  // to supply the passkey the "user" typed in -- we return our fixed,
  // pre-shared value, which must match the server's static passkey exactly.
  uint32_t onPassKeyRequest() override { return BLE_STATIC_PASSKEY; }
  void onPassKeyNotify(uint32_t k) override {}
  bool onConfirmPIN(uint32_t k) override { return true; }
  bool onSecurityRequest() override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t res) override {
    if (res.success) {
      isBonded = true;
      Serial.println("[SECURITY] ---> BONDED & ENCRYPTED (MITM) SUCCESSFUL!");
    } else {
      isBonded = false;
      Serial.printf("[SECURITY] ---> BONDING FAILED! reason=%d\n", res.fail_reason);
    }
  }
};

bool connectToServer(BLEAdvertisedDevice dev) {
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new ClientCB());

  if (!pClient->connect(&dev)) return false;

  BLERemoteService* pServ = pClient->getService(serviceUUID);
  if (!pServ) return false;

  pChar = pServ->getCharacteristic(charUUID);
  if (!pChar) return false;

  // Reading the protected characteristic forces the BLE stack to run the
  // MITM passkey-entry authentication/bonding handshake before any data flows.
  pChar->readValue();

  if (pChar->canNotify()) {
    pChar->registerForNotify(notifyCallback);
  }

  return true;
}

// ---------------------------------------------------------------------------
// 3-MODE LED STATE MACHINE (called every loop, independent of BLE traffic)
//   Mode 1 - OFF          : not connected to server (scanning)
//   Mode 2 - BLINK GREEN  : connected, securing/bonding in progress
//   Mode 3 - SOLID GREEN  : connected AND bonded/encrypted (secure)
// ---------------------------------------------------------------------------
void updateClientLED() {
  if (!connected) {
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
    return;
  }

  if (isBonded) {
    neopixelWrite(RGB_BUILTIN, 0, 64, 0);
    return;
  }

  if (millis() - lastBlinkTime >= 200) {
    lastBlinkTime = millis();
    ledState = !ledState;
    neopixelWrite(RGB_BUILTIN, 0, ledState ? 64 : 0, 0);
  }
}

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
  Serial.begin(115200);

  neopixelWrite(RGB_BUILTIN, 0, 0, 0); // LED OFF

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);

  servo1.setPeriodHertz(50);
  servo2.setPeriodHertz(50);
  servo3.setPeriodHertz(50);

  servo1.attach(SERVO_PIN_1, 500, 2400);
  servo2.attach(SERVO_PIN_2, 500, 2400);
  servo3.attach(SERVO_PIN_3, 500, 2400);

  servo1.write(0);
  servo2.write(0);
  servo3.write(0);

  BLEDevice::init("C3_CLIENT");
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
  BLEDevice::setSecurityCallbacks(new SecurityCB());

  // Force a fixed, pre-shared passkey instead of a randomly generated one.
  // Both server and client must set the SAME value.
  uint32_t staticPasskey = BLE_STATIC_PASSKEY;
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &staticPasskey, sizeof(uint32_t));

  BLESecurity *pSec = new BLESecurity();
  pSec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND); // SC + MITM + Bonding
  pSec->setCapability(ESP_IO_CAP_IN);                        // "Keyboard" role
  pSec->setKeySize(16);
  pSec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  pSec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  Serial.println("=================================================");
  Serial.println("         BLE CLIENT ONLINE (MITM SECURE)           ");
  Serial.println("=================================================");
}

void loop() {
  if (!connected) {
    BLEScan* pScan = BLEDevice::getScan();
    BLEScanResults* results = pScan->start(2, false);

    for (int i = 0; i < results->getCount(); i++) {
      BLEAdvertisedDevice dev = results->getDevice(i);
      if (dev.haveServiceUUID() && dev.isAdvertisingService(serviceUUID)) {
        pScan->stop();
        connected = connectToServer(dev);
        break;
      }
    }
    pScan->clearResults();
  }

  updateClientLED();

  delay(50);
}
