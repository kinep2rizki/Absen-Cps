#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <time.h>

// RFID and LCD pin definitions
#define SS_PIN D8
#define RST_PIN D0
#define LCD_ADDR 0x27  // Adjust if needed

// Firebase project settings
#define FIREBASE_HOST "Fill with ur Database......"
#define FIREBASE_AUTH "Fill with Auth of ur database...."

// Wi-Fi credentials
const char* ssid = "Your SSID.....";
const char* password = "Your Password......";

// RFID, LCD, and Firebase objects
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
FirebaseData firebaseData;
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  SPI.begin();
  rfid.PCD_Init();
  lcd.begin(16, 2);
  lcd.backlight();
  
  connectToWiFi();
  displayIPAddress();

  firebaseConfig.host = FIREBASE_HOST;
  firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&firebaseConfig, &firebaseAuth);
  Firebase.reconnectWiFi(true);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Firebase Init");

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready to scan");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String rfidData = readRFID();
    displayRFID(rfidData);
    processCheckInOut(rfidData);
    rfid.PICC_HaltA();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Processing...");
  }
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
    retryCount++;
    delay(1000);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
  } else {
    Serial.println("WiFi connection failed.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi failed");
  }
  delay(2000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready to scan");
}

void displayIPAddress() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IP Address:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(2000);
  lcd.clear();
}

String readRFID() {
  String rfidData = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    rfidData += String(rfid.uid.uidByte[i], HEX);
  }
  return rfidData;
}

void displayRFID(String rfidData) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Card ID:");
  lcd.setCursor(0, 1);
  lcd.print(rfidData);
  Serial.print("RFID Card ID: ");
  Serial.println(rfidData);
}

void processCheckInOut(String rfidData) {
  rfidData.toUpperCase();
  
  // Check if the RFID card ID exists in the registered names and get its value
  String namePath = "/names/" + rfidData;
  if (Firebase.getString(firebaseData, namePath) && firebaseData.stringData() != "") {
    // Display the registered name on the LCD and in Serial Monitor
    String registeredName = firebaseData.stringData();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Name Found:");
    lcd.setCursor(0, 1);
    lcd.print(registeredName);
    Serial.print("RFID registered as: ");
    Serial.println(registeredName);
    delay(2000);
  } else {
    // If ID is not found in "/names", show an error message and return
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ID not registered");
    Serial.println("RFID not registered");
    delay(2000);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ready to scan");
    return;
  }

  // If the card ID is registered, proceed with the attendance process
  String path = "/attendance/" + rfidData;
  String currentDate = getFormattedDate();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Card ID: ");
  lcd.setCursor(0, 1);
  lcd.print(rfidData);
  delay(2000);

  if (Firebase.getString(firebaseData, path + "/lastCheckIn")) {
    String lastCheckIn = firebaseData.stringData();
    if (lastCheckIn.startsWith(currentDate)) {
      handleCheckOut(rfidData, path);
    } else {
      handleCheckIn(rfidData, path);
      incrementDailyCount(rfidData, path, currentDate);
    }
  } else {
    handleCheckIn(rfidData, path);
    incrementDailyCount(rfidData, path, currentDate);
  }
  delay(2000);
}

void handleCheckIn(String rfidData, String path) {
  String timestamp = getFormattedTime();
  
  // Retrieve the registered name from the database
  String namePath = "/names/" + rfidData;
  String registeredName = "";
  if (Firebase.getString(firebaseData, namePath)) {
    registeredName = firebaseData.stringData();
  }

  // Store the check-in information in the database
  Firebase.setString(firebaseData, path + "/lastCheckIn", timestamp);
  Firebase.setString(firebaseData, path + "/status", "Checked In");
  Firebase.setString(firebaseData, path + "/cardID", rfidData);
  Firebase.setString(firebaseData, path + "/name", registeredName);  // Store the registered name

  // Increment count for today
  incrementDailyCount(rfidData, path, getFormattedDate());

  // Retrieve updated count to display
  int count = 0;
  if (Firebase.getInt(firebaseData, path + "/count")) {
    count = firebaseData.intData();
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Checked In:");
  lcd.setCursor(0, 1);
  lcd.print("Count: " + String(count));
  delay(2000); // Show count for 2 seconds
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Status: Checked In");
  Serial.println("Checked In successfully");
}

void handleCheckOut(String rfidData, String path) {
  String timestamp = getFormattedTime();
  Firebase.setString(firebaseData, path + "/lastCheckOut", timestamp);
  Firebase.setString(firebaseData, path + "/status", "Checked Out");
  Firebase.setString(firebaseData, path + "/cardID", rfidData);  // Store card ID in the database

  // Retrieve current count to display
  int count = 0;
  if (Firebase.getInt(firebaseData, path + "/count")) {
    count = firebaseData.intData();
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Checked Out:");
  lcd.setCursor(0, 1);
  lcd.print("Count: " + String(count));
  delay(2000); // Show count for 2 seconds
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Status: Checked Out");
  Serial.println("Checked Out successfully");
}

// Function to increment count only once per day
void incrementDailyCount(String rfidData, String path, String currentDate) {
  String countPath = path + "/count";
  String lastCountDatePath = path + "/lastCountDate";

  if (Firebase.getString(firebaseData, lastCountDatePath) && firebaseData.stringData() == currentDate) {
    // If count is already incremented today, do nothing
    return;
  }

  // Otherwise, increment count
  int count = 0;
  if (Firebase.getInt(firebaseData, countPath)) {
    count = firebaseData.intData();
  }
  count++;

  Firebase.setInt(firebaseData, countPath, count);
  Firebase.setString(firebaseData, lastCountDatePath, currentDate);
}

String getFormattedDate() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);
  return String(dateStr);
}

String getFormattedTime() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(timeStr);
}
