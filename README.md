# 🛡️ ESP32-Pentest-Firmware  
### *Мультипротокольный инструмент для пентеста на базе ESP32-U (38-pin) написанная на chatgpt*

[![Version](https://img.shields.io/badge/версия-1.0.0-blue.svg)]()
[![Platform](https://img.shields.io/badge/платформа-ESP32--U-green.svg)]()
[![Release](https://img.shields.io/badge/релиз-.bin-orange.svg)]()

Готовая прошивка для модуля **ESP32-U 38-pin** Написанная ChatGPT, превращающая его в портативный мультитул для тестирования беспроводной безопасности. Поддерживает атаки на Wi-Fi, Bluetooth, Sub-GHz, Infrared и NRF24.

> ⚠️ **Дисклеймер:** Прошивка предназначена только для тестирования собственных устройств и образовательных целей. Использование против чужих сетей и устройств незаконно. Автор не несет ответственности за неправомерное использование.

---

## 📦 Возможности

| Модуль | Функционал |
| :--- | :--- |
| **📡 Wi-Fi** | Deauth атака, Beacon Spam (сотни фейковых точек), Сканирование сетей, Мониторинг пакетов, Evil Portal |
| **🔵 Bluetooth** | iOS Spam (AirDrop-рассылка), Android Spam, Windows Spam (Swift Pair) |
| **📻 Sub-GHz** | Чтение/Отправка сигналов, Raw режим, Анализатор спектра, Глушилка (Jammer), Брутфорс кодов |
| **🔦 Infrared** | Чтение/Отправка ИК-команд, Универсальный пульт (TV/Проектор), Глушилка ИК |
| **📶 NRF24** | Анализатор спектра, Глушилка (Jammer) |

---

## 💾 Установка

### 1. Скачайте прошивку
Перейдите в раздел **[Releases](https://github.com/yourname/esp32-pentest-firmware/releases)** и скачайте файлы:
- `firmware.bin` — основная прошивка
- `bootloader.bin` — загрузчик
- `partitions.bin` — таблица разделов

### 2. Прошейте ESP32

**Способ 1 — ESP32 Flash Download Tool (Windows):**
1. Запустите программу
2. Выберите ESP32
3. Укажите пути к трём .bin файлам и адреса:
   - `bootloader.bin` → `0x1000`
   - `partitions.bin` → `0x8000`
   - `firmware.bin` → `0x10000`
4. Выберите порт и нажмите START

**Способ 2 — esptool.py (Windows/Linux/macOS):**

```bash
esptool.py --chip esp32 --port COM3 --baud 921600 write_flash \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin
