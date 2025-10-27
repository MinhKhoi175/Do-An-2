#include <Wire.h>                       // Thư viện giao tiếp I2C cho LCD
#include <LiquidCrystal_I2C.h>          // Thư viện điều khiển màn hình LCD I2C
#include <Keypad.h>                     // Thư viện điều khiển bàn phím 4x4
#include <ESP32Servo.h>                 // Thư viện điều khiển servo trên ESP32
#include <SPI.h>                        // Thư viện giao tiếp SPI cho RFID RC522
#include <MFRC522.h>                    // Thư viện điều khiển module RFID RC522
#include <SoftwareSerial.h>             // Thư viện tạo cổng Serial ảo cho cảm biến vân tay
#include <Adafruit_Fingerprint.h>       // Thư viện điều khiển cảm biến vân tay AS608
#include <WiFi.h>                       // Thư viện kết nối WiFi cho ESP32
#include <Firebase_ESP_Client.h>        // Thư viện kết nối và làm việc với Firebase
#include <addons/TokenHelper.h>         // Thư viện hỗ trợ quản lý token Firebase
#include <addons/RTDBHelper.h>          // Thư viện hỗ trợ làm việc với Realtime Database của Firebase

// Thông tin WiFi
#define WIFI_SSID "NGOC HIEN"      // Tên mạng WiFi
#define WIFI_PASSWORD "trancaominhkhoi"    // Mật khẩu WiFi

// Firebase credentials
#define API_KEY "AIzaSyAwNLS7RHasQrILtUGDW6BiNZqB5u9U5WY"                         // Khóa API của Firebase
#define DATABASE_URL "https://smart-home-d6e89-default-rtdb.firebaseio.com/"      // URL của Firebase Realtime Database
#define USER_EMAIL "nguyentaianhtuan2004@gmail.com"                               // Email đăng nhập Firebase
#define USER_PASSWORD "0123456789"                                                // Mật khẩu đăng nhập Firebase

// Khởi tạo LCD với địa chỉ I2C 0x27, 20 cột, 4 hàng
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Cấu hình bàn phím 4x4
const byte ROWS = 4;                // Số hàng của bàn phím
const byte COLS = 4;                // Số cột của bàn phím
char keys[ROWS][COLS] = 
{
  {'1','2','3','A'},        // Ma trận ký tự trên bàn phím
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {13, 12, 14, 27};          // Chân GPIO kết nối với các hàng
byte colPins[COLS] = {26, 25, 33, 32};          // Chân GPIO kết nối với các cột
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);     // Khởi tạo bàn phím

// Cấu hình Servo
Servo doorServo;              // Đối tượng điều khiển servo
const int servoPin = 18;      // Chân GPIO kết nối với servo

// Cấu hình RFID RC522
#define RST_PIN 2                     // Chân Reset của module RFID
#define SS_PIN 5                      // Chân SS (Slave Select) của module RFID
MFRC522 mfrc522(SS_PIN, RST_PIN);     // Khởi tạo module RFID

// Cấu hình cảm biến vân tay AS608
SoftwareSerial mySerial(16, 17);            // Cổng Serial ảo (RX, TX) cho cảm biến vân tay
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);        // Khởi tạo cảm biến vân tay

// Firebase objects
FirebaseData fbdo;            // Đối tượng lưu trữ dữ liệu Firebase
FirebaseAuth auth;            // Đối tượng xác thực Firebase
FirebaseConfig config;        // Đối tượng cấu hình Firebase

// Biến trạng thái
enum State {
  MAIN_SCREEN,          // Màn hình chính
  AUTH_MENU,            // Menu chọn phương thức xác thực
  PASSWORD_AUTH,        // Xác thực mật khẩu
  RFID_AUTH,            // Xác thực thẻ RFID
  FINGERPRINT_AUTH,     // Xác thực vân tay
  SETUP_MENU,           // Menu cài đặt
  SETUP_PASSWORD,       // Cài đặt mật khẩu mới
  SETUP_RFID,           // Cài đặt thẻ RFID mới
  SETUP_FINGERPRINT,    // Cài đặt vân tay mới
  SUCCESS_SCREEN,       // Màn hình xác thực thành công
  FAILED_SCREEN         // Màn hình xác thực thất bại
};

State currentState = MAIN_SCREEN;               // Trạng thái hiện tại của hệ thống
int menuSelection = 0;                          // Lựa chọn trong menu xác thực (0: Mật khẩu, 1: RFID, 2: Vân tay)
int setupSelection = 0;                         // Lựa chọn trong menu cài đặt (0: Mật khẩu, 1: RFID, 2: Vân tay)
String enteredPassword = "";                    // Mật khẩu người dùng nhập
String newPassword = "";                        // Mật khẩu mới trong chế độ cài đặt
String correctPassword = "123456";              // Mật khẩu mặc định
String authorizedUIDs[10];                      // Danh sách UID của thẻ RFID được phép
int numAuthorizedUIDs = 0;                      // Số lượng UID hiện tại
int authorizedFingerprints[10];                 // Danh sách ID vân tay được phép
int numAuthorizedFingerprints = 0;              // Số lượng ID vân tay hiện tại
unsigned long lastActionTime = 0;               // Thời gian của hành động cuối cùng (dùng cho timeout)
unsigned long rfidCheckTime = 0;                // Thời gian kiểm tra RFID
unsigned long fingerprintCheckTime = 0;         // Thời gian kiểm tra vân tay

void setup() {
  Serial.begin(115200);                         // Khởi tạo cổng Serial để debug
  
  // Kết nối WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Đang kết nối WiFi...");
  while (WiFi.status() != WL_CONNECTED) {       // Chờ kết nối WiFi
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nĐã kết nối WiFi!");

  // Khởi tạo Firebase
  config.api_key = API_KEY;                     // Gán khóa API
  config.database_url = DATABASE_URL;           // Gán URL cơ sở dữ liệu
  auth.user.email = USER_EMAIL;                 // Gán email người dùng
  auth.user.password = USER_PASSWORD;           // Gán mật khẩu người dùng
  config.token_status_callback = tokenStatusCallback;           // Callback xử lý token
  Firebase.begin(&config, &auth);                               // Khởi tạo Firebase
  Firebase.reconnectWiFi(true);                                 // Tự động kết nối lại WiFi nếu mất kết nối
  
  // Chờ đăng nhập Firebase
  Serial.print("Đang đăng nhập Firebase...");
  while (!Firebase.ready()) {         // Chờ Firebase sẵn sàng
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nFirebase đã sẵn sàng!");

  // Khởi tạo SPI cho RFID
  SPI.begin(4, 19, 23, 5);                        // Cấu hình chân SPI: SCK, MISO, MOSI, SS
  mfrc522.PCD_Init();                             // Khởi tạo module RFID
  Serial.println("RFID RC522 đã sẵn sàng!");
  
  // Khởi tạo cảm biến vân tay
  mySerial.begin(57600);                          // Khởi tạo cổng Serial ảo với tốc độ 57600
  delay(100);
  finger.begin(57600);                            // Khởi tạo cảm biến vân tay với tốc độ 57600
  delay(100);
  
  // Thử kết nối cảm biến vân tay
  int fingerAttempts = 0;
  bool fingerConnected = false;
  while (fingerAttempts < 5 && !fingerConnected) 
  {    
    // Thử tối đa 5 lần
    if (finger.verifyPassword()) 
    { 
      // Kiểm tra kết nối với cảm biến
      Serial.println("Cảm biến vân tay AS608 đã sẵn sàng!");
      fingerConnected = true;
    } else {
      Serial.println("Thử kết nối cảm biến vân tay lần " + String(fingerAttempts + 1));
      fingerAttempts++;
      delay(500);
    }
  }
  
  if (!fingerConnected) 
  { 
    // Nếu không kết nối được
    Serial.println("CẢNH BÁO: Không tìm thấy cảm biến vân tay!");
    Serial.println("Kiểm tra kết nối:");
    Serial.println("- VCC AS608 -> 3.3V hoặc 5V");
    Serial.println("- GND AS608 -> GND");
    Serial.println("- TX AS608 -> GPIO16");
    Serial.println("- RX AS608 -> GPIO17");
  }
  
  // Khởi tạo I2C
  Wire.begin();                 // Bắt đầu giao tiếp I2C
  
  // Khởi tạo LCD
  lcd.init();                   // Khởi tạo LCD
  lcd.backlight();              // Bật đèn nền LCD
  lcd.clear();                  // Xóa màn hình LCD
  
  // Khởi tạo Servo
  doorServo.attach(servoPin);     // Gắn servo vào chân GPIO
  doorServo.write(0);             // Đặt servo về góc 0 độ (đóng cửa)
  delay(500);
  
  // Tải dữ liệu từ Firebase
  loadFirebaseData();             // Tải mật khẩu, UID RFID và ID vân tay từ Firebase
  
  // Hiển thị màn hình chính
  showMainScreen();
  
  Serial.println("Hệ thống đã sẵn sàng!");
}

void loop() {
  char key = keypad.getKey();                 // Lấy phím được nhấn từ bàn phím
  
  if (key) 
  {    
    // Nếu có phím được nhấn
    Serial.println("Phím nhấn: " + String(key));
    handleKeyPress(key); // Xử lý phím nhấn
  }
  
  // Kiểm tra RFID trong chế độ xác thực RFID
  if (currentState == RFID_AUTH && rfidCheckTime > 0 && millis() - rfidCheckTime > 500) {
    checkRFID(); // Kiểm tra thẻ RFID
    rfidCheckTime = millis(); // Cập nhật thời gian kiểm tra
  }
  
  // Kiểm tra RFID trong chế độ cài đặt RFID mới
  if (currentState == SETUP_RFID && rfidCheckTime > 0 && millis() - rfidCheckTime > 500) {
    setupNewRFID(); // Cài đặt thẻ RFID mới
    rfidCheckTime = millis(); // Cập nhật thời gian kiểm tra
  }
  
  // Kiểm tra vân tay trong chế độ xác thực vân tay
  if (currentState == FINGERPRINT_AUTH && fingerprintCheckTime > 0 && millis() - fingerprintCheckTime > 500) {
    checkFingerprint(); // Kiểm tra vân tay
    fingerprintCheckTime = millis(); // Cập nhật thời gian kiểm tra
  }
  
  // Kiểm tra vân tay trong chế độ cài đặt vân tay mới
  if (currentState == SETUP_FINGERPRINT && fingerprintCheckTime > 0 && millis() - fingerprintCheckTime > 500) {
    setupNewFingerprint(); // Cài đặt vân tay mới
    fingerprintCheckTime = millis(); // Cập nhật thời gian kiểm tra
  }
  
  // Kiểm tra timeout cho các màn hình kết quả
  if ((currentState == SUCCESS_SCREEN || currentState == FAILED_SCREEN) && 
      lastActionTime > 0 && millis() - lastActionTime > 3000) {
    currentState = MAIN_SCREEN; // Quay về màn hình chính sau 3 giây
    showMainScreen();
  }
}

// Tải dữ liệu từ Firebase
void loadFirebaseData() {
  // Tải mật khẩu từ Firebase
  if (Firebase.RTDB.getString(&fbdo, "/smart_home/auth/password")) {
    correctPassword = fbdo.stringData(); // Lấy mật khẩu từ Firebase
    Serial.println("Mật khẩu từ Firebase: " + correctPassword);
  } else {
    Serial.println("Lỗi tải mật khẩu: " + fbdo.errorReason());
    // Sử dụng mật khẩu mặc định nếu không tải được
    correctPassword = "123456";
    Firebase.RTDB.setString(&fbdo, "/smart_home/auth/password", correctPassword);
  }

  // Tải danh sách UID RFID từ Firebase
  if (Firebase.RTDB.getArray(&fbdo, "/smart_home/auth/rfid")) {
    FirebaseJsonArray &arr = fbdo.jsonArray();
    numAuthorizedUIDs = arr.size(); // Lấy số lượng UID
    if (numAuthorizedUIDs > 10) numAuthorizedUIDs = 10; // Giới hạn tối đa 10 UID
    for (int i = 0; i < numAuthorizedUIDs; i++) {
      FirebaseJsonData jsonData;
      arr.get(jsonData, i); // Lấy từng UID
      authorizedUIDs[i] = jsonData.stringValue; // Lưu UID vào mảng
      Serial.println("UID " + String(i) + ": " + authorizedUIDs[i]);
    }
  } else {
    Serial.println("Lỗi tải RFID: " + fbdo.errorReason());
    // Thêm UID mặc định nếu không tải được
    authorizedUIDs[0] = "A1 B2 C3 D4";
    numAuthorizedUIDs = 1;
    FirebaseJsonArray arr;
    arr.add(authorizedUIDs[0]);
    Firebase.RTDB.setArray(&fbdo, "/smart_home/auth/rfid", &arr);
  }

  // Tải danh sách ID vân tay từ Firebase
  if (Firebase.RTDB.getArray(&fbdo, "/smart_home/auth/fingerprint")) {
    FirebaseJsonArray &arr = fbdo.jsonArray();
    numAuthorizedFingerprints = arr.size(); // Lấy số lượng ID vân tay
    if (numAuthorizedFingerprints > 10) numAuthorizedFingerprints = 10; // Giới hạn tối đa 10 ID
    for (int i = 0; i < numAuthorizedFingerprints; i++) {
      FirebaseJsonData jsonData;
      arr.get(jsonData, i); // Lấy từng ID
      authorizedFingerprints[i] = jsonData.intValue; // Lưu ID vào mảng
      Serial.println("ID vân tay " + String(i) + ": " + String(authorizedFingerprints[i]));
    }
  } else {
    Serial.println("Lỗi tải vân tay: " + fbdo.errorReason());
    // Không thêm vân tay mặc định
  }
}

// Xử lý phím nhấn từ bàn phím
void handleKeyPress(char key) {
  switch (currentState) {
    case MAIN_SCREEN:
      if (key == 'A') { // Phím A: Vào menu xác thực
        currentState = AUTH_MENU;
        menuSelection = 0;
        showAuthMenu();
      } else if (key == 'D') { // Phím D: Vào menu cài đặt
        currentState = SETUP_MENU;
        setupSelection = 0;
        showSetupMenu();
      }
      break;
      
    case AUTH_MENU:
      if (key == 'B') { // Phím B: Chuyển đổi lựa chọn trong menu xác thực
        menuSelection = (menuSelection + 1) % 3;
        showAuthMenu();
      } else if (key == '#') { // Phím #: Chọn phương thức xác thực
        selectAuthMethod();
      } else if (key == '*') { // Phím *: Quay về màn hình chính
        currentState = MAIN_SCREEN;
        showMainScreen();
      }
      break;
      
    case SETUP_MENU:
      if (key == 'B') { // Phím B: Chuyển đổi lựa chọn trong menu cài đặt
        setupSelection = (setupSelection + 1) % 3;
        showSetupMenu();
      } else if (key == '#') { // Phím #: Chọn phương thức cài đặt
        selectSetupMethod();
      } else if (key == '*') { // Phím *: Quay về màn hình chính
        currentState = MAIN_SCREEN;
        showMainScreen();
      }
      break;
      
    case PASSWORD_AUTH:
      if (key == '#') { // Phím #: Kiểm tra mật khẩu
        checkPassword();
      } else if (key == '*') { // Phím *: Xóa ký tự hoặc quay về menu xác thực
        if (enteredPassword.length() > 0) {
          enteredPassword.remove(enteredPassword.length() - 1);
          showPasswordScreen();
        } else {
          currentState = AUTH_MENU;
          menuSelection = 0;
          showAuthMenu();
        }
      } else if (key >= '0' && key <= '9' && enteredPassword.length() < 6) { // Nhập số
        enteredPassword += key;
        showPasswordScreen();
      }
      break;
      
    case SETUP_PASSWORD:
      if (key == '#') { // Phím #: Lưu mật khẩu mới
        setupNewPassword();
      } else if (key == '*') { // Phím *: Xóa ký tự hoặc quay về menu cài đặt
        if (newPassword.length() > 0) {
          newPassword.remove(newPassword.length() - 1);
          showSetupPasswordScreen();
        } else {
          currentState = SETUP_MENU;
          setupSelection = 0;
          showSetupMenu();
        }
      } else if (key >= '0' && key <= '9' && newPassword.length() < 6) { // Nhập số
        newPassword += key;
        showSetupPasswordScreen();
      }
      break;
      
    case RFID_AUTH:
      if (key == '*') { // Phím *: Quay về menu xác thực
        currentState = AUTH_MENU;
        menuSelection = 1;
        showAuthMenu();
      }
      break;
      
    case SETUP_RFID:
      if (key == '*') { // Phím *: Quay về menu cài đặt
        currentState = SETUP_MENU;
        setupSelection = 1;
        showSetupMenu();
      }
      break;
      
    case FINGERPRINT_AUTH:
      if (key == '*') { // Phím *: Quay về menu xác thực
        currentState = AUTH_MENU;
        menuSelection = 2;
        showAuthMenu();
      }
      break;
      
    case SETUP_FINGERPRINT:
      if (key == '*') { // Phím *: Quay về menu cài đặt
        currentState = SETUP_MENU;
        setupSelection = 2;
        showSetupMenu();
      }
      break;
  }
}

// Chọn phương thức xác thực
void selectAuthMethod() {
  switch (menuSelection) {
    case 0: // Mật khẩu
      currentState = PASSWORD_AUTH;
      enteredPassword = "";
      showPasswordScreen();
      break;
    case 1: // RFID
      currentState = RFID_AUTH;
      showRFIDScreen();
      rfidCheckTime = millis();
      break;
    case 2: // Vân tay
      currentState = FINGERPRINT_AUTH;
      showFingerprintScreen();
      fingerprintCheckTime = millis();
      break;
  }
}

// Chọn phương thức cài đặt
void selectSetupMethod() {
  switch (setupSelection) {
    case 0: // Mật khẩu mới
      currentState = SETUP_PASSWORD;
      newPassword = "";
      showSetupPasswordScreen();
      break;
    case 1: // RFID mới
      currentState = SETUP_RFID;
      showSetupRFIDScreen();
      rfidCheckTime = millis();
      break;
    case 2: // Vân tay mới
      currentState = SETUP_FINGERPRINT;
      showSetupFingerprintScreen();
      fingerprintCheckTime = millis();
      break;
  }
}

// Hiển thị màn hình chính
void showMainScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("HE THONG GIAM SAT"); // Hiển thị tiêu đề
  lcd.setCursor(0, 1);
  lcd.print("NHA THONG MINH"); // Hiển thị dòng phụ
  lcd.setCursor(0, 2);
  lcd.print("NGUYEN TAI ANH TUAN"); // Tên tác giả
  lcd.setCursor(0, 3);
  lcd.print("DH SPKT TP.HCM"); // Đơn vị thực hiện
}

// Hiển thị menu xác thực
void showAuthMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PHUONG THUC XAC THUC");
  
  lcd.setCursor(0, 1);
  lcd.print((menuSelection == 0) ? "> MAT KHAU" : "  MAT KHAU"); // Hiển thị lựa chọn mật khẩu
  
  lcd.setCursor(0, 2);
  lcd.print((menuSelection == 1) ? "> THE UID" : "  THE UID"); // Hiển thị lựa chọn RFID
  
  lcd.setCursor(0, 3);
  lcd.print((menuSelection == 2) ? "> VAN TAY" : "  VAN TAY"); // Hiển thị lựa chọn vân tay
}

// Hiển thị menu cài đặt
void showSetupMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CHE DO CAI DAT");
  
  lcd.setCursor(0, 1);
  lcd.print((setupSelection == 0) ? "> MAT KHAU MOI" : "  MAT KHAU MOI"); // Hiển thị lựa chọn mật khẩu mới
  
  lcd.setCursor(0, 2);
  lcd.print((setupSelection == 1) ? "> THE RFID MOI" : "  THE RFID MOI"); // Hiển thị lựa chọn RFID mới
  
  lcd.setCursor(0, 3);
  lcd.print((setupSelection == 2) ? "> VAN TAY MOI" : "  VAN TAY MOI"); // Hiển thị lựa chọn vân tay mới
}

// Hiển thị màn hình nhập mật khẩu mới
void showSetupPasswordScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MAT KHAU MOI");
  
  lcd.setCursor(0, 1);
  String display = "NHAP: ";
  for (int i = 0; i < newPassword.length(); i++) {
    display += "*"; // Hiển thị dấu * thay cho ký tự
  }
  lcd.print(display);
  
  lcd.setCursor(0, 2);
  lcd.print("#: XAC THUC/OK"); // Hướng dẫn phím #
  
  lcd.setCursor(0, 3);
  lcd.print("*: XOA/HUY"); // Hướng dẫn phím *
}

// Hiển thị màn hình cài đặt thẻ RFID mới
void showSetupRFIDScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("THE RFID MOI");
  
  lcd.setCursor(0, 1);
  lcd.print("VUI LONG QUET THE"); // Yêu cầu quét thẻ
  
  lcd.setCursor(0, 3);
  lcd.print("*: XOA/HUY"); // Hướng dẫn phím *
}

// Hiển thị màn hình cài đặt vân tay mới
void showSetupFingerprintScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("VAN TAY MOI");
  
  lcd.setCursor(0, 1);
  lcd.print("VUI LONG DAT VAN TAY"); // Yêu cầu đặt vân tay
  
  lcd.setCursor(0, 3);
  lcd.print("*: XOA/HUY"); // Hướng dẫn phím *
}

// Lưu mật khẩu mới vào Firebase
void setupNewPassword() {
  if (newPassword.length() >= 4) { // Kiểm tra độ dài mật khẩu
    if (Firebase.RTDB.setString(&fbdo, "/smart_home/auth/password", newPassword)) {
      correctPassword = newPassword; // Cập nhật mật khẩu
      Serial.println("Mật khẩu mới đã được lưu: " + newPassword);
      showSetupSuccessScreen();
    } else {
      Serial.println("Lỗi lưu mật khẩu: " + fbdo.errorReason());
      showSetupFailedScreen();
    }
  } else {
    Serial.println("Mật khẩu quá ngắn!");
    showSetupFailedScreen();
  }
  newPassword = ""; // Xóa mật khẩu tạm
}

// Cài đặt thẻ RFID mới
void setupNewRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) { // Kiểm tra xem có thẻ mới không
    return;
  }
  
  if (!mfrc522.PICC_ReadCardSerial()) { // Đọc dữ liệu thẻ
    return;
  }
  
  // Lấy UID của thẻ
  String content = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    content += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    content += String(mfrc522.uid.uidByte[i], HEX);
    if (i < mfrc522.uid.size - 1) content += " ";
  }
  content.toUpperCase(); // Chuyển UID sang chữ in hoa
  
  // Kiểm tra xem UID đã tồn tại chưa
  bool exists = false;
  for (int i = 0; i < numAuthorizedUIDs; i++) {
    if (authorizedUIDs[i] == content) {
      exists = true;
      break;
    }
  }
  
  if (!exists && numAuthorizedUIDs < 10) { // Nếu UID chưa tồn tại và danh sách chưa đầy
    authorizedUIDs[numAuthorizedUIDs] = content; // Thêm UID mới
    numAuthorizedUIDs++;
    
    // Cập nhật danh sách UID vào Firebase
    FirebaseJsonArray arr;
    for (int i = 0; i < numAuthorizedUIDs; i++) {
      arr.add(authorizedUIDs[i]);
    }
    if (Firebase.RTDB.setArray(&fbdo, "/smart_home/auth/rfid", &arr)) {
      Serial.println("UID thẻ mới đã được lưu: " + content);
      showSetupSuccessScreen();
    } else {
      Serial.println("Lỗi lưu UID: " + fbdo.errorReason());
      showSetupFailedScreen();
      numAuthorizedUIDs--; // Hủy thêm UID nếu lỗi
    }
  } else {
    Serial.println(exists ? "UID đã tồn tại!" : "Danh sách RFID đầy!");
    showSetupFailedScreen();
  }
  
  mfrc522.PICC_HaltA(); // Dừng giao tiếp với thẻ
}

// Cài đặt vân tay mới
void setupNewFingerprint() {
  int id = numAuthorizedFingerprints + 1; // Tạo ID mới cho vân tay
  Serial.println("Đang đăng ký vân tay với ID: " + String(id));
  
  // Bước 1: Lấy ảnh vân tay lần 1
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DAT VAN TAY LAN 1");
  while (finger.getImage() != FINGERPRINT_OK) { // Chờ lấy ảnh vân tay
    delay(50);
  }
  if (finger.image2Tz(1) != FINGERPRINT_OK) { // Chuyển ảnh thành đặc trưng
    showSetupFailedScreen();
    return;
  }
  
  // Yêu cầu bỏ tay ra
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("BO TAY RA");
  delay(2000);
  
  // Bước 2: Lấy ảnh vân tay lần 2
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DAT VAN TAY LAN 2");
  while (finger.getImage() != FINGERPRINT_OK) { // Chờ lấy ảnh vân tay
    delay(50);
  }
  if (finger.image2Tz(2) != FINGERPRINT_OK) { // Chuyển ảnh thành đặc trưng
    showSetupFailedScreen();
    return;
  }
  
  if (finger.createModel() != FINGERPRINT_OK) { // Tạo mô hình từ hai đặc trưng
    showSetupFailedScreen();
    return;
  }
  
  if (finger.storeModel(id) != FINGERPRINT_OK) { // Lưu mô hình vào cảm biến
    showSetupFailedScreen();
    return;
  }
  
  // Lưu ID vân tay vào Firebase
  if (numAuthorizedFingerprints < 10) { // Kiểm tra danh sách chưa đầy
    authorizedFingerprints[numAuthorizedFingerprints] = id;
    numAuthorizedFingerprints++;
    
    FirebaseJsonArray arr;
    for (int i = 0; i < numAuthorizedFingerprints; i++) {
      arr.add(authorizedFingerprints[i]);
    }
    if (Firebase.RTDB.setArray(&fbdo, "/smart_home/auth/fingerprint", &arr)) {
      Serial.println("Vân tay mới đã được lưu với ID: " + String(id));
      showSetupSuccessScreen();
    } else {
      Serial.println("Lỗi lưu vân tay: " + fbdo.errorReason());
      showSetupFailedScreen();
      numAuthorizedFingerprints--; // Hủy thêm ID nếu lỗi
    }
  } else {
    Serial.println("Danh sách vân tay đầy!");
    showSetupFailedScreen();
  }
}

// Hiển thị màn hình cài đặt thành công
void showSetupSuccessScreen() {
  currentState = SUCCESS_SCREEN;
  lastActionTime = millis();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CAI DAT THANH CONG");
}

// Hiển thị màn hình cài đặt thất bại
void showSetupFailedScreen() {
  currentState = FAILED_SCREEN;
  lastActionTime = millis();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CAI DAT THAT BAI");
}

// Hiển thị màn hình nhập mật khẩu
void showPasswordScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("XAC THUC MAT KHAU");
  
  lcd.setCursor(0, 1);
  String display = "NHAP: ";
  for (int i = 0; i < enteredPassword.length(); i++) {
    display += "*"; // Hiển thị dấu * thay cho ký tự
  }
  lcd.print(display);
  
  lcd.setCursor(0, 2);
  lcd.print("#: XAC THUC/OK"); // Hướng dẫn phím #
  
  lcd.setCursor(0, 3);
  lcd.print("*: XOA/HUY"); // Hướng dẫn phím *
}

// Hiển thị màn hình xác thực RFID
void showRFIDScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("XAC THUC THE RFID");
  
  lcd.setCursor(0, 1);
  lcd.print("VUI LONG QUET THE"); // Yêu cầu quét thẻ
  
  lcd.setCursor(0, 3);
  lcd.print("*: XOA/HUY"); // Hướng dẫn phím *
}

// Hiển thị màn hình xác thực vân tay
void showFingerprintScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("XAC THUC VAN TAY");
  
  lcd.setCursor(0, 1);
  lcd.print("VUI LONG DAT VAN TAY"); // Yêu cầu đặt vân tay
  
  lcd.setCursor(0, 3);
  lcd.print("*: XOA/HUY"); // Hướng dẫn phím *
}

// Kiểm tra mật khẩu
void checkPassword() {
  if (enteredPassword == correctPassword) { // So sánh mật khẩu
    showSuccessScreen();
    openDoor();
  } else {
    showFailedScreen();
  }
  enteredPassword = ""; // Xóa mật khẩu tạm
}

// Kiểm tra thẻ RFID
void checkRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) { // Kiểm tra xem có thẻ mới không
    return;
  }
  
  if (!mfrc522.PICC_ReadCardSerial()) { // Đọc dữ liệu thẻ
    return;
  }
  
  // Lấy UID của thẻ
  String content = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    content += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    content += String(mfrc522.uid.uidByte[i], HEX);
    if (i < mfrc522.uid.size - 1) content += " ";
  }
  content.toUpperCase(); // Chuyển UID sang chữ in hoa
  
  Serial.println("UID của thẻ: " + content);
  
  bool authorized = false;
  for (int i = 0; i < numAuthorizedUIDs; i++) {
    if (content == authorizedUIDs[i]) { // So sánh UID
      authorized = true;
      break;
    }
  }
  
  if (authorized) { // Nếu UID hợp lệ
    showSuccessScreen();
    openDoor();
  } else {
    showFailedScreen();
  }
  
  mfrc522.PICC_HaltA(); // Dừng giao tiếp với thẻ
}

// Kiểm tra vân tay
void checkFingerprint() {
  if (!finger.verifyPassword()) { // Kiểm tra kết nối cảm biến
    Serial.println("Cảm biến vân tay không phản hồi!");
    showFailedScreen();
    return;
  }
  
  uint8_t p = finger.getImage(); // Lấy ảnh vân tay
  if (p != FINGERPRINT_OK) return;
  
  p = finger.image2Tz(); // Chuyển ảnh thành đặc trưng
  if (p != FINGERPRINT_OK) return;
  
  p = finger.fingerFastSearch(); // Tìm kiếm vân tay
  if (p == FINGERPRINT_OK) {
    bool authorized = false;
    for (int i = 0; i < numAuthorizedFingerprints; i++) {
      if (finger.fingerID == authorizedFingerprints[i]) { // So sánh ID vân tay
        authorized = true;
        break;
      }
    }
    if (authorized) {
      Serial.println("Vân tay khớp! ID: " + String(finger.fingerID) + 
                     ", Độ tin cậy: " + String(finger.confidence));
      showSuccessScreen();
      openDoor();
    } else {
      Serial.println("Vân tay không được phép!");
      showFailedScreen();
    }
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Vân tay không khớp!");
    showFailedScreen();
  } else {
    Serial.println("Lỗi đọc vân tay: " + String(p));
  }
}

// Hiển thị màn hình xác thực thành công
void showSuccessScreen() {
  currentState = SUCCESS_SCREEN;
  lastActionTime = millis();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("XAC THUC THANH CONG");
  
  lcd.setCursor(0, 1);
  lcd.print("CUA MO");
}

// Hiển thị màn hình xác thực thất bại
void showFailedScreen() {
  currentState = FAILED_SCREEN;
  lastActionTime = millis();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("XAC THUC THAT BAI");
  
  lcd.setCursor(0, 1);
  lcd.print("CUA DONG");
}

// Điều khiển servo mở/đóng cửa
void openDoor() {
  doorServo.write(180); // Xoay servo đến 180 độ (mở cửa)
  Serial.println("Cửa đã mở!");
  
  delay(2000); // Giữ cửa mở trong 2 giây
  doorServo.write(0); // Đóng cửa
  Serial.println("Cửa đã đóng!");
}