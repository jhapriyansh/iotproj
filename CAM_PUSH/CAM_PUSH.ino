#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "img_converters.h"

// ===========================
// Camera model
// ===========================
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// ===========================
// WiFi credentials
// ===========================
const char* ssid = "Pixel_4477";
const char* password = "4rs3gdfznnvncmt";

// ===========================
// Button + Server
// ===========================
#define BUTTON_PIN 13
const char* SERVER_URL = "http://172.20.73.180:8000/upload";

// ===========================
// Setup (UNCHANGED CORE LOGIC)
// ===========================
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.println();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;

  // 🔒 DO NOT CHANGE (stable)
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count     = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed: 0x%x\n", err);
    return;
  }

  WiFi.begin(ssid, password);
  Serial.print("📶 Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

// ===========================
// Capture latest frame → send
// ===========================
void captureAndSend() {
  camera_fb_t* fb = NULL;

  // 1️⃣ Flush old buffered frame
  fb = esp_camera_fb_get();
  if (fb) esp_camera_fb_return(fb);

  delay(30);  // allow fresh frame

  // 2️⃣ Capture latest frame
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Capture failed");
    return;
  }

  uint8_t* jpg_buf = NULL;
  size_t jpg_len = 0;

  bool ok = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
  esp_camera_fb_return(fb);

  if (!ok) {
    Serial.println("❌ JPEG conversion failed");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "image/jpeg");

  int httpCode = http.POST(jpg_buf, jpg_len);

  Serial.print("📤 HTTP response: ");
  Serial.println(httpCode);

  http.end();
  free(jpg_buf);
}

// ===========================
// Loop (button edge detect)
// ===========================
void loop() {
  static bool lastState = HIGH;
  bool currentState = digitalRead(BUTTON_PIN);

  if (lastState == HIGH && currentState == LOW) {
    Serial.println("🔘 Button pressed");
    captureAndSend();
    delay(400); // debounce
  }

  lastState = currentState;
}
