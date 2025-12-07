# Flood Monitor IoT (ESP32)

Sistem monitoring banjir berbasis **ESP32** yang menggunakan:

- Sensor **ultrasonik** untuk membaca ketinggian air
- Sensor **hujan** (raindrop sensor) yang bisa dibaca via **analog** maupun **digital**
- **Servo** sebagai pintu air otomatis (buka/tutup)
- Notifikasi **Telegram Bot** untuk mengirim status dan peringatan banjir

Project ini dirancang untuk:
- Mengurangi spam gerakan servo dengan logika **draining** dan **hysteresis**
- Mengirim notifikasi berkala dan peringatan ketika kondisi banjir terdeteksi

---

## ✨ Fitur Utama

- 📏 **Deteksi ketinggian air** dengan sensor ultrasonik
- 🌧 **Deteksi hujan** via:
  - Mode **analog** → persentase intensitas hujan (0–100%)
  - Mode **digital** → hanya HUJAN / TIDAK HUJAN
- 🚧 **Logika banjir** dengan:
  - Hysteresis (batas naik & turun berbeda) → mengurangi flip-flop status
  - Smoothing (filter) pada bacaan ketinggian air
- 🚰 **Mode draining**:
  - Saat banjir terdeteksi, servo membuka pintu air selama X detik
  - Setelah itu baru cek ulang apakah masih banjir atau tidak
- 🤖 **Notifikasi Telegram**:
  - Kirim **WARNING BANJIR** saat kondisi banjir terdeteksi
  - Kirim **status monitoring** berkala (ketinggian air & kondisi hujan)
- 🔒 **Keamanan credential**:
  - WiFi SSID, password, dan Telegram BOT_TOKEN disimpan di file `flood.monitor.h` yang **tidak di-push** ke GitHub
  - Disediakan `flood.monitor.h.example` sebagai template aman

---
