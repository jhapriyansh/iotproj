#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include "img_converters.h"

#define FLASH_PIN 4
#define BRIGHTNESS_THRESHOLD 60 // 0–255 scale; tune this after testing

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// ===== CONFIG =====
const char *ssid = "trex";
const char *password = "pkj1554vitc*@";
#define SERVER_URL "http://192.168.0.100:8080/upload"
#define COLLISION_URL "http://192.168.0.100:8080/collision"

// Pin assignments (free GPIOs on AI-Thinker: 2, 12, 13, 14, 15)
#define BUTTON_PIN 15
#define TRIG_PIN 13
#define ECHO_PIN 12
#define BUZZER_PIN 14
#define MOTOR_PIN 2
// ==================

bool lastButton = HIGH;

// Collision alert cooldown
unsigned long lastCollisionSent = 0;
#define COLLISION_COOLDOWN 3000 // ms between server alerts

// Non-blocking buzzer timing
unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 BOOT ===");

  // Peripheral pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);

  // OFF initially (active-LOW peripherals)
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(MOTOR_PIN, HIGH);

  // FLASH Config
  pinMode(FLASH_PIN, OUTPUT);
  digitalWrite(FLASH_PIN, LOW); // flash is active-HIGH, LOW = off

  // Camera config
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
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

  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_VGA;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("❌ Camera init failed: 0x%x\n", err);
    while (true)
      delay(1000);
  }
  Serial.println("✅ Camera initialized");

  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("blind-assist"))
  {
    Serial.println("✅ mDNS ready: blind-assist.local");
  }
  else
  {
    Serial.println("❌ mDNS failed");
  }

  Serial.println("✅ Ultrasonic + buzzer + motor ready");
}

// Rough average luminance estimate from an RGB565 framebuffer.
// Samples every Nth pixel for speed rather than the whole frame.
int estimateBrightness(camera_fb_t *fb)
{
  if (!fb || fb->format != PIXFORMAT_RGB565) return 255; // assume bright if unknown

  uint16_t *pixels = (uint16_t *)fb->buf;
  size_t pixelCount = fb->len / 2;
  size_t step = 37; // prime-ish stride, avoids sampling a repeating pattern

  long sum = 0;
  int samples = 0;

  for (size_t i = 0; i < pixelCount; i += step)
  {
    uint16_t p = pixels[i];
    // RGB565: RRRRR GGGGGG BBBBB
    uint8_t r = (p >> 11) & 0x1F;
    uint8_t g = (p >> 5) & 0x3F;
    uint8_t b = p & 0x1F;

    // Normalize to 0–255 and combine via luma-ish weights
    uint8_t r8 = (r * 255) / 31;
    uint8_t g8 = (g * 255) / 63;
    uint8_t b8 = (b * 255) / 31;

    int luma = (int)(0.299 * r8 + 0.587 * g8 + 0.114 * b8);
    sum += luma;
    samples++;
  }

  if (samples == 0) return 255;
  return sum / samples;
}

// ── Ultrasonic distance (cm, 0 = no echo) ──
long getDistance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return duration * 0.034 / 2;
}

// ── Send collision alert to server (with cooldown) ──
void sendCollisionAlert(int distance)
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  HTTPClient http;
  http.begin(COLLISION_URL);
  http.addHeader("Content-Type", "application/json");

  String body = "{\"distance\":" + String(distance) + "}";
  int code = http.POST(body);
  Serial.printf("⚠️  Collision alert sent (HTTP %d, dist %dcm)\n", code, distance);

  http.end();
}

void loop()
{
  unsigned long now = millis();

  // ── Button check (capture photo – one shot per press) ──
  bool button = digitalRead(BUTTON_PIN);
  if (lastButton == HIGH && button == LOW)
  {
    Serial.println("📸 Button pressed");
    captureAndSend();

    // Wait for full release before resuming (debounce)
    while (digitalRead(BUTTON_PIN) == LOW)
      delay(10);
    delay(200); // extra settle time
  }
  lastButton = digitalRead(BUTTON_PIN);

  // ── Ultrasonic distance ──
  int distance = getDistance();

  if (distance > 10 || distance == 0)
  {
    // SAFE – everything off
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(MOTOR_PIN, HIGH);
    buzzerState = false;
  }
  else if (distance > 5)
  {
    // WARNING (5–10 cm) – slow beep, no motor
    digitalWrite(MOTOR_PIN, HIGH);
    if (now - lastBuzzerToggle >= 500)
    {
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? LOW : HIGH);
      lastBuzzerToggle = now;
    }
  }
  else
  {
    // DANGER (≤5 cm) – fast beep + vibration motor
    digitalWrite(MOTOR_PIN, LOW);
    if (now - lastBuzzerToggle >= 100)
    {
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? LOW : HIGH);
      lastBuzzerToggle = now;
    }
    // Send collision alert (throttled)
    if (now - lastCollisionSent >= COLLISION_COOLDOWN)
    {
      sendCollisionAlert(distance);
      lastCollisionSent = now;
    }
  }

  delay(50);
}

// ── Capture JPEG and POST to server ──
void captureAndSend()
{
  camera_fb_t *fb = nullptr;

  // Flush old frame
  fb = esp_camera_fb_get();
  if (fb)
    esp_camera_fb_return(fb);
  delay(30);

  // Capture a frame to check ambient brightness
  fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("❌ Capture failed");
    return;
  }

  int brightness = estimateBrightness(fb);
  Serial.printf("💡 Brightness estimate: %d\n", brightness);

  bool usedFlash = false;
  if (brightness < BRIGHTNESS_THRESHOLD)
  {
    Serial.println("🔦 Low light detected — enabling flash");
    esp_camera_fb_return(fb); // discard the dark frame
    digitalWrite(FLASH_PIN, HIGH);
    delay(100); // let the sensor's auto-exposure settle under new light

    fb = esp_camera_fb_get(); // re-capture with flash on
    usedFlash = true;
  }

  if (!fb)
  {
    Serial.println("❌ Capture failed (post-flash)");
    digitalWrite(FLASH_PIN, LOW);
    return;
  }

  uint8_t *jpg_buf = nullptr;
  size_t jpg_len = 0;

  bool ok = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
  esp_camera_fb_return(fb);

  if (usedFlash)
  {
    digitalWrite(FLASH_PIN, LOW); // turn flash off promptly, don't drain battery
  }

  if (!ok || jpg_len == 0)
  {
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