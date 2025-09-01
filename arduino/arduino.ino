#include <Wire.h>
#include <SoftwareSerial.h>

// ------------ Pins / Config ------------
#define SIM_RX_PIN      A0
#define SIM_TX_PIN      A1
#define PIN_HALL        2

// 24C256 with A2=A1=A0=GND
#define EEPROM_I2C_ADDR 0x50
#define EEPROM_TOTAL_BYTES  32768UL
#define EEPROM_PAGE_SIZE    64

// Wear-leveling record format (8 bytes, aligned)
#define RECORD_SIZE         8
#define RECORDS_COUNT       (EEPROM_TOTAL_BYTES / RECORD_SIZE)
#define COMMIT_HI           0xA5
#define COMMIT_LO           0x5A

// Flow & SMS config
const bool   DEBUG                = true;
const String TRUSTED_NUMBER       = "+981234567890";
const unsigned int PULSES_PER_LITER = 670;
const unsigned int LITERS_PER_SAVE  = 5;

// ------------ Globals ------------
SoftwareSerial sim800(SIM_RX_PIN, SIM_TX_PIN);

// Flow counters (shared with ISR)
volatile unsigned int pulseCounter = 0;
volatile unsigned int literCounter = 0;
unsigned int lastSavedLiters = 0;

// EEPROM wear-leveling state
uint32_t lastSequence = 0;
uint32_t eepromWriteIndex = 0;

// SIM buffer & SMS queues
String simBuffer;

// Queue for incoming SMS indexes from +CMTI
#define INBOX_Q_SZ 10
int inboxQ[INBOX_Q_SZ];
uint8_t inboxHead = 0, inboxTail = 0;
bool readingSms = false;
int currentReadIndex = -1;

// Outgoing SMS queue (non-blocking)
#define SMS_Q_SZ 5
struct SmsJob {
  bool used = false;
  String to;
  String txt;
  enum State { IDLE=0, SENT_CMGF, SENT_CMGS, WAIT_PROMPT, SENT_BODY, WAIT_CMGS, DONE, ERROR } state = IDLE;
  unsigned long ts = 0;
};
SmsJob smsQ[SMS_Q_SZ];
int smsCurrent = -1;
bool smsBusy = false;

// Initial liters behavior (first trusted SMS sets initial liters)
bool waitingForInitialLiters = true;

// ------------ ISR ------------
void countPulse() {
  pulseCounter++;
  if (pulseCounter >= PULSES_PER_LITER) {
    pulseCounter = 0;
    // Multi-byte increment; main reads/writes with interrupts off when needed
    literCounter++;
  }
}

// ------------ EEPROM helpers (24C256) ------------
void eeprom_wait_ready() {
  // ACK polling (fast & robust)
  while (true) {
    Wire.beginTransmission(EEPROM_I2C_ADDR);
    uint8_t e = Wire.endTransmission();
    if (e == 0) break;
    delayMicroseconds(500);
  }
}

void eeprom_write_pagewise(uint16_t addr, const uint8_t* data, uint16_t len) {
  // Split across 64-byte page boundaries
  uint16_t remaining = len;
  uint16_t offset = 0;

  while (remaining > 0) {
    uint16_t pageSpace = EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE);
    uint16_t chunk = (remaining < pageSpace) ? remaining : pageSpace;

    Wire.beginTransmission(EEPROM_I2C_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    for (uint16_t i = 0; i < chunk; i++) Wire.write(data[offset + i]);
    Wire.endTransmission();
    eeprom_wait_ready(); // write cycle complete

    addr += chunk;
    offset += chunk;
    remaining -= chunk;
  }
}

void eeprom_read_burst(uint16_t addr, uint8_t* out, uint16_t len) {
  // Wire buffer on AVR ~32 bytes; read in chunks
  while (len > 0) {
    uint8_t chunk = (len > 30) ? 30 : len;
    Wire.beginTransmission(EEPROM_I2C_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    Wire.endTransmission();
    Wire.requestFrom(EEPROM_I2C_ADDR, chunk);
    uint8_t i = 0;
    while (Wire.available() && i < chunk) {
      out[i++] = Wire.read();
    }
    addr += chunk;
    out += chunk;
    len -= chunk;
  }
}

inline uint16_t recAddr(uint32_t recIndex) {
  recIndex %= RECORDS_COUNT;
  return (uint16_t)(recIndex * RECORD_SIZE);
}

// Write record in two steps: data then commit (atomicity)
void eeprom_write_record(uint32_t recIndex, uint32_t seq, uint16_t liters) {
  uint8_t rec[RECORD_SIZE];
  // Layout: [SEQ(0..3), LIT(4..5), COMMIT(6..7)]
  rec[0] = (uint8_t)(seq >> 24);
  rec[1] = (uint8_t)(seq >> 16);
  rec[2] = (uint8_t)(seq >> 8);
  rec[3] = (uint8_t)(seq);
  rec[4] = (uint8_t)(liters >> 8);
  rec[5] = (uint8_t)(liters);
  rec[6] = 0xFF; // not committed yet
  rec[7] = 0xFF;

  uint16_t addr = recAddr(recIndex);

  // Step 1: write bytes 0..5
  eeprom_write_pagewise(addr, rec, 6);

  // Step 2: write commit marker bytes 6..7
  rec[6] = COMMIT_HI;
  rec[7] = COMMIT_LO;
  eeprom_write_pagewise(addr + 6, rec + 6, 2);
}

bool eeprom_read_record(uint32_t recIndex, uint32_t &seq, uint16_t &liters, bool &valid) {
  uint8_t rec[RECORD_SIZE];
  eeprom_read_burst(recAddr(recIndex), rec, RECORD_SIZE);
  valid = (rec[6] == COMMIT_HI && rec[7] == COMMIT_LO);
  if (!valid) { seq = 0; liters = 0; return false; }
  seq = ((uint32_t)rec[0] << 24) | ((uint32_t)rec[1] << 16) | ((uint32_t)rec[2] << 8) | rec[3];
  liters = ((uint16_t)rec[4] << 8) | rec[5];
  return true;
}

// Scan all records; pick highest seq with valid commit
uint16_t loadLitersFromEEPROM() {
  uint32_t bestSeq = 0;
  uint16_t bestLit = 0;
  uint32_t bestIdx = 0;
  bool any = false;

  uint8_t rec[RECORD_SIZE];

  for (uint32_t i = 0; i < RECORDS_COUNT; i++) {
    eeprom_read_burst(recAddr(i), rec, RECORD_SIZE);
    if (rec[6] == COMMIT_HI && rec[7] == COMMIT_LO) {
      uint32_t seq = ((uint32_t)rec[0] << 24) | ((uint32_t)rec[1] << 16) | ((uint32_t)rec[2] << 8) | rec[3];
      uint16_t lit = ((uint16_t)rec[4] << 8) | rec[5];
      if (!any || seq > bestSeq) {
        any = true;
        bestSeq = seq;
        bestLit = lit;
        bestIdx = i;
      }
    }
  }

  if (!any) {
    // Fresh chip
    eepromWriteIndex = 0;
    lastSequence = 0;
    if (DEBUG) Serial.println(F("EEPROM empty; start at 0 liters"));
    return 0;
  }

  // Next write goes to the next record
  eepromWriteIndex = bestIdx + 1;
  if (eepromWriteIndex >= RECORDS_COUNT) eepromWriteIndex = 0;
  lastSequence = bestSeq;

  if (DEBUG) {
    Serial.print(F("EEPROM loaded liters=")); Serial.print(bestLit);
    Serial.print(F(" seq=")); Serial.print(bestSeq);
    Serial.print(F(" lastIdx=")); Serial.println(bestIdx);
  }
  return bestLit;
}

void saveLitersToEEPROM(uint16_t liters) {
  lastSequence++;
  if (lastSequence == 0) lastSequence = 1; // avoid 0, though it's fine
  eeprom_write_record(eepromWriteIndex, lastSequence, liters);
  if (DEBUG) {
    Serial.print(F("Saved liters=")); Serial.print(liters);
    Serial.print(F(" seq=")); Serial.print(lastSequence);
    Serial.print(F(" idx=")); Serial.println(eepromWriteIndex);
  }
  eepromWriteIndex++;
  if (eepromWriteIndex >= RECORDS_COUNT) eepromWriteIndex = 0;
}

// ------------ SMS helpers ------------
void enqueueSms(const String &to, const String &txt) {
  for (int i = 0; i < SMS_Q_SZ; i++) {
    if (!smsQ[i].used) {
      smsQ[i].used = true;
      smsQ[i].to = to;
      smsQ[i].txt = txt;
      smsQ[i].state = SmsJob::IDLE;
      if (DEBUG) { Serial.print(F("Enqueue SMS -> ")); Serial.print(to); Serial.print(F(" : ")); Serial.println(txt); }
      return;
    }
  }
  if (DEBUG) Serial.println(F("SMS queue full; dropping message"));
}

void serviceSmsOut() {
  // Progress current job if busy
  if (smsBusy) {
    SmsJob &job = smsQ[smsCurrent];
    switch (job.state) {
      case SmsJob::SENT_CMGF:
        // Next: AT+CMGS="<to>"
        sim800.print(F("AT+CMGS=\"")); sim800.print(job.to); sim800.println(F("\""));
        job.state = SmsJob::SENT_CMGS;
        job.ts = millis();
        break;

      case SmsJob::WAIT_PROMPT:
        // Prompt '>' is handled in sim buffer processor; here we only timeout-safety
        if (millis() - job.ts > 15000) {
          job.state = SmsJob::ERROR;
          if (DEBUG) Serial.println(F("Timeout waiting for '>'"));
        }
        break;

      case SmsJob::SENT_BODY:
        // Waiting for +CMGS in buffer; timeout safety
        if (millis() - job.ts > 40000) {
          job.state = SmsJob::ERROR;
          if (DEBUG) Serial.println(F("Timeout waiting for +CMGS"));
        }
        break;

      case SmsJob::DONE:
      case SmsJob::ERROR:
        if (DEBUG) Serial.println(job.state == SmsJob::DONE ? F("SMS send OK") : F("SMS send ERROR"));
        job.used = false; job.to = ""; job.txt = ""; job.state = SmsJob::IDLE;
        smsBusy = false; smsCurrent = -1;
        break;

      default:
        break;
    }
    return;
  }

  // Not busy: pick next queued job
  for (int i = 0; i < SMS_Q_SZ; i++) {
    if (smsQ[i].used && smsQ[i].state == SmsJob::IDLE) {
      smsCurrent = i;
      smsBusy = true;
      // Start: AT+CMGF=1
      sim800.println(F("AT+CMGF=1"));
      smsQ[i].state = SmsJob::SENT_CMGF;
      smsQ[i].ts = millis();
      if (DEBUG) { Serial.print(F("Start SMS -> ")); Serial.println(smsQ[i].to); }
      break;
    }
  }
}

// ------------ Inbox queue helpers ------------
bool inbox_push(int idx) {
  uint8_t nextTail = (inboxTail + 1) % INBOX_Q_SZ;
  if (nextTail == inboxHead) return false; // full
  inboxQ[inboxTail] = idx;
  inboxTail = nextTail;
  return true;
}
bool inbox_pop(int &idx) {
  if (inboxHead == inboxTail) return false;
  idx = inboxQ[inboxHead];
  inboxHead = (inboxHead + 1) % INBOX_Q_SZ;
  return true;
}

// ------------ SIM buffer processing ------------
void requestReadSms(int index) {
  currentReadIndex = index;
  readingSms = true;
  sim800.print(F("AT+CMGR=")); sim800.println(index);
}

void deleteSmsIndex(int index) {
  sim800.print(F("AT+CMGD=")); sim800.println(index);
}

void processReadSmsResponse(const String &resp) {
  // Parse header line
  // Example:
  // +CMGR: "REC UNREAD","+1234567890","","25/08/18,03:05:00+00"
  // body...
  // OK
  int headerStart = resp.indexOf(F("+CMGR:"));
  if (headerStart < 0) { readingSms = false; return; }
  int headerEnd = resp.indexOf('\n', headerStart);
  if (headerEnd < 0) { readingSms = false; return; }
  String header = resp.substring(headerStart, headerEnd);
  String body = resp.substring(headerEnd + 1);
  // Trim up to OK
  int okPos = body.indexOf(F("\r\nOK"));
  if (okPos >= 0) body = body.substring(0, okPos);
  body.trim();

  // Extract sender (second quoted field)
  int q1 = header.indexOf('\"');
  int q2 = header.indexOf('\"', q1 + 1);
  int q3 = header.indexOf('\"', q2 + 1);
  int q4 = header.indexOf('\"', q3 + 1);
  String from = "";
  if (q3 >= 0 && q4 > q3) from = header.substring(q3 + 1, q4);

  if (DEBUG) {
    Serial.print(F("SMS FROM: ")); Serial.println(from);
    Serial.print(F("BODY: ")); Serial.println(body);
  }

  // Process only trusted number
  if (from == TRUSTED_NUMBER) {
    if (waitingForInitialLiters) {
      uint16_t v = (uint16_t) body.toInt();
      noInterrupts(); literCounter = v; interrupts();
      waitingForInitialLiters = false;
      enqueueSms(from, "LITER SET TO " + String(v));
      noInterrupts(); saveLitersToEEPROM(v); lastSavedLiters = v; interrupts();
    } else if (body == "GET") {
      uint16_t cur; noInterrupts(); cur = literCounter; interrupts();
      enqueueSms(from, "LITERS=" + String(cur));
    } else if (body == "RESET") {
      noInterrupts(); literCounter = 0; interrupts();
      enqueueSms(from, "LITERS RESET");
      noInterrupts(); saveLitersToEEPROM(0); lastSavedLiters = 0; interrupts();
    } else if (body.startsWith("POST ")) {
      uint16_t v = (uint16_t) body.substring(5).toInt();
      noInterrupts(); literCounter = v; interrupts();
      enqueueSms(from, "LITERS SET TO " + String(v));
      noInterrupts(); saveLitersToEEPROM(v); lastSavedLiters = v; interrupts();
    } else {
      enqueueSms(from, "INVALID COMMAND");
    }
  } else {
    if (DEBUG) Serial.println(F("Ignoring unauthorized sender"));
  }

  // Delete processed SMS
  if (currentReadIndex >= 0) deleteSmsIndex(currentReadIndex);
  readingSms = false;
  currentReadIndex = -1;
}

void processSimBuffer() {
  // Feed buffer with available chars first
  while (sim800.available()) {
    char c = (char)sim800.read();
    simBuffer += c;
    if (DEBUG) Serial.write(c);
  }

  // Handle +CMTI: "SM",index
  int cmtiPos;
  while ((cmtiPos = simBuffer.indexOf(F("+CMTI:"))) >= 0) {
    int lineEnd = simBuffer.indexOf('\n', cmtiPos);
    if (lineEnd < 0) break; // wait for line completion
    String line = simBuffer.substring(cmtiPos, lineEnd + 1);
    simBuffer.remove(cmtiPos, line.length());
    // Parse last comma
    int comma = line.lastIndexOf(',');
    if (comma > 0) {
      String idxStr = line.substring(comma + 1); idxStr.trim();
      int idx = idxStr.toInt();
      inbox_push(idx);
      if (DEBUG) { Serial.print(F("Queued SMS index: ")); Serial.println(idx); }
    }
  }

  // Handle +CMGR response complete block (from header to OK)
  int cmgrPos = simBuffer.indexOf(F("+CMGR:"));
  if (cmgrPos >= 0) {
    int okPos = simBuffer.indexOf(F("\r\nOK\r\n"), cmgrPos);
    if (okPos >= 0) {
      String block = simBuffer.substring(cmgrPos, okPos + 6);
      simBuffer.remove(cmgrPos, block.length());
      processReadSmsResponse(block);
    }
  }

  // Handle '>' prompt for SMS body
  int promptPos = simBuffer.indexOf('>');
  if (promptPos >= 0 && smsBusy && smsCurrent >= 0) {
    SmsJob &job = smsQ[smsCurrent];
    if (job.state == SmsJob::WAIT_PROMPT) {
      // Send body + Ctrl+Z
      sim800.print(job.txt);
      delay(10);
      sim800.write(26);
      job.state = SmsJob::SENT_BODY;
      job.ts = millis();
    }
    // consume prompt
    simBuffer.remove(promptPos, 1);
  }

  // Handle +CMGS (send success)
  int cmgsPos = simBuffer.indexOf(F("+CMGS:"));
  if (cmgsPos >= 0 && smsBusy && smsCurrent >= 0) {
    // consume that line
    int lineEnd = simBuffer.indexOf('\n', cmgsPos);
    if (lineEnd > cmgsPos) {
      simBuffer.remove(cmgsPos, lineEnd - cmgsPos + 1);
      smsQ[smsCurrent].state = SmsJob::DONE;
    }
  }

  // After CMGF or CMGS command, we may get "OK"
  int okPos = simBuffer.indexOf(F("\r\nOK\r\n"));
  if (okPos >= 0 && smsBusy && smsCurrent >= 0) {
    SmsJob &job = smsQ[smsCurrent];
    // Advance states on OK when appropriate
    if (job.state == SmsJob::SENT_CMGF) {
      job.state = SmsJob::SENT_CMGS; // actually we already sent CMGS; now we expect '>' prompt
      job.state = SmsJob::WAIT_PROMPT;
      job.ts = millis();
    } else if (job.state == SmsJob::SENT_BODY) {
      // Sometimes +CMGS is followed by OK; if we missed +CMGS, treat OK as completion
      job.state = SmsJob::DONE;
    }
    // consume one OK
    simBuffer.remove(okPos, 6);
  }

  // Prevent runaway buffer growth
  if (simBuffer.length() > 8192) {
    simBuffer = simBuffer.substring(simBuffer.length() - 4096);
  }
}

// ------------ Setup / Loop ------------
void setup() {
  Serial.begin(115200);
  Wire.begin();
  sim800.begin(9600);

  pinMode(PIN_HALL, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_HALL), countPulse, FALLING);

  delay(1500);
  sim800.println(F("AT"));
  delay(300);
  sim800.println(F("AT+CMGF=1"));
  delay(300);
  // Store incoming SMS, notify with +CMTI
  sim800.println(F("AT+CNMI=2,1,0,0,0"));
  delay(300);

  // Load last liters
  uint16_t startLit = loadLitersFromEEPROM();
  noInterrupts(); literCounter = startLit; interrupts();
  lastSavedLiters = startLit;

  // Non-blocking startup SMS
  enqueueSms(TRUSTED_NUMBER, "START");
}

void loop() {
  // Read + process SIM input continually
  processSimBuffer();

  // Drive SMS sender state machine
  serviceSmsOut();

  // If not currently reading an SMS and there's one queued, start reading
  if (!readingSms) {
    int idx;
    if (inbox_pop(idx)) {
      requestReadSms(idx);
    }
  }

  // Snapshot current liters
  uint16_t cur;
  noInterrupts(); cur = literCounter; interrupts();

  static uint16_t lastSnap = 0;
  if (cur != lastSnap) {
    lastSnap = cur;
    if (DEBUG) { Serial.print(F("Liters: ")); Serial.println(cur); }
    // Every LITERS_PER_SAVE liters, persist
    if ((cur / LITERS_PER_SAVE) != (lastSavedLiters / LITERS_PER_SAVE)) {
      noInterrupts();
      saveLitersToEEPROM(cur);
      lastSavedLiters = cur;
      interrupts();
    }
  }

  delay(10);
}
