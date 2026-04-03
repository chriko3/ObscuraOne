#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD_MMC.h>
#include <FS.h>
#include <Preferences.h>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cctype>

struct FileEntry {
  String name;
  time_t mtime;
  size_t size;
};

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define LED_GPIO_NUM       4

// Vibrationsmodul am selben Pin wie der Button.
// Wichtig: Nach der Vibration muss der Pin wieder auf INPUT_PULLUP zurück.
#define VIB_GPIO_NUM      BUTTON_PIN

#define BUTTON_PIN        13

#define AP_SSID     "Obscura One"
#define AP_PASS     "12345678"

#define TEMP_ZIP_PATH "/__obscura_all.zip"

static const IPAddress kApIp(192, 168, 4, 1);
static const IPAddress kApGw(192, 168, 4, 1);
static const IPAddress kApMask(255, 255, 255, 0);

static WebServer server(80);
static Preferences prefs;

enum RunMode { MODE_CAPTURE, MODE_SERVER };
static RunMode runMode = MODE_CAPTURE;

static const unsigned long DEBOUNCE_MS = 45;
static const unsigned long LONG_PRESS_MS = 3000;
static const unsigned long DOUBLE_GAP_MS = 450;

// Long-Press-Vibration deaktiviert: der Vibrator hängt am Button-Pin.

static bool btnStable = true;
static bool btnHigh = true;
static unsigned long debounceT = 0;
static unsigned long pressDownT = 0;
static bool longSent = false;
static uint8_t clickCount = 0;
static unsigned long clickWindowEnd = 0;

static bool cameraReady = false;
static bool sdReady = false;

static uint32_t gCrcTab[256];
static bool gCrcInit = false;

static void crcInit() {
  if (gCrcInit) return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >> 1)) : (c >> 1);
    gCrcTab[i] = c;
  }
  gCrcInit = true;
}

static uint32_t crc32Feed(uint32_t crc, const uint8_t *data, size_t n) {
  crcInit();
  for (size_t i = 0; i < n; i++) crc = gCrcTab[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
  return crc;
}

static void zipWriteU16(File &f, uint16_t v) {
  uint8_t b[2] = {(uint8_t)(v & 0xff), (uint8_t)(v >> 8)};
  f.write(b, 2);
}

static void zipWriteU32(File &f, uint32_t v) {
  uint8_t b[4] = {(uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff), (uint8_t)((v >> 16) & 0xff),
                  (uint8_t)((v >> 24) & 0xff)};
  f.write(b, 4);
}

static void cdAppendU16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back((uint8_t)(x & 0xff));
  v.push_back((uint8_t)(x >> 8));
}

static void cdAppendU32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back((uint8_t)(x & 0xff));
  v.push_back((uint8_t)((x >> 8) & 0xff));
  v.push_back((uint8_t)((x >> 16) & 0xff));
  v.push_back((uint8_t)((x >> 24) & 0xff));
}

static void logLine(const char *msg) { Serial.println(msg); }

static void logF(const char *fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
}

static bool endsWithJpg(const char *name) {
  size_t n = strlen(name);
  if (n >= 4 && strcasecmp(name + n - 4, ".jpg") == 0) return true;
  if (n >= 5 && strcasecmp(name + n - 5, ".jpeg") == 0) return true;
  return false;
}

static bool isSafeSdName(const char *name) {
  if (!name || !*name || strchr(name, '/')) return false;
  for (const char *p = name; *p; ++p) {
    char c = *p;
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
  }
  return endsWithJpg(name);
}

static bool initSd() {
  SD_MMC.setPins(14, 15, 2);
  if (!SD_MMC.begin("/sdcard", true)) {
    logLine("[SD] mount failed");
    return false;
  }
  uint8_t card = SD_MMC.cardType();
  if (card == CARD_NONE) {
    logLine("[SD] no card");
    return false;
  }
  logF("[SD] ok type=%u size=%llu MB", (unsigned)card, SD_MMC.cardSize() / (1024 * 1024));
  return true;
}

static bool initCamera() {
  camera_config_t config = {};
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
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_count = 1;
  config.jpeg_quality = 12;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    logF("[CAM] init failed: 0x%x", (unsigned)err);
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, FRAMESIZE_SVGA);
    s->set_quality(s, 10);
  }
  logLine("[CAM] ready");
  return true;
}

static void vibSingle(uint32_t msHigh) {
  pinMode(VIB_GPIO_NUM, OUTPUT);
  digitalWrite(VIB_GPIO_NUM, HIGH);
  delay(msHigh);
  digitalWrite(VIB_GPIO_NUM, LOW);
  if (VIB_GPIO_NUM == BUTTON_PIN) {
    // Vibrator hängt am selben Pin wie der Button.
    // Nach der Vibration muss der Pin wieder als Eingang dienen.
    pinMode(BUTTON_PIN, INPUT_PULLUP);
  }
}

static void vibPattern(uint8_t count, uint32_t msHigh, uint32_t gapMs) {
  if (count == 0) return;
  for (uint8_t i = 0; i < count; i++) {
    vibSingle(msHigh);
    if (i + 1 < count) delay(gapMs);
  }
}

static String nextPhotoPath() {
  prefs.begin("obscura", false);
  uint32_t n = prefs.getUInt("seq", 0);
  n++;
  prefs.putUInt("seq", n);
  prefs.end();
  char name[32];
  snprintf(name, sizeof(name), "/img_%05lu.jpg", (unsigned long)n);
  return String(name);
}

static void discardCameraFrames(int n) {
  for (int i = 0; i < n; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }
}

static bool savePhoto(bool useFlash) {
  if (!cameraReady) {
    logLine("[Photo] camera not ready");
    return false;
  }
  if (!sdReady) {
    logLine("[Photo] SD not ready");
    return false;
  }

  if (useFlash) {
    pinMode(LED_GPIO_NUM, OUTPUT);
    digitalWrite(LED_GPIO_NUM, HIGH);
    delay(250);
  }

  discardCameraFrames(2);
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    logLine("[Photo] empty frame");
    if (useFlash) digitalWrite(LED_GPIO_NUM, LOW);
    return false;
  }

  const size_t jpgLen = fb->len;
  String path = nextPhotoPath();
  File f = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!f) {
    logF("[Photo] write open failed: %s", path.c_str());
    esp_camera_fb_return(fb);
    if (useFlash) digitalWrite(LED_GPIO_NUM, LOW);
    return false;
  }
  size_t written = f.write(fb->buf, jpgLen);
  f.flush();
  f.close();
  esp_camera_fb_return(fb);

  if (useFlash) {
    delay(180);
    digitalWrite(LED_GPIO_NUM, LOW);
  }

  if (written != jpgLen) {
    logF("[Photo] write mismatch %s (%u/%u)", path.c_str(), (unsigned)written, (unsigned)jpgLen);
    return false;
  }
  logF("[Photo] saved %s %u bytes flash=%s", path.c_str(), (unsigned)written, useFlash ? "yes" : "no");

  // Vibrationsmuster immer erst nach dem erfolgreichen Speichern.
  if (useFlash) vibPattern(2, 400, 120);
  else vibPattern(1, 400, 120);

  return true;
}

static uint32_t seqFromImgName(const String &name) {
  if (!name.startsWith("img_")) return 0;
  int dot = name.lastIndexOf('.');
  if (dot <= 4) return 0;
  return (uint32_t)name.substring(4, dot).toInt();
}

static int compareEntries(const void *a, const void *b) {
  const FileEntry *fa = (const FileEntry *)a;
  const FileEntry *fb = (const FileEntry *)b;
  uint32_t sa = seqFromImgName(fa->name);
  uint32_t sb = seqFromImgName(fb->name);
  if (sa != sb) {
    if (sa > sb) return -1;
    if (sa < sb) return 1;
  }
  return fb->name.compareTo(fa->name);
}

static void collectFiles(fs::FS &fs, const char *dirname, std::vector<FileEntry> &out) {
  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) return;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String fn = String(file.name());
      if (fn.startsWith("/")) fn = fn.substring(1);
      if (isSafeSdName(fn.c_str())) {
        FileEntry e;
        e.name = fn;
        e.mtime = file.getLastWrite();
        e.size = file.size();
        out.push_back(e);
      }
    }
    file = root.openNextFile();
  }
}

static bool buildZipOnSd(const char *zipPath, std::vector<FileEntry> &entries) {
  SD_MMC.remove(zipPath);
  File out = SD_MMC.open(zipPath, FILE_WRITE);
  if (!out) {
    logLine("[Zip] cannot create temp file");
    return false;
  }

  std::vector<uint8_t> centralDir;
  const uint16_t nfiles = (uint16_t)entries.size();

  for (uint16_t fi = 0; fi < nfiles; fi++) {
    const FileEntry &e = entries[fi];
    String path = "/" + e.name;
    File src = SD_MMC.open(path.c_str(), FILE_READ);
    if (!src) {
      out.close();
      SD_MMC.remove(zipPath);
      logF("[Zip] open failed: %s", path.c_str());
      return false;
    }

    const uint32_t localOff = (uint32_t)out.position();
    const uint16_t fnLen = (uint16_t)e.name.length();

    zipWriteU32(out, 0x04034b50u);
    zipWriteU16(out, 10);
    zipWriteU16(out, 0x0008);
    zipWriteU16(out, 0);
    zipWriteU16(out, 0);
    zipWriteU16(out, 0);
    zipWriteU32(out, 0);
    zipWriteU32(out, 0);
    zipWriteU32(out, 0);
    zipWriteU16(out, fnLen);
    zipWriteU16(out, 0);
    out.write((const uint8_t *)e.name.c_str(), fnLen);

    uint32_t crcAcc = 0xffffffffu;
    size_t total = 0;
    uint8_t buf[1024];
    for (;;) {
      size_t rd = src.read(buf, sizeof buf);
      if (!rd) break;
      total += rd;
      crcAcc = crc32Feed(crcAcc, buf, rd);
      size_t w = out.write(buf, rd);
      if (w != rd) {
        src.close();
        out.close();
        SD_MMC.remove(zipPath);
        logLine("[Zip] write error");
        return false;
      }
    }
    src.close();

    const uint32_t crcFinal = crcAcc ^ 0xffffffffu;
    const uint32_t usize = (uint32_t)total;
    zipWriteU32(out, 0x08074b50u);
    zipWriteU32(out, crcFinal);
    zipWriteU32(out, usize);
    zipWriteU32(out, usize);

    cdAppendU32(centralDir, 0x02014b50u);
    cdAppendU16(centralDir, 0x0314);
    cdAppendU16(centralDir, 10);
    cdAppendU16(centralDir, 0x0008);
    cdAppendU16(centralDir, 0);
    cdAppendU16(centralDir, 0);
    cdAppendU16(centralDir, 0);
    cdAppendU32(centralDir, crcFinal);
    cdAppendU32(centralDir, usize);
    cdAppendU32(centralDir, usize);
    cdAppendU16(centralDir, fnLen);
    cdAppendU16(centralDir, 0);
    cdAppendU16(centralDir, 0);
    cdAppendU16(centralDir, 0);
    cdAppendU16(centralDir, 0);
    cdAppendU32(centralDir, 0);
    cdAppendU32(centralDir, localOff);
    for (uint16_t i = 0; i < fnLen; i++) centralDir.push_back((uint8_t)e.name[i]);
  }

  const uint32_t cdOff = (uint32_t)out.position();
  out.write(centralDir.data(), centralDir.size());
  const uint32_t cdSize = (uint32_t)centralDir.size();

  zipWriteU32(out, 0x06054b50u);
  zipWriteU16(out, 0);
  zipWriteU16(out, 0);
  zipWriteU16(out, nfiles);
  zipWriteU16(out, nfiles);
  zipWriteU32(out, cdSize);
  zipWriteU32(out, cdOff);
  zipWriteU16(out, 0);

  out.flush();
  out.close();
  logF("[Zip] built %s (%u files)", zipPath, (unsigned)nfiles);
  return true;
}

static void sendIndexHtml() {
  std::vector<FileEntry> entries;
  if (sdReady) collectFiles(SD_MMC, "/", entries);
  if (!entries.empty()) qsort(entries.data(), entries.size(), sizeof(FileEntry), compareEntries);

  String html;
  html.reserve(6000 + entries.size() * 400);
  html += F("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
             "content=\"width=device-width,initial-scale=1\"><title>Obscura One</title>"
             "<style>:root{--bg:#0c0d10;--card:#16181f;--b:#252836;--a:#6c9cff;--t:#e8eaef;--m:#8b90a0}"
             "*{box-sizing:border-box}body{margin:0;font-family:system-ui,Segoe UI,sans-serif;"
             "background:radial-gradient(1200px 800px at 20% -10%,#1a1f2e 0%,var(--bg)55%);"
             "color:var(--t);min-height:100vh}"
             "header{padding:1.25rem 1.5rem;border-bottom:1px solid var(--b);display:flex;"
             "align-items:center;justify-content:space-between;flex-wrap:wrap;gap:.75rem}"
             "h1{font-size:1.35rem;margin:0;letter-spacing:.02em}"
             ".sub{color:var(--m);font-size:.85rem}"
             ".wrap{max-width:960px;margin:0 auto;padding:1.25rem 1.5rem 2.5rem}"
             ".toolbar{margin-bottom:1.25rem;padding:1rem;border:1px solid var(--b);border-radius:14px;"
             "background:var(--card);display:flex;flex-wrap:wrap;gap:.65rem;align-items:center}"
             ".grid{display:grid;gap:1rem}"
             "@media(min-width:640px){.grid{grid-template-columns:repeat(2,1fr)}}"
             ".card{background:var(--card);border:1px solid var(--b);border-radius:14px;overflow:hidden;"
             "box-shadow:0 8px 30px rgba(0,0,0,.35)}"
             ".thumb{width:100%;aspect-ratio:4/3;object-fit:cover;background:#000;display:block}"
             ".meta{padding:.85rem 1rem;display:flex;flex-direction:column;gap:.4rem}"
             ".row{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center}"
             "a.btn,button.btn{display:inline-flex;align-items:center;justify-content:center;"
             "padding:.45rem .85rem;border-radius:10px;font-size:.88rem;text-decoration:none;border:none;"
             "cursor:pointer;font-weight:600}"
             "a.pri{background:var(--a);color:#0a0c12}a.sec{background:var(--b);color:var(--t)}"
             "button.dan{background:#3a2430;color:#ffb4c0;border:1px solid #5c3846}"
             ".empty{text-align:center;padding:3rem 1rem;color:var(--m)}"
             "</style></head><body><header><div><h1>Obscura One</h1>"
             "<div class=\"sub\">Gallery · newest first</div></div></header><div class=\"wrap\">");

  if (entries.empty()) {
    html += F("<div class=\"empty\">No photos on the SD card.</div>");
  } else {
    html += F("<div class=\"toolbar\">"
             "<a class=\"btn pri\" href=\"/download-all\">Download all</a>"
             "<form method=\"POST\" action=\"/delete-all\" style=\"display:inline\" "
             "onsubmit=\"return confirm('Delete ALL photos? This cannot be undone.');\">"
             "<button type=\"submit\" class=\"btn dan\">Delete all</button></form></div>");
    html += F("<div class=\"grid\">");
    for (const auto &e : entries) {
      String enc = e.name;
      enc.replace("&", "&amp;");
      enc.replace("\"", "&quot;");
      enc.replace("<", "&lt;");
      String nameEsc = e.name;
      nameEsc.replace("&", "%26");

      html += F("<div class=\"card\">");
      html += F("<img class=\"thumb\" src=\"/image?name=");
      html += nameEsc;
      html += F("\" alt=\"\">");
      html += F("<div class=\"meta\"><strong>");
      html += enc;
      html += F("</strong><span class=\"sub\">");
      html += String((unsigned)(e.size / 1024));
      html += F(" KB</span><div class=\"row\">");
      html += F("<a class=\"btn pri\" href=\"/download?name=");
      html += nameEsc;
      html += F("\">Download</a>");
      html += F("<form method=\"POST\" action=\"/delete\" onsubmit=\"return confirm('Delete this photo?');\">");
      html += F("<input type=\"hidden\" name=\"name\" value=\"");
      html += e.name;
      html += F("\"><button type=\"submit\" class=\"btn dan\">Delete</button></form>");
      html += F("</div></div></div>");
    }
    html += F("</div>");
  }

  html += F("</div></body></html>");
  server.send(200, "text/html; charset=utf-8", html);
}

static void handleRoot() { sendIndexHtml(); }

static void handleImage() {
  if (!sdReady) {
    server.send(500, "text/plain", "SD error");
    return;
  }
  String name = server.arg("name");
  if (!isSafeSdName(name.c_str())) {
    server.send(400, "text/plain", "bad name");
    return;
  }
  String path = "/" + name;
  if (!SD_MMC.exists(path)) {
    server.send(404, "text/plain", "not found");
    return;
  }
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "open failed");
    return;
  }
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.streamFile(f, "image/jpeg");
  f.close();
}

static void handleDownload() {
  if (!sdReady) {
    server.send(500, "text/plain", "SD error");
    return;
  }
  String name = server.arg("name");
  if (!isSafeSdName(name.c_str())) {
    server.send(400, "text/plain", "bad name");
    return;
  }
  String path = "/" + name;
  if (!SD_MMC.exists(path)) {
    server.send(404, "text/plain", "not found");
    return;
  }
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "open failed");
    return;
  }
  String disp = "attachment; filename=\"" + name + "\"";
  server.sendHeader("Content-Disposition", disp);
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

static void handleDownloadAll() {
  if (!sdReady) {
    server.send(500, "text/plain", "SD error");
    return;
  }
  std::vector<FileEntry> entries;
  collectFiles(SD_MMC, "/", entries);
  if (entries.empty()) {
    server.send(404, "text/plain", "no photos");
    return;
  }
  qsort(entries.data(), entries.size(), sizeof(FileEntry), compareEntries);

  if (!buildZipOnSd(TEMP_ZIP_PATH, entries)) {
    server.send(500, "text/plain", "zip failed");
    return;
  }

  File f = SD_MMC.open(TEMP_ZIP_PATH, FILE_READ);
  if (!f) {
    SD_MMC.remove(TEMP_ZIP_PATH);
    server.send(500, "text/plain", "zip read failed");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"obscura_photos.zip\"");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.streamFile(f, "application/zip");
  f.close();
  SD_MMC.remove(TEMP_ZIP_PATH);
  logLine("[Web] sent download-all zip");
}

static void handleDelete() {
  if (!sdReady) {
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  String name = server.arg("name");
  if (!isSafeSdName(name.c_str())) {
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  String path = "/" + name;
  if (SD_MMC.exists(path)) {
    SD_MMC.remove(path);
    logF("[Web] deleted %s", path.c_str());
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleDeleteAll() {
  if (!sdReady) {
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  std::vector<FileEntry> entries;
  collectFiles(SD_MMC, "/", entries);
  int n = 0;
  for (const auto &e : entries) {
    String path = "/" + e.name;
    if (SD_MMC.exists(path)) {
      SD_MMC.remove(path);
      n++;
    }
  }
  logF("[Web] delete-all removed %d file(s)", n);
  server.sendHeader("Location", "/");
  server.send(303);
}

static void startApAndServer() {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(kApIp, kApGw, kApMask)) logLine("[WiFi] softAPConfig warning");
  bool apOk = WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  IPAddress ip = WiFi.softAPIP();
  logF("[WiFi] AP \"%s\" %s IP %s (static %s)", AP_SSID, apOk ? "up" : "ERROR", ip.toString().c_str(),
       kApIp.toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/image", HTTP_GET, handleImage);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/download-all", HTTP_GET, handleDownloadAll);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/delete-all", HTTP_POST, handleDeleteAll);
  server.begin();
  logLine("[Web] server on port 80 (AP mode, button capture disabled)");
}

static void stopServer() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  SD_MMC.remove(TEMP_ZIP_PATH);
  logLine("[Web] server stopped, capture mode");
}

static void toggleServerMode() {
  if (runMode == MODE_CAPTURE) {
    runMode = MODE_SERVER;
    startApAndServer();
  } else {
    runMode = MODE_CAPTURE;
    stopServer();
  }
}

static void onShortClickSequenceDone(uint8_t count) {
  if (runMode != MODE_CAPTURE) return;
  if (!sdReady || !cameraReady) {
    logLine("[Button] SD/camera not ready");
    return;
  }
  if (count >= 2) savePhoto(true);
  else savePhoto(false);
}

static void processButton(unsigned long now) {
  bool raw = digitalRead(BUTTON_PIN) == HIGH;
  if (raw != btnHigh) {
    if (debounceT == 0) debounceT = now;
  } else {
    debounceT = 0;
  }
  if (debounceT && (now - debounceT > DEBOUNCE_MS)) {
    btnHigh = raw;
    btnStable = true;
    debounceT = 0;
    if (!btnHigh) {
      pressDownT = now;
      longSent = false;
    } else {
      unsigned long held = now - pressDownT;
      if (!longSent) {
        if (held < LONG_PRESS_MS) {
          clickCount++;
          clickWindowEnd = now + DOUBLE_GAP_MS;
        }
      }
    }
  }

  if (!btnHigh && !longSent && (now - pressDownT >= LONG_PRESS_MS)) {
    longSent = true;
    if (runMode == MODE_CAPTURE) logLine("[Button] 3s hold: start web server");
    else logLine("[Button] 3s hold: stop web server");
    toggleServerMode();
    clickCount = 0;
    clickWindowEnd = 0;
  }

  if (btnHigh && clickWindowEnd && now > clickWindowEnd) {
    uint8_t c = clickCount;
    clickCount = 0;
    clickWindowEnd = 0;
    if (c == 1) onShortClickSequenceDone(1);
    else if (c >= 2) onShortClickSequenceDone(2);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  logLine("=== Obscura One start ===");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW);

  cameraReady = initCamera();
  sdReady = initSd();

  logLine("[Setup] single=photo double=flash+photo 3s-hold=toggle server");
}

void loop() {
  unsigned long now = millis();

  if (runMode == MODE_SERVER) {
    server.handleClient();
  } else {
    processButton(now);
  }

  if (runMode == MODE_SERVER) {
    bool raw = digitalRead(BUTTON_PIN) == HIGH;
    static unsigned long dbt = 0;
    static bool stableHigh = true;
    static unsigned long downT = 0;
    static bool longDone = false;

    if (raw != stableHigh) {
      if (dbt == 0) dbt = now;
    } else {
      dbt = 0;
    }
    if (dbt && now - dbt > DEBOUNCE_MS) {
      stableHigh = raw;
      dbt = 0;
      if (!stableHigh) {
        downT = now;
        longDone = false;
      }
    }
    if (!stableHigh && !longDone && (now - downT >= LONG_PRESS_MS)) {
      longDone = true;
      logLine("[Button] 3s hold while server on: stop server");
      toggleServerMode();
    }
    if (stableHigh) longDone = false;
  }
}
