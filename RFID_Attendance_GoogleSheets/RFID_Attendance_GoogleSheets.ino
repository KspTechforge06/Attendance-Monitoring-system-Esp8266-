// Smart Attendance System with Google Sheets (No LCD)
// ESP8266 + MFRC522 RFID Reader

#include <SPI.h>
#include <MFRC522.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

// ---------------- Pins Configuration ----------------
// NodeMCU ESP8266 pin connections to RC522:
// RST  <-> D3 (GPIO 0)
// SDA  <-> D4 (GPIO 2)
// MOSI <-> D7 (GPIO 13)
// MISO <-> D6 (GPIO 12)
// SCK  <-> D5 (GPIO 14)
// 3.3V <-> 3.3V
// GND  <-> GND
#define RST_PIN  D3
#define SS_PIN   D4

// Buzzer configuration (connected to D2)
#define ENABLE_BUZZER true
#define BUZZER_PIN    D2

// ---------------- Wi-Fi Credentials ----------------
const char* WIFI_SSID     = "ksp's network";
const char* WIFI_PASSWORD = "Prajwal.k";

// ---------------- Google Script Web App URL ----------------
const String sheet_url = "https://script.google.com/macros/s/AKfycbzS3dt7Gf60A1K8IASLiEyDyWGSE9kVSx90AOjF7bhrG5mfno251_DMKCbYqpjTePw/exec?name=";

// ---------------- RFID Setup ----------------
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

// Block number to read stored name from (Block 2 by default)
const int blockNum = 2;

// Function declarations
bool readNameFromCard(String &cardName);
String getCardUID();
String urlEncode(const String &str);
void sendToGoogleSheets(const String &identifier);
void connectWiFi();

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println(F("=========================================="));
  Serial.println(F("  RFID Attendance System -> Google Sheets "));
  Serial.println(F("=========================================="));

  if (ENABLE_BUZZER) {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Connect to Wi-Fi
  connectWiFi();

  // Initialize SPI bus & MFRC522 reader
  SPI.begin();
  mfrc522.PCD_Init();
  delay(50);

  Serial.println(F("[RFID] Checking MFRC522 reader communication..."));
  mfrc522.PCD_DumpVersionToSerial();
  byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Serial.println(F("[RFID WARNING] Reader not detected! Please check SPI wiring (D3, D4, D5, D6, D7, 3.3V, GND)."));
  } else {
    Serial.println(F("[RFID SUCCESS] MFRC522 reader communication verified!"));
  }

  // Prepare standard MIFARE default transport key (all 0xFF)
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  Serial.println(F("------------------------------------------"));
  Serial.println(F("Ready! Please tap your RFID card / tag on the reader..."));
  Serial.println(F("------------------------------------------"));
}

void loop() {
  // Reconnect WiFi if disconnected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[Wi-Fi] Connection lost. Reconnecting..."));
    connectWiFi();
  }

  // Check if reader is ready and a card is present
  if (!mfrc522.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }

  // Select card
  if (!mfrc522.PICC_ReadCardSerial()) {
    delay(50);
    return;
  }

  Serial.println();
  Serial.println(F("------------------------------------------"));
  Serial.println(F("[RFID] Card detected!"));

  // Read Card UID
  String cardUID = getCardUID();
  Serial.print(F("[RFID] Card UID: "));
  Serial.println(cardUID);

  // Read name stored in Block 2 (if written)
  String studentName = "";
  bool nameReadOk = readNameFromCard(studentName);

  // If a valid name is stored in Block 2, use it; otherwise fallback to the UID
  String attendanceIdentifier = "";
  if (nameReadOk && studentName.length() > 0) {
    Serial.print(F("[RFID] Stored Name found: "));
    Serial.println(studentName);
    attendanceIdentifier = studentName;
  } else {
    Serial.println(F("[RFID] No custom name in Block 2; using Card UID."));
    attendanceIdentifier = "UID_" + cardUID;
  }

  // Optional buzzer beep on scan
  if (ENABLE_BUZZER) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Send record to Google Sheet
  sendToGoogleSheets(attendanceIdentifier);

  // Halt PICC and stop encryption on PCD
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  // Cooldown delay to prevent accidental re-scanning of the same card
  Serial.println(F("Ready for next card..."));
  Serial.println(F("------------------------------------------\n"));
  delay(2000);
}

// ---------------- Wi-Fi Connection Helper ----------------
void connectWiFi() {
  Serial.print(F("Connecting to Wi-Fi SSID: '"));
  Serial.print(WIFI_SSID);
  Serial.println(F("'..."));

  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(500);
    Serial.print(F("."));
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("[Wi-Fi] Connected successfully!"));
    Serial.print(F("[Wi-Fi] IP Address: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("[Wi-Fi] Signal Strength (RSSI): "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
  } else {
    Serial.print(F("[Wi-Fi WARNING] Could not connect to '"));
    Serial.print(WIFI_SSID);
    Serial.print(F("'. Status code: "));
    Serial.println(WiFi.status());

    Serial.println(F("[Wi-Fi SCAN] Scanning visible 2.4 GHz networks in range..."));
    int n = WiFi.scanNetworks();
    if (n == 0) {
      Serial.println(F("[Wi-Fi SCAN] No networks found."));
    } else {
      Serial.printf("[Wi-Fi SCAN] %d networks found:\n", n);
      for (int i = 0; i < n; ++i) {
        Serial.printf("  %d: %s (%d dBm, Ch %d) %s\n",
          i + 1,
          WiFi.SSID(i).c_str(),
          WiFi.RSSI(i),
          WiFi.channel(i),
          (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "Open" : "Secured"
        );
      }
    }
  }
}

// ---------------- Helper Functions ----------------

// Helper to extract UID as uppercase Hex string
String getCardUID() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

// Read text stored in specified block
bool readNameFromCard(String &cardName) {
  byte readBlockData[18];
  byte bufferLen = sizeof(readBlockData);

  // Authenticate using Key A
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockNum, &key, &(mfrc522.uid)
  );

  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("[RFID] Authentication failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }

  // Read Block data
  status = mfrc522.MIFARE_Read(blockNum, readBlockData, &bufferLen);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("[RFID] Reading Block failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }

  // Ensure null termination
  readBlockData[16] = '\0';

  // Extract printable characters
  cardName = "";
  for (int i = 0; i < 16; i++) {
    char c = (char)readBlockData[i];
    if (c >= 32 && c <= 126) {
      cardName += c;
    }
  }
  cardName.trim();
  return (cardName.length() > 0);
}

// URL encode string so spaces and special characters can be sent safely via HTTP GET
String urlEncode(const String &str) {
  String encoded = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

// Send HTTPS GET request to Google Apps Script
void sendToGoogleSheets(const String &identifier) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[HTTPS] Wi-Fi not connected. Attempting reconnection..."));
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("[HTTPS] Cannot send data: Wi-Fi still not connected."));
      return;
    }
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  // Disable SSL verification for Google Apps Script dynamic certs
  client->setInsecure();

  HTTPClient https;
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  https.setTimeout(8000);

  const char* headerKeys[] = {"Location"};
  https.collectHeaders(headerKeys, 1);

  String fullUrl = sheet_url + urlEncode(identifier);
  Serial.print(F("[HTTPS] Request URL: "));
  Serial.println(fullUrl);

  if (https.begin(*client, fullUrl)) {
    Serial.println(F("[HTTPS] Sending GET request to Google Sheets..."));
    int httpCode = https.GET();

    if (httpCode > 0) {
      Serial.printf("[HTTPS] Response Code: %d\n", httpCode);
      String redirectLocation = https.header("Location");
      if (redirectLocation.length() > 0) {
        Serial.print(F("[HTTPS] Redirect Location: "));
        Serial.println(redirectLocation);
      }

      if (redirectLocation.indexOf("accounts.google.com") >= 0) {
        Serial.println(F("[HTTPS ERROR] Google blocked request (requires login)!"));
        Serial.println(F("[ACTION REQUIRED] In Google Sheets -> Apps Script -> Deploy -> Manage Deployments:"));
        Serial.println(F("                  Set 'Who has access' to 'Anyone'!"));
      } else if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_FOUND || httpCode == 303 || httpCode == 307) {
        Serial.println(F("[HTTPS SUCCESS] Attendance registered successfully in Google Sheet!"));
      } else {
        String payload = https.getString();
        Serial.print(F("[HTTPS] Server Output: "));
        Serial.println(payload);
      }
    } else {
      Serial.printf("[HTTPS] Request failed, error: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    Serial.println(F("[HTTPS] Unable to begin connection."));
  }
}
