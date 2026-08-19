# Smart RFID Dormitory Living System | RFID 智慧宿舍生活系統

![ESP32](https://img.shields.io/badge/ESP32-Arduino_C++-E7352C?style=flat&logo=espressif&logoColor=white)
![Node.js](https://img.shields.io/badge/Node.js-Express-339933?style=flat&logo=node.js&logoColor=white)
![MQTT](https://img.shields.io/badge/MQTT-HiveMQ-660066?style=flat&logo=mqtt&logoColor=white)
![MongoDB](https://img.shields.io/badge/MongoDB-Atlas-47A248?style=flat&logo=mongodb&logoColor=white)

An Internet of Things (IoT) system for student dormitory management. It combines RFID access control, cloud verification, parent notifications, and local lighting controls.

本專案是一個智慧宿舍物聯網系統，結合 RFID 門禁、雲端權限驗證、家長通知，以及本地端燈光控制。

---

## System Architecture | 系統架構

```
ESP32 (RFID reader, RGB LEDs, servo lock)
   │  MQTT (HiveMQ)
   ▼
Node.js / Express backend  ──▶  MongoDB Atlas (card whitelist, activity log)
   │                        └─▶  Nodemailer (Gmail SMTP → parent notification)
   ▼  Server-Sent Events (SSE)
Web dashboard (virtual door twin, lighting control, voice commands)
```

---

## 🌟 Core Features | 核心功能

### 1. Secure RFID Access Control & Real-Time Parent Notifications
### 核心功能一：安全 RFID 門禁控制與即時家長通知

*   **English:**
    An automated entry and safety assurance system. When a student scans their RFID student ID card at the door sensor:
    *   **Live Cloud Verification:** The ESP32 micro-controller reads the card UID and publishes it over MQTT. The Node.js server verifies the card in real-time against a whitelist in MongoDB Atlas.
    *   **Access Actuation:** Authorized cards unlock the door, while unrecognized cards trigger visual intruder alarms and logging.
    *   **Automated Communication:** Upon entrance, the backend automatically dispatches a reassurance email to parents via **Nodemailer (Gmail SMTP)** containing the student's name, room number, and exact local check-in timestamp.
*   **臺灣中文：**
    自動化出入與平安回宿通知系統。當學生於門口感應器刷 RFID 學生證時：
    *   **雲端即時驗證：** ESP32 微控制器讀取卡片 UID 並透過 MQTT 協定發送，由 Node.js 後端伺服器即時比對 MongoDB Atlas 資料庫白名單。
    *   **門禁控制：** 授權卡片通過時門禁解鎖，未授權卡片則會觸發入侵警示並記錄於系統中。
    *   **自動化親友通知：** 刷卡成功後，後端會自動透過 **Nodemailer (Gmail SMTP)** 發送平安信件至家長信箱，包含學生姓名、宿舍房號及精確的抵達時間。

---

### 2. Smart Ambient Lighting & Voice Controller with Digital Twin
### 核心功能二：智慧環境燈光、網頁原生語音控制與數位雙生門禁模擬

*   **English:**
    An interactive room controller with native voice activation and software simulation fail-safes:
    *   **Multimodal Smart Lighting:** Supports visual app controls (power, brightness capped at 85% for hardware current protection) and various dynamic preset patterns (Rainbow color cycles, breathing Aurora effects, and warm amber Bedtime light) on WS2812B RGB LEDs.
    *   **HTML5 Native Voice Control:** Built-in Speech Recognition allows users to toggle states hands-free by speaking commands (e.g., *"i'm home"*, *"party mode"*, *"goodnight"*) directly to their browser using local, client-side English decoding.
    *   **Digital Twin Simulation (Virtual Door):** Features a dynamic 3D virtual door interface synced via **Server-Sent Events (SSE)**. If the mechanical servo latch suffers a physical failure, the system automatically runs Software-in-the-Loop (SIL) simulation, verifying the entire data telemetry loop.
*   **臺灣中文：**
    具備網頁原生語音控制與軟體模擬容錯機制的互動式房務控制中心：
    *   **多模態智慧環境燈光：** 支援網頁端與 App 控制 WS2812B RGB 燈條（支援開關、亮度調整，硬體端亮度上限設為 85 以達電流防護），並內建多種情境（全彩彩虹漸變、呼吸極光特效、暖琥珀睡眠燈光）。
    *   **HTML5 原生語音控制：** 整合網頁端 Speech Recognition API，支援免動手語音控制。用戶可直接對瀏覽器說出英文指令（例如：*"i'm home"*, *"party mode"*, *"goodnight"*）進行本地端語音即時解碼與控制。
    *   **數位雙生虛擬門禁 (Digital Twin)：** 設計了 3D 虛擬門禁網頁，透過 **Server-Sent Events (SSE)** 即時與伺服器連線。即使實體伺服馬達發生硬體故障，亦能透過軟體模擬（Software-in-the-Loop）確保整條數據流（RFID -> ESP32 -> MQTT -> Server -> SSE -> 網頁 UI）通暢無阻。

---

## 🛠️ Technology Stack | 技術棧
*   **Controller:** ESP32 (Arduino C++)
*   **Communications:** MQTT (HiveMQ), SSE (Server-Sent Events), HTTP REST
*   **Backend:** Node.js, Express, Nodemailer
*   **Database:** MongoDB Atlas (Mongoose)
*   **Frontend:** HTML5 (Native Web Speech API), Tailwind CSS

---

## Repository Layout | 專案結構

```
dorm_iot_system/
├── ESP32_code/IoT_Final_Project/   # Arduino firmware: RFID reader, RGB LED control, servo lock
└── dorm-iot-backend/               # Express server: MQTT subscriber, MongoDB, SSE, email dispatch
    ├── server.js                   # Entry point & SSE/MQTT wiring
    ├── student.js / activity.js    # Card whitelist & access-log models
    └── public/                     # Virtual door twin + lighting control dashboard
```

## Running It | 執行方式

```bash
cd dorm-iot-backend
npm install
# configure MONGO_URI, MQTT broker credentials, and Gmail SMTP creds in .env
npm start
```

Flash `ESP32_code/IoT_Final_Project/IoT_Final_Project.ino` to the ESP32 via the Arduino IDE, pointing it at the same MQTT broker.
