#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include "img_converters.h"

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// ===== CONFIG =====
const char* ssid = "WiFi_SSID";
const char* password = "WiFi_PassKey";
#define SERVER_URL "http://blind-assist.local:8000/upload"
#define BUTTON_PIN 13
// ==================

bool lastButton = HIGH;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 BOOT ===");

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;

  // 🔒 DO NOT CHANGE (stable)
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size   = FRAMESIZE_VGA;
  config.fb_count     = 1;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed: 0x%x\n", err);
    while (true) delay(1000);
  }
  Serial.println("✅ Camera initialized");

  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("blind-assist")) {
    Serial.println("✅ mDNS ready: blind-assist.local");
  } else {
    Serial.println("❌ mDNS failed");
  }
}

void loop() {
  bool button = digitalRead(BUTTON_PIN);

  if (lastButton == HIGH && button == LOW) {
    Serial.println("📸 Button pressed");
    captureAndSend();
    delay(600);
  }

  lastButton = button;
}

void captureAndSend() {
  camera_fb_t* fb = nullptr;

  // Flush old frame
  fb = esp_camera_fb_get();
  if (fb) esp_camera_fb_return(fb);
  delay(30);

  // Capture fresh frame
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Capture failed");
    return;
  }

  uint8_t* jpg_buf = nullptr;
  size_t jpg_len = 0;

  bool ok = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
  esp_camera_fb_return(fb);

  if (!ok || jpg_len == 0) {
    Serial.println("❌ JPEG conversion failed");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "image/jpeg");

  int code = http.POST(jpg_buf, jpg_len);
  Serial.printf("📤 Upload result: %d\n", code);

  http.end();
  free(jpg_buf);
}
