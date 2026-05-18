#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_err.h"

#include <errno.h>
#include <fcntl.h>
#include <lwip/inet.h>
#include <lwip/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Pinout for ESP32-S3 WROOM N16R8 CAM boards compatible with
// ESP32-S3-EYE / Keyestudio MB0184 / DIYables-style OV3660 wiring.
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   15
#define CAM_PIN_SIOD   4
#define CAM_PIN_SIOC   5

#define CAM_PIN_D0     11  // Y2
#define CAM_PIN_D1     9   // Y3
#define CAM_PIN_D2     8   // Y4
#define CAM_PIN_D3     10  // Y5
#define CAM_PIN_D4     12  // Y6
#define CAM_PIN_D5     18  // Y7
#define CAM_PIN_D6     17  // Y8
#define CAM_PIN_D7     16  // Y9

#define CAM_PIN_VSYNC  6
#define CAM_PIN_HREF   7
#define CAM_PIN_PCLK   13

#define CAMERA_VFLIP_ENABLED    0
#define CAMERA_HMIRROR_ENABLED  0

// Start stable. After video appears, try FRAMESIZE_VGA.
#define CAMERA_FRAME_SIZE       FRAMESIZE_QVGA
#define CAMERA_JPEG_QUALITY     14
#define CAMERA_FB_COUNT         2
#define CAMERA_XCLK_HZ          20000000

#define HTTP_PORT               80
#define MAX_STREAM_CLIENTS      4
#define STREAM_TARGET_FPS       15
#define SOCKET_SEND_TIMEOUT_MS  1000
#define SOCKET_RECV_TIMEOUT_MS  700

static const char* STREAM_BOUNDARY = "esp32s3ov3660";

int listenFd = -1;
int streamFds[MAX_STREAM_CLIENTS] = {-1, -1, -1, -1};
uint32_t streamIds[MAX_STREAM_CLIENTS] = {0, 0, 0, 0};
uint32_t nextStreamId = 1;
uint32_t lastFrameAt = 0;
uint32_t lastStatsAt = 0;
uint32_t statsFrames = 0;
uint32_t statsBytes = 0;

void haltWithMessage(const char* message) {
  Serial.println(message);
  Serial.println("Stopped. Fix the configuration and reset the board.");
  while (true) {
    delay(1000);
  }
}

void closeFd(int& fd) {
  if (fd >= 0) {
    shutdown(fd, SHUT_RDWR);
    close(fd);
    fd = -1;
  }
}

void setSocketTimeouts(int fd) {
  int yes = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

  timeval tv = {};
  tv.tv_sec = 0;
  tv.tv_usec = SOCKET_SEND_TIMEOUT_MS * 1000;
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  tv.tv_sec = 0;
  tv.tv_usec = SOCKET_RECV_TIMEOUT_MS * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

bool sendAll(int fd, const uint8_t* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    int written = send(fd, data + sent, len - sent, 0);
    if (written > 0) {
      sent += (size_t)written;
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
  return true;
}

bool sendText(int fd, const char* text) {
  return sendAll(fd, (const uint8_t*)text, strlen(text));
}

int activeStreamCount() {
  int count = 0;
  for (int i = 0; i < MAX_STREAM_CLIENTS; ++i) {
    if (streamFds[i] >= 0) {
      count++;
    }
  }
  return count;
}

int findFreeStreamSlot() {
  for (int i = 0; i < MAX_STREAM_CLIENTS; ++i) {
    if (streamFds[i] < 0) {
      return i;
    }
  }
  return -1;
}

void removeStreamClient(int slot, const char* reason) {
  if (slot < 0 || slot >= MAX_STREAM_CLIENTS || streamFds[slot] < 0) {
    return;
  }
  Serial.printf("Stream client #%u disconnected: %s. Active: %d/%d\n",
                (unsigned)streamIds[slot],
                reason,
                max(0, activeStreamCount() - 1),
                MAX_STREAM_CLIENTS);
  closeFd(streamFds[slot]);
  streamIds[slot] = 0;
}

void printChipInfo() {
  Serial.println();
  Serial.println("ESP32-S3 OV3660 MJPEG stream");
  Serial.printf("Chip: %s rev %u, cores: %u\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("Flash size: %u bytes\n", (unsigned)ESP.getFlashChipSize());
  Serial.printf("Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.printf("PSRAM found: %s\n", psramFound() ? "yes" : "no");
  Serial.printf("PSRAM size: %u bytes\n", (unsigned)ESP.getPsramSize());
  Serial.printf("Free PSRAM: %u bytes\n", (unsigned)ESP.getFreePsram());
}

bool initCamera() {
  const bool hasPsram = psramFound() && ESP.getPsramSize() > 0;

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_pclk = CAM_PIN_PCLK;
  config.xclk_freq_hz = CAMERA_XCLK_HZ;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = CAMERA_FRAME_SIZE;
  config.jpeg_quality = CAMERA_JPEG_QUALITY;
  config.fb_count = hasPsram ? CAMERA_FB_COUNT : 1;
  config.fb_location = hasPsram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("esp_camera_init failed: 0x%x (%s)\n",
                  (unsigned)err, esp_err_to_name(err));
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor) {
    Serial.println("Camera initialized, but sensor handle is null.");
    return false;
  }

  Serial.printf("Camera PID: 0x%04x\n", sensor->id.PID);
  sensor->set_brightness(sensor, 0);
  sensor->set_contrast(sensor, 0);
  sensor->set_saturation(sensor, -1);
  sensor->set_gain_ctrl(sensor, 1);
  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_whitebal(sensor, 1);
  sensor->set_awb_gain(sensor, 1);
  sensor->set_wb_mode(sensor, 0);
  sensor->set_ae_level(sensor, 0);
  sensor->set_hmirror(sensor, CAMERA_HMIRROR_ENABLED);
  sensor->set_vflip(sensor, CAMERA_VFLIP_ENABLED);

  if (sensor->id.PID == OV3660_PID) {
    Serial.println("OV3660 detected.");
    sensor->set_saturation(sensor, -2);
  }

  Serial.println("Camera initialized.");
  return true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to Wi-Fi SSID '%s'", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("Wi-Fi connected. IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Open this only: http://%s/\n", WiFi.localIP().toString().c_str());
}

void startSocketServer() {
  listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (listenFd < 0) {
    haltWithMessage("socket() failed.");
  }

  int yes = 1;
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(HTTP_PORT);

  if (bind(listenFd, (sockaddr*)&address, sizeof(address)) < 0) {
    closeFd(listenFd);
    haltWithMessage("bind() failed on port 80.");
  }

  if (listen(listenFd, 8) < 0) {
    closeFd(listenFd);
    haltWithMessage("listen() failed.");
  }

  int flags = fcntl(listenFd, F_GETFL, 0);
  fcntl(listenFd, F_SETFL, flags | O_NONBLOCK);
  Serial.printf("HTTP/MJPEG socket server started on port %u\n", HTTP_PORT);
}

void sendRootPage(int fd) {
  String body;
  body.reserve(1800);
  body += F("<!doctype html><html><head><meta charset=\"utf-8\">");
  body += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  body += F("<title>ESP32-S3 Camera</title>");
  body += F("<style>body{margin:0;background:#111;color:#eee;font-family:Arial,system-ui}");
  body += F("main{max-width:960px;margin:0 auto;padding:16px}");
  body += F("img{display:block;width:100%;height:auto;background:#000}");
  body += F(".meta{color:#aaa;font-size:14px;margin:8px 0 14px}</style></head><body><main>");
  body += F("<h1>ESP32-S3 Camera</h1>");
  body += F("<div class=\"meta\">");
  body += WiFi.localIP().toString();
  body += F(" | viewers: ");
  body += String(activeStreamCount());
  body += F("/");
  body += String(MAX_STREAM_CLIENTS);
  body += F("</div>");
  body += F("<img id=\"stream\" src=\"/stream?start=");
  body += String(millis());
  body += F("\" alt=\"camera stream\">");
  body += F("<script>");
  body += F("const img=document.getElementById('stream');");
  body += F("let timer=0;");
  body += F("function restart(){clearTimeout(timer);timer=setTimeout(()=>{img.src='/stream?t='+Date.now();},700);}");
  body += F("img.onerror=restart;");
  body += F("document.addEventListener('visibilitychange',()=>{if(!document.hidden)restart();});");
  body += F("setInterval(()=>{img.src='/stream?t='+Date.now();},60000);");
  body += F("</script>");
  body += F("</main></body></html>");

  char header[220];
  snprintf(header, sizeof(header),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html; charset=utf-8\r\n"
           "Cache-Control: no-cache, no-store, must-revalidate\r\n"
           "Connection: close\r\n"
           "Content-Length: %u\r\n\r\n",
           (unsigned)body.length());
  sendText(fd, header);
  sendAll(fd, (const uint8_t*)body.c_str(), body.length());
}

void sendPlain(int fd, const char* status, const char* body) {
  char header[180];
  snprintf(header, sizeof(header),
           "HTTP/1.1 %s\r\n"
           "Content-Type: text/plain; charset=utf-8\r\n"
           "Connection: close\r\n"
           "Content-Length: %u\r\n\r\n",
           status,
           (unsigned)strlen(body));
  sendText(fd, header);
  sendText(fd, body);
}

void sendCapture(int fd) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    sendPlain(fd, "503 Service Unavailable", "Camera capture failed.\n");
    Serial.println("Capture failed: esp_camera_fb_get returned null.");
    return;
  }

  Serial.printf("Capture OK: %u bytes\n", (unsigned)fb->len);
  char header[180];
  snprintf(header, sizeof(header),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: image/jpeg\r\n"
           "Cache-Control: no-cache, no-store, must-revalidate\r\n"
           "Connection: close\r\n"
           "Content-Length: %u\r\n\r\n",
           (unsigned)fb->len);
  sendText(fd, header);
  sendAll(fd, fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void attachStreamClient(int fd, const char* remoteIp) {
  int slot = findFreeStreamSlot();
  if (slot < 0) {
    sendPlain(fd, "503 Service Unavailable", "Too many viewers.\n");
    close(fd);
    Serial.println("Rejected stream client: max viewers reached.");
    return;
  }

  char header[240];
  snprintf(header, sizeof(header),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: multipart/x-mixed-replace; boundary=%s\r\n"
           "Cache-Control: no-cache, no-store, must-revalidate\r\n"
           "Pragma: no-cache\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Connection: close\r\n\r\n",
           STREAM_BOUNDARY);

  if (!sendText(fd, header)) {
    close(fd);
    return;
  }

  streamFds[slot] = fd;
  streamIds[slot] = nextStreamId++;
  Serial.printf("Stream client #%u connected from %s. Active: %d/%d\n",
                (unsigned)streamIds[slot],
                remoteIp,
                activeStreamCount(),
                MAX_STREAM_CLIENTS);
}

void handleAcceptedClient(int fd, sockaddr_in& remote) {
  setSocketTimeouts(fd);
  char request[768];
  int len = recv(fd, request, sizeof(request) - 1, 0);
  if (len <= 0) {
    close(fd);
    return;
  }
  request[len] = '\0';

  char method[8] = {};
  char path[160] = {};
  sscanf(request, "%7s %159s", method, path);
  const char* remoteIp = inet_ntoa(remote.sin_addr);
  Serial.printf("HTTP %s %s from %s\n", method, path, remoteIp);

  if (strcmp(method, "GET") != 0) {
    sendPlain(fd, "405 Method Not Allowed", "Only GET is supported.\n");
    close(fd);
    return;
  }

  if (strcmp(path, "/") == 0 || strncmp(path, "/?", 2) == 0) {
    sendRootPage(fd);
    close(fd);
  } else if (strcmp(path, "/stream") == 0 || strncmp(path, "/stream?", 8) == 0) {
    attachStreamClient(fd, remoteIp);
  } else if (strcmp(path, "/capture") == 0 || strncmp(path, "/capture?", 9) == 0) {
    sendCapture(fd);
    close(fd);
  } else if (strcmp(path, "/favicon.ico") == 0) {
    sendPlain(fd, "404 Not Found", "");
    close(fd);
  } else {
    sendPlain(fd, "404 Not Found", "Use / or /stream.\n");
    close(fd);
  }
}

void acceptNewClients() {
  while (true) {
    sockaddr_in remote = {};
    socklen_t addrLen = sizeof(remote);
    int fd = accept(listenFd, (sockaddr*)&remote, &addrLen);
    if (fd < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        Serial.printf("accept() failed, errno=%d\n", errno);
      }
      return;
    }
    handleAcceptedClient(fd, remote);
  }
}

void broadcastFrameIfDue() {
  if (activeStreamCount() == 0) {
    statsFrames = 0;
    statsBytes = 0;
    lastStatsAt = millis();
    return;
  }

  uint32_t now = millis();
  uint32_t frameIntervalMs = 1000UL / STREAM_TARGET_FPS;
  if ((now - lastFrameAt) < frameIntervalMs) {
    return;
  }
  lastFrameAt = now;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Stream failed: esp_camera_fb_get returned null.");
    return;
  }

  char partHeader[180];
  int headerLen = snprintf(partHeader, sizeof(partHeader),
                           "\r\n--%s\r\n"
                           "Content-Type: image/jpeg\r\n"
                           "Content-Length: %u\r\n\r\n",
                           STREAM_BOUNDARY,
                           (unsigned)fb->len);

  int delivered = 0;
  for (int i = 0; i < MAX_STREAM_CLIENTS; ++i) {
    if (streamFds[i] < 0) {
      continue;
    }

    bool ok = sendAll(streamFds[i], (const uint8_t*)partHeader, headerLen);
    if (ok) {
      ok = sendAll(streamFds[i], fb->buf, fb->len);
    }

    if (ok) {
      delivered++;
    } else {
      removeStreamClient(i, "send failed or socket closed");
    }
  }

  size_t frameLen = fb->len;
  esp_camera_fb_return(fb);

  statsFrames++;
  statsBytes += (uint32_t)(frameLen * delivered);

  uint32_t statsNow = millis();
  if ((statsNow - lastStatsAt) >= 5000) {
    float fps = statsFrames * 1000.0f / (float)(statsNow - lastStatsAt);
    float kbps = statsBytes * 8.0f / (float)(statsNow - lastStatsAt);
    Serial.printf("Stream stats: clients=%d, fps=%.1f, last_jpeg=%u bytes, tx=%.0f kbit/s, heap=%u, psram=%u\n",
                  activeStreamCount(),
                  fps,
                  (unsigned)frameLen,
                  kbps,
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram());
    statsFrames = 0;
    statsBytes = 0;
    lastStatsAt = statsNow;
  }
}

void maintainWiFi() {
  static uint32_t lastReconnectAttempt = 0;
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if ((millis() - lastReconnectAttempt) > 5000) {
    lastReconnectAttempt = millis();
    Serial.println("Wi-Fi disconnected, reconnecting...");
    WiFi.reconnect();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  printChipInfo();

  if (!initCamera()) {
    haltWithMessage("Camera initialization failed.");
  }

  connectWiFi();
  startSocketServer();
}

void loop() {
  maintainWiFi();
  if (WiFi.status() == WL_CONNECTED && listenFd >= 0) {
    acceptNewClients();
    broadcastFrameIfDue();
  }
  delay(1);
}
