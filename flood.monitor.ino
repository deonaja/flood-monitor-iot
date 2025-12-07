#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ESP32Servo.h>
#include <flood.monitor.h>

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// -------------------------------
// Sensor Ultrasonik
// -------------------------------
const int TRIG_PIN = 25;
const int ECHO_PIN = 26;

long duration;
float distance_cm   = 0.0;   // ketinggian air (raw, untuk display)
float filteredWater = 0.0;   // ketinggian air (filtered, untuk logika)
bool  firstFilter   = true;

// Tinggi maksimal wadah air (cm)
const float TINGGI_AIR = 12.53;

// -------------------------------
// Sensor Hujan
// -------------------------------
const int RAIN_ANALOG  = 34;  // A0 modul rain sensor
const int RAIN_DIGITAL = 27;  // D0 modul rain sensor (ubah jika pakai pin lain)

// true  -> pakai pembacaan analog (persentase)
// false -> pakai pembacaan digital (sekadar HUJAN / TIDAK)
bool USE_ANALOG_RAIN = true;  // GANTI ke false jika mau pakai digital

// -------------------------------
// Servo
// -------------------------------
Servo servoMotor;
int SERVO_PIN      = 14;
int lastServoAngle = 0;       // track posisi servo terakhir

// -------------------------------
// KONFIG BANJIR & HUJAN
// -------------------------------
// Threshold naik & turun (hysteresis)
const float BANJIR_ON_LEVEL  = 8.0;   // cm (mulai dianggap banjir)
const float BANJIR_OFF_LEVEL = 7.0;   // cm (baru dianggap aman)
// Threshold persen hujan
const float HUJAN_THRESHOLD  = 50.0;  // persen

// Status banjir untuk logika
bool banjirState = false;

// Mode draining (buka pintu air beberapa detik dulu)
bool draining           = false;
unsigned long drainStart = 0;
const unsigned long DRAIN_TIME = 7000;   // 7 detik (bisa diubah 5000–10000)

// Timer notifikasi
unsigned long lastNotify = 0;
const unsigned long INTERVAL = 10000;    // 10 detik (buat testing)

// -------------------------------
// FUNGSI GERAK SERVO PELAN
// -------------------------------
void moveServoSmooth(int startAngle, int endAngle, int stepDelay) {
  if (startAngle < endAngle) {
    for (int pos = startAngle; pos <= endAngle; pos++) {
      servoMotor.write(pos);
      delay(stepDelay);
    }
  } else {
    for (int pos = startAngle; pos >= endAngle; pos--) {
      servoMotor.write(pos);
      delay(stepDelay);
    }
  }
}

// -------------------------------
// FILTER KETINGGIAN AIR (SMOOTHING)
// -------------------------------
void updateFilteredWater(float raw) {
  const float ALPHA = 0.2; // 0.1–0.3: makin kecil makin halus
  if (firstFilter) {
    filteredWater = raw;
    firstFilter   = false;
  } else {
    filteredWater = ALPHA * raw + (1.0 - ALPHA) * filteredWater;
  }
}

// -------------------------------
// PASTIKAN WIFI TERSAMBUNG
// -------------------------------
void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("WiFi terputus, mencoba reconnect...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi tersambung kembali.");
  } else {
    Serial.println("\nGagal reconnect WiFi.");
  }
}

// -------------------------------
// NOTIF BANJIR
// -------------------------------
void notifWarning(float jarak, float persenHujan) {
  String pesan =
    "⚠ WARNING BANJIR\n"
    "-----------------------------\n"
    "📏 Ketinggian Air: " + String(jarak) + " cm\n"
    "🌧 Hujan: YA\n"
    "💧 Persentase Curah Hujan: " + String(persenHujan) + "%\n\n"
    "🚨 Air dan hujan terdeteksi pada level berbahaya! \n"
    "Segera lakukan tindakan pencegahan!";

  bot.sendMessage(CHAT_ID, pesan, "Markdown");
}

// -------------------------------
// NOTIF STATUS
// -------------------------------
void notifStatus(float jarak, float persenHujan, bool hujan) {
  String pesan =
    "📡 Status Monitoring\n"
    "----------------------\n"
    "📏 Ketinggian Air: " + String(jarak) + " cm\n"
    "🌧 Hujan: " + String(hujan ? "YA" : "TIDAK") + "\n"
    "💧 Persentase Curah Hujan: " + String(persenHujan) + "%";

  bot.sendMessage(CHAT_ID, pesan, "Markdown");
}

// -------------------------------
// BACA SENSOR HUJAN (ANALOG/DIGITAL)
// -------------------------------
void readRainSensor(float &rainPercent, bool &hujan) {
  if (USE_ANALOG_RAIN) {
    // MODE ANALOG: baca ADC dan konversi ke persen
    int analogValue = analogRead(RAIN_ANALOG);

    // 4095 = kering, 0 = basah → konversi ke % hujan
    rainPercent = (4095.0 - analogValue) * 100.0 / 4095.0;
    if (rainPercent < 0)   rainPercent = 0;
    if (rainPercent > 100) rainPercent = 100;

    hujan = (rainPercent >= HUJAN_THRESHOLD);

  } else {
    // MODE DIGITAL: baca D0 modul rain sensor
    int digitalValue = digitalRead(RAIN_DIGITAL);

    // Umumnya: LOW = basah, HIGH = kering
    // Kalau di modulmu kebalik, ganti jadi (digitalValue == HIGH)
    hujan = (digitalValue == LOW);

    // Di mode digital, pakai 0% atau 100% saja
    rainPercent = hujan ? 100.0 : 0.0;
  }
}

// -------------------------------
// SETUP
// -------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(RAIN_DIGITAL, INPUT);   // kalau perlu: INPUT_PULLUP

  servoMotor.attach(SERVO_PIN);
  lastServoAngle = 0;
  servoMotor.write(lastServoAngle);

  Serial.println("Menghubungkan ke WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Terhubung!");
  client.setInsecure();
}

// -------------------------------
// LOOP UTAMA
// -------------------------------
void loop() {

  // --------------------------
  // BACA SENSOR ULTRASONIK
  // --------------------------
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // timeout 30 ms untuk menghindari hang
  duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    // gagal baca, jangan update distance_cm
    Serial.println("Gagal baca ultrasonik (timeout), pakai nilai sebelumnya.");
  } else {
    float jarak_sensor_ke_air = duration * 0.034 / 2.0;
    float ketinggian = TINGGI_AIR - jarak_sensor_ke_air;

    // clamp 0–TINGGI_AIR
    if (ketinggian < 0)           ketinggian = 0;
    if (ketinggian > TINGGI_AIR)  ketinggian = TINGGI_AIR;

    distance_cm = ketinggian;
  }

  // update filter untuk logika banjir
  updateFilteredWater(distance_cm);
  float levelForLogic = filteredWater;

  // --------------------------
  // BACA SENSOR HUJAN (ANALOG/DIGITAL via SWITCH)
// --------------------------
  float rainPercent = 0.0;
  bool hujan        = false;

  readRainSensor(rainPercent, hujan);

  // --------------------------
  // UPDATE STATUS BANJIR (HYSTERESIS)
// --------------------------
  if (!banjirState) {
    // sebelumnya tidak banjir → cek apakah masuk banjir
    if (levelForLogic >= BANJIR_ON_LEVEL && hujan) {
      banjirState = true;
    }
  } else {
    // sebelumnya banjir → cek apakah sudah aman
    if (levelForLogic <= BANJIR_OFF_LEVEL || !hujan) {
      banjirState = false;
    }
  }

  // --------------------------
  // LOG KONDISI KE SERIAL
  // --------------------------
  Serial.print("Ketinggian Air (raw): ");
  Serial.print(distance_cm);
  Serial.print(" cm | Filtered: ");
  Serial.print(levelForLogic);
  Serial.print(" cm | Hujan: ");
  Serial.print(hujan ? "YA" : "TIDAK");
  Serial.print(" | Persen Hujan: ");
  Serial.println(rainPercent);

  // --------------------------
  // LOGIKA SERVO + MODE DRAINING
  // --------------------------
  unsigned long now = millis();

  if (!draining) {
    // Tidak sedang draining
    if (banjirState) {
      // Mulai siklus buang air
      Serial.println("BANJIR terdeteksi → mulai DRAINING, buka servo.");
      draining   = true;
      drainStart = now;

      if (lastServoAngle != 180) {
        moveServoSmooth(lastServoAngle, 180, 8);
        lastServoAngle = 180;
      }
    } else {
      // Tidak banjir, pastikan servo tertutup
      if (lastServoAngle != 0) {
        Serial.println("Kondisi normal → tutup servo.");
        moveServoSmooth(lastServoAngle, 0, 8);
        lastServoAngle = 0;
      }
    }
  } else {
    // Sedang dalam mode draining
    if (now - drainStart >= DRAIN_TIME) {
      // Waktu buang air selesai, cek ulang status banjir
      Serial.println("Selesai durasi DRAINING → cek ulang status banjir.");

      if (banjirState) {
        // Masih banjir → ulangi siklus drain (servo tetap buka)
        Serial.println("Masih BANJIR → ulangi DRAINING.");
        drainStart = now;  // restart timer
        // lastServoAngle sudah 180, tidak perlu digerakkan lagi
      } else {
        // Sudah tidak banjir → tutup servo dan keluar dari draining
        Serial.println("BANJIR reda → tutup servo dan akhiri DRAINING.");
        if (lastServoAngle != 0) {
          moveServoSmooth(lastServoAngle, 0, 8);
          lastServoAngle = 0;
        }
        draining = false;
      }
    } else {
      // Masih dalam waktu buang air → biarkan servo tetap terbuka
    }
  }

  // --------------------------
  // PENGIRIMAN STATUS / WARNING
  // --------------------------
  ensureWiFiConnected();

  static bool lastBanjirNotifState = false;
  bool banjirNotif = banjirState;  // bisa diganti (banjirState || draining) kalau mau warning selama draining

  if (now - lastNotify >= INTERVAL) {
    if (banjirNotif) {
      // Baru masuk banjir?
      if (!lastBanjirNotifState) {
        Serial.println("Kirim WARNING BANJIR (transisi normal → banjir).");
        notifWarning(distance_cm, rainPercent);
      } else {
        Serial.println("Kirim WARNING BANJIR (reminder).");
        notifWarning(distance_cm, rainPercent);
      }
    } else {
      Serial.println("Kirim status normal.");
      notifStatus(distance_cm, rainPercent, hujan);
    }

    lastNotify = now;
    lastBanjirNotifState = banjirNotif;
  }

  delay(300);
}
