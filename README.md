# 🚰 Water Flow Meter with SMS Alerts & EEPROM Logging

This project measures water flow, counts liters, and provides remote monitoring via SMS commands.  
It uses an **Arduino + flow sensor + GSM module + 24C256 I²C EEPROM** to create a robust, low-power, field-ready solution.

---

## ✨ Features

### Accurate Water Flow Measurement
- Interrupt-driven pulse counter for precise flow tracking.  

### SMS Control & Monitoring
- Send **`GET`** → receive the total liters used.  
- Send **`RESET`** → reset the counter.  

### Persistent Storage (24C256 EEPROM)
- Stores the water usage counter even after power loss.  
- Sequence-numbered entries ensure the latest record is always found.  
- Optimized page writes for endurance.  

### Non-blocking SMS Sending
- No more 5-second freezes when sending SMS.  

### Robust SMS Parsing & Deletion
- Reads only new messages.  
- Deletes them after processing.  

### Data Safety
- Wraps counter updates in `noInterrupts()` for atomic operations.  

---

## 🛠️ Hardware Requirements
- Arduino Uno / Nano / Mega  
- Water Flow Sensor (Hall-effect type, e.g., **YF-S201**)  
- SIM800L / SIM900 GSM module  
- 24C256 I²C EEPROM (**AT24C256N** or equivalent)  
- Jumper wires, breadboard, stable power supply  

---

## ⚡ Wiring

### EEPROM (AT24C256N, I²C)

| Pin (EEPROM) | Arduino |
|--------------|---------|
| VCC          | 5V      |
| GND          | GND     |
| SDA          | A4      |
| SCL          | A5      |
| WP           | GND     |

### Flow Sensor

| Pin   | Arduino |
|-------|---------|
| VCC   | 5V      |
| GND   | GND     |
| Signal| D2 (INT0)|

### GSM Module (SIM800L/SIM900)

| Pin | Arduino              |
|-----|----------------------|
| VCC | 4V–5V (separate PSU recommended) |
| GND | GND                  |
| TX  | D8 (SoftwareSerial RX) |
| RX  | D9 (SoftwareSerial TX) |

---

## 📂 EEPROM Data Layout

Each record = **6 bytes**:
- `sequence` (4 bytes, uint32_t)  
- `liters` (2 bytes, uint16_t)  

With 32 KB total:  
That’s **~5,461 saves** → with one save per liter, you get **years of retention**.

---

## 📲 SMS Commands

- **GET** → returns the current water usage in liters.  
- **RESET** → resets the counter to zero and saves it.    

---

## 🚦 How It Works

1. Flow sensor pulses are counted in an interrupt.  
2. Every time the counter updates, it’s stored in EEPROM with a sequence number.  
3. On startup → only the **latest EEPROM entry** is loaded.  
4. GSM module listens for incoming SMS:  
   - Parses and executes commands.  
   - Sends back results using **non-blocking SMS sending**.  
   - Deletes messages after processing.  

---

## ⚙️ Software Highlights
- **EEPROM_Manager** → handles sequence tracking, page writes, safe loads.  
- **FlowCounter ISR** → atomic counting with `noInterrupts()`.  
- **SMSHandler** → parses messages, deletes after processing.  
- **NonBlockingSMS** → allows loop to run while SMS is sending.  

---

## 🚀 Getting Started

1. Clone this repo:
   ```bash
   git clone https://github.com/yourname/water-flow-sms-meter.git
   cd water-flow-sms-meter
