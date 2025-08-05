#include <Keypad.h>
#include <LiquidCrystal.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <EEPROM.h>

// Hardware configuration
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {32, 33, 25, 26};
byte colPins[COLS] = {27, 14, 21, 13};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// LCD pin configuration (RS, EN, D4, D5, D6, D7)
LiquidCrystal lcd(19, 23, 18, 17, 16, 4);
Servo doorLock;
const int servoPin = 5;
const int buzzerPin = 15;  // Changed from 18 to avoid conflict with LCD

// Security settings
#define MAX_ATTEMPTS 4
#define LOCKOUT_TIME 30000 // 30 sec
#define MAX_USERS 5

struct User {
  String name;
  String password;
  bool isAdmin;
};

User users[MAX_USERS] = {
  {"Admin", "1234", true},
  {"Family", "1111", false},
  {"Guest", "0000", false},
  {"Maint", "9999", false},
  {"", "", false} // Empty slot
};

byte wrongAttempts = 0;
bool isLockedOut = false;
unsigned long lockoutStart = 0;
String lastUser = "";
String accessLog = "";
bool intruderAlert = false;
bool doorLocked = true; // Track door state

// WiFi configuration
const char* ssid = "Docus";
const char* password = "Docus562";
WebServer server(80);
String dashboardURL;

void setup() {
  Serial.begin(115200);
  
  // Initialize hardware
  lcd.begin(16, 2);
  doorLock.attach(servoPin);
  lockDoor(); // Start locked
  pinMode(buzzerPin, OUTPUT);
  
  // Load wrong attempts from EEPROM
  EEPROM.begin(1);
  wrongAttempts = EEPROM.read(0);
  
  // Connect to WiFi
  connectWiFi();
  
  // Web server routes
  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/logs", handleLogs);
  server.begin();
  
  lcd.clear();
  lcd.print("Enter PIN:");
}

void loop() {
  server.handleClient();
  
  if (isLockedOut) {
    if (millis() - lockoutStart >= LOCKOUT_TIME) {
      isLockedOut = false;
      intruderAlert = false;
      lcd.clear();
      lcd.print("Enter PIN:");
    }
    return;
  }

  char key = keypad.getKey();
  if (key) {
    static String enteredPIN;
    
    if (key == '#') {
      checkPIN(enteredPIN);
      enteredPIN = "";
    } else if (key == '*') {
      enteredPIN = "";
      lcd.clear();
      lcd.print("Enter PIN:");
    } else if (isdigit(key)) {
      enteredPIN += key;
      lcd.setCursor(enteredPIN.length() - 1, 1);
      lcd.print('*');
    }
  }
}

// Core functions
void checkPIN(String enteredPIN) {
  bool accessGranted = false;
  String userName = "";
  bool isAdmin = false;
  
  for (int i = 0; i < MAX_USERS; i++) {
    if (users[i].password != "" && enteredPIN == users[i].password) {
      accessGranted = true;
      userName = users[i].name;
      isAdmin = users[i].isAdmin;
      break;
    }
  }

  if (accessGranted) {
    lastUser = userName;
    String logEntry = "[" + getTimeStamp() + "] " + userName + " entered";
    accessLog = logEntry + "<br>" + accessLog;
    if (accessLog.length() > 1000) accessLog = accessLog.substring(0, 1000);
    
    unlockDoor();
    wrongAttempts = 0;
    EEPROM.write(0, wrongAttempts);
    EEPROM.commit();
  } else {
    handleWrongAttempt();
  }
}

String getTimeStamp() {
  unsigned long now = millis();
  int seconds = now / 1000;
  int minutes = seconds / 60;
  int hours = minutes / 60;
  char buf[10];
  sprintf(buf, "%02d:%02d:%02d", hours % 24, minutes % 60, seconds % 60);
  return String(buf);
}

void lockDoor() {
  doorLock.write(0);
  doorLocked = true;
  lcd.clear();
  lcd.print("Door Locked");
  delay(1000);
  lcd.clear();
  lcd.print("Enter PIN:");
  
  // Log the action
  String logEntry = "[" + getTimeStamp() + "] Door locked by " + lastUser;
  accessLog = logEntry + "<br>" + accessLog;
}

void unlockDoor() {
  doorLock.write(90);
  doorLocked = false;
  lcd.clear();
  lcd.print("Access Granted!");
  lcd.setCursor(0, 1);
  lcd.print(lastUser);
  digitalWrite(buzzerPin, HIGH);
  delay(500);
  digitalWrite(buzzerPin, LOW);
  
  // Log the action
  String logEntry = "[" + getTimeStamp() + "] Door unlocked by " + lastUser;
  accessLog = logEntry + "<br>" + accessLog;
  
  
}

void handleWrongAttempt() {
  wrongAttempts++;
  EEPROM.write(0, wrongAttempts);
  EEPROM.commit();
  
  lcd.clear();
  lcd.print("Wrong PIN!");
  
  if (wrongAttempts == 3) {
    digitalWrite(buzzerPin, HIGH);
    delay(2000);
    digitalWrite(buzzerPin, LOW);
    intruderAlert = true;
    String logEntry = "[" + getTimeStamp() + "] INTRUDER ALERT! 3 failed attempts";
    accessLog = logEntry + "<br>" + accessLog;
  }
  else if (wrongAttempts >= MAX_ATTEMPTS) {
    isLockedOut = true;
    lockoutStart = millis();
    digitalWrite(buzzerPin, HIGH);
    delay(1000);
    digitalWrite(buzzerPin, LOW);
    lcd.clear();
    lcd.print("LOCKED OUT!");
    lcd.setCursor(0, 1);
    lcd.print("Wait 30 seconds");
    String logEntry = "[" + getTimeStamp() + "] SYSTEM LOCKOUT! 4 failed attempts";
    accessLog = logEntry + "<br>" + accessLog;
  } 
  else {
    lcd.clear();
    lcd.print("Attempts: ");
    lcd.print(wrongAttempts);
    delay(1000);
    lcd.clear();
    lcd.print("Enter PIN:");
  }
}

// WiFi functions
void connectWiFi() {
  Serial.println("\nConnecting to WiFi...");
  WiFi.begin(ssid, password);
  lcd.clear();
  lcd.print("Connecting WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    lcd.print(".");
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    dashboardURL = "http://" + WiFi.localIP().toString();
    lcd.clear();
    lcd.print("WiFi Connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Dashboard URL: ");
    Serial.println(dashboardURL);
    
    delay(2000);
  } else {
    lcd.clear();
    lcd.print("WiFi Failed!");
    Serial.println("\nWiFi connection failed!");
    delay(2000);
  }
  lcd.clear();
  lcd.print("Enter PIN:");
}

// Webserver handlers
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Smart Door Lock</title>";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; background-color: #f5f5f5; }";
  html += ".card { background: white; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); padding: 20px; margin-bottom: 20px; }";
  html += "h1 { color: #2c3e50; text-align: center; margin-bottom: 20px; }";
  html += ".status { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }";
  html += ".status-badge { padding: 8px 15px; border-radius: 20px; font-weight: bold; }";
  html += ".locked { background-color: #ff6b6b; color: white; }";
  html += ".unlocked { background-color: #51cf66; color: white; }";
  html += ".user-badge { background-color: #339af0; color: white; padding: 5px 10px; border-radius: 15px; display: inline-block; }";
  html += ".control-btn { display: block; width: 100%; padding: 12px; border: none; border-radius: 6px; font-size: 16px; font-weight: bold; cursor: pointer; margin: 10px 0; }";
  html += ".unlock-btn { background-color: #51cf66; color: white; }";
  html += ".lock-btn { background-color: #ff6b6b; color: white; }";
  html += ".logs-btn { background-color: #868e96; color: white; }";
  html += ".alert { background-color: #fff3bf; padding: 15px; border-left: 5px solid #fcc419; margin: 15px 0; }";
  html += ".attempts { color: #e03131; font-weight: bold; }";
  html += "</style>";
  html += "<script>";
  html += "function toggleDoor() {";
  html += "  fetch('/toggle').then(response => {";
  html += "    if (response.ok) {";
  html += "      return response.text();";
  html += "    } else {";
  html += "      throw new Error('Action failed');";
  html += "    }";
  html += "  }).then(data => {";
  html += "    location.reload();";
  html += "  }).catch(error => {";
  html += "    alert(error.message);";
  html += "  });";
  html += "}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='card'>";
  html += "<h1>Smart Door Lock</h1>";
  
  if (intruderAlert) {
    html += "<div class='alert'>";
    html += "🚨 INTRUDER ALERT! Multiple failed attempts detected";
    html += "</div>";
  }
  
  html += "<div class='status'>";
  html += "<div>";
  html += "<h3>Door Status</h3>";
  html += "<span class='status-badge " + String(doorLocked ? "locked'> LOCKED" : "unlocked'> UNLOCKED") + "</span>";
  html += "</div>";
  html += "<div>";
  html += "<h3>Last Entrant</h3>";
  html += "<span class='user-badge'>" + lastUser + "</span>";
  html += "</div>";
  html += "</div>";
  
  html += "<p>Wrong Attempts: <span class='attempts'>" + String(wrongAttempts) + "</span></p>";
  
  html += "<button class='control-btn " + String(doorLocked ? "unlock-btn' onclick='toggleDoor()'> Unlock Door" : "lock-btn' onclick='toggleDoor()'> Lock Door") + "</button>";
  
  html += "<a href='/logs'><button class='control-btn logs-btn'> View Access Logs</button></a>";
  
  html += "<p style='text-align: center; margin-top: 20px; color: #868e96;'>Access: " + dashboardURL + "</p>";
  html += "</div>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleToggle() {
  if (!isLockedOut) {
    lastUser = "Web Admin";
    String logEntry;
    
    if (doorLocked) {
      unlockDoor();
      logEntry = "[" + getTimeStamp() + "] Door unlocked via web";
    } else {
      lockDoor();
      logEntry = "[" + getTimeStamp() + "] Door locked via web";
    }
    
    accessLog = logEntry + "<br>" + accessLog;
    server.send(200, "text/plain", doorLocked ? "Door Locked" : "Door Unlocked");
  } else {
    server.send(403, "text/plain", "System Locked - Too many failed attempts");
  }
}

void handleLogs() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Access Logs</title>";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; }";
  html += ".log-entry { padding: 10px; border-bottom: 1px solid #eee; }";
  html += ".alert-entry { color: #e03131; font-weight: bold; }";
  html += ".back-btn { display: inline-block; margin-top: 20px; padding: 10px 15px; background: #339af0; color: white; text-decoration: none; border-radius: 5px; }";
  html += "</style>";
  html += "</head><body>";
  html += "<h1>Access Logs</h1>";
  
  // Split log entries and format them
  int lastNewline = 0;
  while (accessLog.indexOf("<br>", lastNewline) != -1) {
    int nextNewline = accessLog.indexOf("<br>", lastNewline);
    String entry = accessLog.substring(lastNewline, nextNewline);
    
    // Style alert entries differently
    if (entry.indexOf("ALERT") != -1 || entry.indexOf("LOCKOUT") != -1) {
      html += "<div class='log-entry alert-entry'>" + entry + "</div>";
    } else {
      html += "<div class='log-entry'>" + entry + "</div>";
    }
    
    lastNewline = nextNewline + 4; // Skip past the <br>
  }
  
  html += "<a href='/' class='back-btn'>← Back to Dashboard</a>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}