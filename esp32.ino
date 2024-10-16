#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <driver/i2s.h>
#include "time.h"

// SD kart yapılandırması
#define SD_CS 5
#define SD_SCK 18
#define SD_MOSI 23
#define SD_MISO 19

// Birinci mikrofon pinleri
#define I2S_WS 25
#define I2S_SD 33
#define I2S_SCK 32
#define I2S_PORT I2S_NUM_0

// İkinci mikrofon pinleri
#define I2S_WS_2 27
#define I2S_SD_2 26
#define I2S_SCK_2 14
#define I2S_PORT_2 I2S_NUM_1

#define SAMPLE_RATE 44100
#define SAMPLE_BITS 16
#define CHANNELS 1

#define RECORD_TIME_SECONDS 7
#define SPEAKER_PIN 4
#define BEEP_FREQUENCY 2000
#define BEEP_DURATION 7000

int16_t buffer[1024];
File file1, file2;
bool beepDone = false;
bool recordingStarted = false;  // Kaydı başlatma bayrağı

const char* apSSID = "ESP32_AP";
const char* apPassword = "123456789";
const char* ntpServer = "pool.ntp.org";
const char* testSoundFile = "/test_sound.wav";
const char* espmic1File = "/espmic1.wav";
const char* espmic2File = "/espmic2.wav";
const char* phonemicFile = "/phonemic.wav";

AsyncWebServer server(80);

// WAV başlık yazma fonksiyonu
void writeWAVHeader(File file, int sampleRate, int bitsPerSample, int channels, int dataSize) {
  file.write((uint8_t*)"RIFF", 4);
  int fileSize = 36 + dataSize;
  file.write((uint8_t*)&fileSize, 4);
  file.write((uint8_t*)"WAVE", 4);
  file.write((uint8_t*)"fmt ", 4);
  int fmtSize = 16;
  file.write((uint8_t*)&fmtSize, 4);
  int16_t format = 1;
  file.write((uint8_t*)&format, 2);
  file.write((uint8_t*)&channels, 2);
  file.write((uint8_t*)&sampleRate, 4);
  int byteRate = sampleRate * channels * bitsPerSample / 8;
  file.write((uint8_t*)&byteRate, 4);
  int16_t blockAlign = channels * bitsPerSample / 8;
  file.write((uint8_t*)&blockAlign, 2);
  file.write((uint8_t*)&bitsPerSample, 2);
  file.write((uint8_t*)"data", 4);
  file.write((uint8_t*)&dataSize, 4);
}

// I2S kurulum fonksiyonu
void i2s_install(i2s_port_t port) {
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = i2s_bits_per_sample_t(SAMPLE_BITS),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false
  };
  i2s_driver_install(port, &i2s_config, 0, NULL);
}

// I2S pinlerini ayarlama fonksiyonu
void i2s_setpin(i2s_port_t port, int sck, int ws, int sd) {
  const i2s_pin_config_t pin_config = {
    .bck_io_num = sck,
    .ws_io_num = ws,
    .data_out_num = -1,
    .data_in_num = sd
  };
  i2s_set_pin(port, &pin_config);
}

// SD kartı başlatma
void initSDCard() {
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card failed to initialize!");
    return;
  }
  Serial.println("SD card initialized.");
}

// Dosya silme fonksiyonu
void deleteFile(fs::FS &fs, const char *path) {
  Serial.printf("The file is being deleted: %s\r\n", path);
  if (fs.remove(path)) {
    Serial.println("- file deleted");
  } else {
    Serial.println("- deletion failed");
  }
}

// Geçerli epoch zamanını getirme fonksiyonu
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to receive time");
    return 0;
  }
  time(&now);
  return now;
}

// Wi-Fi AP'yi başlatma fonksiyonu
void initWiFi() {
  WiFi.softAP(apSSID, apPassword);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

// Mikrofonları kaydetme fonksiyonu
void recordMicrophones() {
  if (!recordingStarted) return; // Kaydın başlaması gerekip gerekmediğini kontrol et

  size_t bytesRead;

  // Birinci mikrofon kaydı
  for (int i = 0; i < RECORD_TIME_SECONDS * (SAMPLE_RATE / 1024); i++) {
    i2s_read(I2S_PORT, (void*)buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);
    file1.write((uint8_t*)buffer, bytesRead);
  }
  Serial.println("The first microphone recording is complete.");

  // Kaydı başlatmadan önce 3 saniye bekleyin
  delay(3000);

  // İkinci mikrofon kaydı
  for (int i = 0; i < RECORD_TIME_SECONDS * (SAMPLE_RATE / 1024); i++) {
    i2s_read(I2S_PORT_2, (void*)buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);
    file2.write((uint8_t*)buffer, bytesRead);
  }
  Serial.println("The second microphone recording is complete.");

  file1.close();
  file2.close();
}

// Bleep ses fonksiyonu
void beepSound() {
  if (!beepDone) {
    tone(SPEAKER_PIN, BEEP_FREQUENCY);
    delay(BEEP_DURATION);
    noTone(SPEAKER_PIN);
    beepDone = true;
  }
}

void setup() {
  Serial.begin(115200);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  initWiFi();
  initSDCard();
  configTime(0, 0, ntpServer);

  // Her iki mikrofon için dosyaları aç
  file1 = SD.open("/espmic1.wav", FILE_WRITE);
  if (!file1) {
    Serial.println("Failed to create file: espmic1.wav");
    return;
  }

  file2 = SD.open("/espmic2.wav", FILE_WRITE);
  if (!file2) {
    Serial.println("Failed to create file: espmic2.wav");
    return;
  }

  // Birinci mikrofonu başlat (I2S_NUM_0)
  i2s_install(I2S_PORT);
  i2s_setpin(I2S_PORT, I2S_SCK, I2S_WS, I2S_SD);
  i2s_start(I2S_PORT);

  // İkinci mikrofonu başlat (I2S_NUM_1)
  i2s_install(I2S_PORT_2);
  i2s_setpin(I2S_PORT_2, I2S_SCK_2, I2S_WS_2, I2S_SD_2);
  i2s_start(I2S_PORT_2);

  // WAV başlıklarını yaz
  writeWAVHeader(file1, SAMPLE_RATE, SAMPLE_BITS, CHANNELS, RECORD_TIME_SECONDS * SAMPLE_RATE * CHANNELS * (SAMPLE_BITS / 8));
  writeWAVHeader(file2, SAMPLE_RATE, SAMPLE_BITS, CHANNELS, RECORD_TIME_SECONDS * SAMPLE_RATE * CHANNELS * (SAMPLE_BITS / 8));
  
  Serial.println("Installation complete. Waiting for the registration command...");

  // Sunucu rotalarını yapılandır
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/index.html", "text/html");
  });

  server.on("/start_recording", HTTP_GET, [](AsyncWebServerRequest *request) {
    recordingStarted = true;
    request->send(200, "text/plain", "Kayıt başladı");
  });

  server.on("/download_test_sound", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, testSoundFile, "audio/wav");
  });

  server.on("/download_espmic1", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, espmic1File, "audio/wav");
  });

  server.on("/download_espmic2", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, espmic2File, "audio/wav");
  });

  server.on("/download_phonemic", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, phonemicFile, "audio/wav");
  });

  server.on("/visualize_test_sound", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/visualize_test_sound.html", "text/html");
  });

  server.on("/visualize_espmic1", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/visualize_espmic1.html", "text/html");
  });

  server.on("/visualize_espmic2", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/visualize_espmic2.html", "text/html");
  });

  server.on("/visualize_phonemic", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/visualize_phonemic.html", "text/html");
  });

  server.on("/waveform_test_sound", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/waveform_test_sound.html", "text/html");
  });

  server.on("/waveform_espmic1", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/waveform_espmic1.html", "text/html");
  });

  server.on("/waveform_espmic2", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/waveform_espmic2.html", "text/html");
  });

  server.on("/waveform_phonemic", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/waveform_phonemic.html", "text/html");
  });

  server.on("/delete_test_sound", HTTP_GET, [](AsyncWebServerRequest *request) {
    deleteFile(SD, testSoundFile);
    request->send(200, "text/plain", "Test sound file deleted");
  });

  server.on("/delete_espmic1", HTTP_GET, [](AsyncWebServerRequest *request) {
    deleteFile(SD, espmic1File);
    request->send(200, "text/plain", "ESP Mic 1 file deleted");
  });

  server.on("/delete_espmic2", HTTP_GET, [](AsyncWebServerRequest *request) {
    deleteFile(SD, espmic2File);
    request->send(200, "text/plain", "ESP Mic 2 file deleted");
  });

  server.on("/delete_phonemic", HTTP_GET, [](AsyncWebServerRequest *request) {
    deleteFile(SD, phonemicFile);
    request->send(200, "text/plain", "Phonemic file deleted");
  });

  server.begin();
}

void loop() {
  if (recordingStarted) {
    recordMicrophones();
    beepSound();
    recordingStarted = false;  
    }
}