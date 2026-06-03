#include <ELECHOUSE_CC1101_SRC_DRV.h>

// =====================================================
// CC1101 Smoke Alarm Discovery Sketch
// ESP32-S3 + SmartRC CC1101 Library
//
// Purpose:
//  - Verify RF reception
//  - Use GDO2 as carrier sense gate
//  - Capture raw GDO0 demodulated data transitions
//  - Dump one RF train after carrier sense ends
//  - Reconstruct approximate bitstream from edge timings
//  - Tally repeated frame codes
//
// Current best estimate:
//  433.92 MHz
//  2FSK
//  9.6 kBaud
//  Base bit period ≈ 104 us
// =====================================================

#include <string.h>

constexpr bool VERBOSE = false;


#define CC1101_IOCFG0 0x02
#define CC1101_IOCFG2 0x00

#define GDO_CARRIER_SENSE 0x0E
#define GDO_SERIAL_DATA 0x0D

constexpr int PIN_SCK = 15;
constexpr int PIN_MISO = 16;
constexpr int PIN_MOSI = 17;
constexpr int PIN_CS = 18;

constexpr int PIN_GDO0 = 7;
constexpr int PIN_GDO2 = 6;

// RF settings
constexpr float RF_FREQ = 433.92;
constexpr float RF_DATARATE = 9.6;
constexpr float RF_DEVIATION = 47.6;
constexpr float RF_RXBW = 203.1;

// Edge capture
constexpr uint16_t MAX_EDGES = 12000;
constexpr uint16_t MIN_EDGES_TO_DUMP = 80;
constexpr uint32_t TRAIN_END_US = 20000;

// Timing decode
constexpr float BIT_US = 104.0;
constexpr uint32_t GLITCH_REJECT_US = 35;
constexpr uint32_t FRAME_GAP_US = 500; //was 500
constexpr uint8_t MAX_RUN_BITS = 32;
constexpr uint16_t MAX_FRAME_BITS = 512;

// Frame catalogue
constexpr uint8_t MAX_UNIQUE_FRAMES = 16;
constexpr uint8_t MAX_FRAME_HEX_LEN = 40;

// Capture window
constexpr uint32_t MAX_CAPTURE_US = 2500000;
volatile uint32_t captureStartUs = 0;

volatile uint32_t edgeTime[MAX_EDGES];
volatile uint8_t edgeLevel[MAX_EDGES];
volatile uint16_t edgeCount = 0;
volatile bool edgeOverflow = false;

volatile bool carrierActive = false;
volatile uint32_t lastCarrierChangeUs = 0;
volatile bool captureHadCarrier = false;

uint32_t lastStatusPrint = 0;
int lastGdo2State = -1;
uint32_t captureNumber = 0;

//************************************* Config Radio ****************************

void configureRadio() {
  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  ELECHOUSE_cc1101.Init();

  // Basic modem
  ELECHOUSE_cc1101.setCCMode(1);
  ELECHOUSE_cc1101.setModulation(0);  // 0 = 2FSK
  ELECHOUSE_cc1101.setMHZ(RF_FREQ);
  ELECHOUSE_cc1101.setChannel(0);
  ELECHOUSE_cc1101.setChsp(199.95);
  ELECHOUSE_cc1101.setDRate(RF_DATARATE);
  ELECHOUSE_cc1101.setDeviation(RF_DEVIATION);
  ELECHOUSE_cc1101.setRxBW(RF_RXBW);

  // Disable protocol assumptions
  ELECHOUSE_cc1101.setSyncMode(0);
  ELECHOUSE_cc1101.setSyncWord(0, 0);
  ELECHOUSE_cc1101.setAdrChk(0);
  ELECHOUSE_cc1101.setAddr(0);
  ELECHOUSE_cc1101.setWhiteData(0);
  ELECHOUSE_cc1101.setCrc(0);
  ELECHOUSE_cc1101.setCRC_AF(0);
  ELECHOUSE_cc1101.setManchester(0);
  ELECHOUSE_cc1101.setFEC(0);
  ELECHOUSE_cc1101.setPRE(0);
  ELECHOUSE_cc1101.setPQT(0);
  ELECHOUSE_cc1101.setDcFilterOff(0);
  ELECHOUSE_cc1101.setAppendStatus(0);

  // Asynchronous serial output from demodulator
  ELECHOUSE_cc1101.setPktFormat(3);

  // Infinite packet mode
  ELECHOUSE_cc1101.setLengthConfig(2);
  ELECHOUSE_cc1101.setPacketLength(255);
  ELECHOUSE_cc1101.setPA(0);

  ELECHOUSE_cc1101.setGDO0(PIN_GDO0);

  // GDO0 = demodulated serial data
  // GDO2 = carrier sense
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_IOCFG0, GDO_SERIAL_DATA);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_IOCFG2, GDO_CARRIER_SENSE);

  ELECHOUSE_cc1101.SetRx();
}

//************************************* functions ****************************

void IRAM_ATTR gdo0ISR() {
  if (!carrierActive) return;

  if (edgeCount < MAX_EDGES) {
    edgeTime[edgeCount] = micros();
    edgeLevel[edgeCount] = digitalRead(PIN_GDO0);
    edgeCount++;
  } else {
    edgeOverflow = true;
  }
}

void IRAM_ATTR gdo2ISR() {
  bool state = digitalRead(PIN_GDO2);
  carrierActive = state;
  lastCarrierChangeUs = micros();

  if (state && !captureHadCarrier) {
    captureHadCarrier = true;
    captureStartUs = micros();
  }
}

void resetCapture() {
  noInterrupts();

  edgeCount = 0;
  edgeOverflow = false;
  captureHadCarrier = false;
  carrierActive = digitalRead(PIN_GDO2);
  lastCarrierChangeUs = micros();
  captureStartUs = micros();

  interrupts();
}

uint8_t durationToBits(uint32_t dt) {
  if (dt < GLITCH_REJECT_US) return 0;

  uint8_t bits = (uint8_t)((dt + (BIT_US / 2.0)) / BIT_US);

  if (bits < 1) bits = 1;
  if (bits > MAX_RUN_BITS) bits = MAX_RUN_BITS;

  return bits;
}

char hexChar(uint8_t value) {
  if (value < 10) return (char)('0' + value);
  return (char)('a' + (value - 10));
}

void bitsToHexString(const char *bits, char *outHex, size_t outSize, bool invert) {
  uint8_t nibble = 0;
  uint8_t nibbleCount = 0;
  size_t outIndex = 0;

  for (uint16_t i = 0; bits[i] != '\0'; i++) {
    char c = bits[i];

    if (c != '0' && c != '1') continue;

    if (invert) {
      c = (c == '1') ? '0' : '1';
    }

    nibble <<= 1;
    if (c == '1') nibble |= 1;
    nibbleCount++;

    if (nibbleCount == 4) {
      if (outIndex < outSize - 1) {
        outHex[outIndex++] = hexChar(nibble);
      }

      nibble = 0;
      nibbleCount = 0;
    }
  }

  if (nibbleCount > 0) {
    nibble <<= (4 - nibbleCount);

    if (outIndex < outSize - 1) {
      outHex[outIndex++] = hexChar(nibble);
    }
  }

  outHex[outIndex] = '\0';
}

void addFrameToCatalogue(
  char catalogue[MAX_UNIQUE_FRAMES][MAX_FRAME_HEX_LEN],
  uint16_t counts[MAX_UNIQUE_FRAMES],
  uint8_t &uniqueCount,
  const char *hexFrame) {
  for (uint8_t i = 0; i < uniqueCount; i++) {
    if (strcmp(catalogue[i], hexFrame) == 0) {
      counts[i]++;
      return;
    }
  }

  if (uniqueCount < MAX_UNIQUE_FRAMES) {
    strncpy(catalogue[uniqueCount], hexFrame, MAX_FRAME_HEX_LEN - 1);
    catalogue[uniqueCount][MAX_FRAME_HEX_LEN - 1] = '\0';
    counts[uniqueCount] = 1;
    uniqueCount++;
  }
}

void printFrameCatalogue(
  char catalogue[MAX_UNIQUE_FRAMES][MAX_FRAME_HEX_LEN],
  uint16_t counts[MAX_UNIQUE_FRAMES],
  uint8_t uniqueCount,
  const char *label) {
  Serial.println();
  Serial.print("Unique frames ");
  Serial.print(label);
  Serial.println(":");

  if (uniqueCount == 0) {
    Serial.println("none");
    return;
  }

  for (uint8_t i = 0; i < uniqueCount; i++) {
    Serial.print(catalogue[i]);
    Serial.print(" x");
    Serial.println(counts[i]);
  }
}

void reconstructBitstream(uint32_t *times, uint8_t *levels, uint16_t count) {
  Serial.println();
  Serial.println("========== RECONSTRUCTED BITSTREAM ==========");
  if (VERBOSE) {
    Serial.println("Method: expand each level run by round(dt / 104 us)");
    Serial.println("Frames split when dt_us > 500 us");
    Serial.println();
  }

  uint16_t frameIndex = 0;
  bool frameOpen = false;

  char frameBits[MAX_FRAME_BITS];
  uint16_t frameBitCount = 0;

  char normalHex[MAX_FRAME_HEX_LEN];
  char invertedHex[MAX_FRAME_HEX_LEN];

  char normalCatalogue[MAX_UNIQUE_FRAMES][MAX_FRAME_HEX_LEN];
  char invertedCatalogue[MAX_UNIQUE_FRAMES][MAX_FRAME_HEX_LEN];
  uint16_t normalCounts[MAX_UNIQUE_FRAMES] = { 0 };
  uint16_t invertedCounts[MAX_UNIQUE_FRAMES] = { 0 };
  uint8_t normalUnique = 0;
  uint8_t invertedUnique = 0;

  for (uint16_t i = 1; i < count; i++) {
    uint32_t dt = times[i] - times[i - 1];

    if (dt < GLITCH_REJECT_US) continue;

    if (dt > FRAME_GAP_US) {
      if (frameOpen && frameBitCount > 0) {
        frameBits[frameBitCount] = '\0';

        bitsToHexString(frameBits, normalHex, sizeof(normalHex), false);
        bitsToHexString(frameBits, invertedHex, sizeof(invertedHex), true);

        addFrameToCatalogue(normalCatalogue, normalCounts, normalUnique, normalHex);
        addFrameToCatalogue(invertedCatalogue, invertedCounts, invertedUnique, invertedHex);

        Serial.print("Frame ");
        Serial.print(frameIndex);
        Serial.print(" bits=");
        Serial.print(frameBitCount);
        Serial.print(" normal=");
        Serial.print(normalHex);
        Serial.print(" inverted=");
        Serial.println(invertedHex);
        if (VERBOSE) {
          Serial.print(" bits_raw=");
          Serial.println(frameBits);
        }

        frameIndex++;
        frameBitCount = 0;
        frameOpen = false;
      }

      continue;
    }

    uint8_t runBits = durationToBits(dt);
    if (runBits == 0) continue;

    uint8_t bitValue = levels[i - 1];

    if (!frameOpen) {
      frameOpen = true;
      frameBitCount = 0;
    }

    for (uint8_t b = 0; b < runBits; b++) {
      if (frameBitCount < MAX_FRAME_BITS - 1) {
        frameBits[frameBitCount++] = bitValue ? '1' : '0';
      }
    }
  }

  if (frameOpen && frameBitCount > 0) {
    frameBits[frameBitCount] = '\0';

    bitsToHexString(frameBits, normalHex, sizeof(normalHex), false);
    bitsToHexString(frameBits, invertedHex, sizeof(invertedHex), true);

    addFrameToCatalogue(normalCatalogue, normalCounts, normalUnique, normalHex);
    addFrameToCatalogue(invertedCatalogue, invertedCounts, invertedUnique, invertedHex);

    Serial.print("Frame ");
    Serial.print(frameIndex);
    Serial.print(" bits=");
    Serial.print(frameBitCount);
    Serial.print(" normal=");
    Serial.print(normalHex);
    Serial.print(" inverted=");
    Serial.print(invertedHex);
    if (VERBOSE) {
      Serial.print(" bits_raw=");
      Serial.println(frameBits);
    }
  }

  printFrameCatalogue(normalCatalogue, normalCounts, normalUnique, "normal");
  printFrameCatalogue(invertedCatalogue, invertedCounts, invertedUnique, "inverted");

  Serial.println("=============================================");
}

void dumpCapture() {
  noInterrupts();

  uint16_t count = edgeCount;
  bool overflow = edgeOverflow;

  static uint32_t times[MAX_EDGES];
  static uint8_t levels[MAX_EDGES];

  for (uint16_t i = 0; i < count; i++) {
    times[i] = edgeTime[i];
    levels[i] = edgeLevel[i];
  }

  edgeCount = 0;
  edgeOverflow = false;
  captureHadCarrier = false;

  interrupts();

  if (count < MIN_EDGES_TO_DUMP) return;

  captureNumber++;

  uint32_t minDt = 999999;
  uint32_t maxDt = 0;
  uint32_t sumDt = 0;

  for (uint16_t i = 1; i < count; i++) {
    uint32_t dt = times[i] - times[i - 1];

    if (dt < minDt) minDt = dt;
    if (dt > maxDt) maxDt = dt;
    sumDt += dt;
  }

  float avgDt = (count > 1) ? (float)sumDt / (float)(count - 1) : 0;

  Serial.println();
  Serial.print("========== GDO0 RAW DATA CAPTURE # ");
  Serial.print(captureNumber);
  Serial.println(" ==========");

  if (VERBOSE) {

    Serial.print("Freq MHz: ");
    Serial.println(RF_FREQ, 3);

    Serial.print("Data rate kBaud: ");
    Serial.println(RF_DATARATE, 1);

    Serial.print("Deviation kHz: ");
    Serial.println(RF_DEVIATION, 1);

    Serial.print("RX bandwidth kHz: ");
    Serial.println(RF_RXBW, 1);

    Serial.print("Edges: ");
    Serial.println(count);

    Serial.print("Overflow: ");
    Serial.println(overflow ? "YES" : "NO");
  }
  Serial.print("RSSI: ");
  Serial.println(ELECHOUSE_cc1101.getRssi());

  Serial.print("LQI: ");
  Serial.println(ELECHOUSE_cc1101.getLqi());

  Serial.print("Min dt us: ");
  Serial.println(minDt);

  Serial.print("Max dt us: ");
  Serial.println(maxDt);

  Serial.print("Average dt us: ");
  Serial.println(avgDt, 2);

  Serial.println();
  /*
  Serial.println("Timing guide:");
  Serial.println("~104 us = 9.6 kBaud single symbol");
  Serial.println("~208 us = 2 symbols at 9.6 kBaud");
  Serial.println("~312 us = 3 symbols at 9.6 kBaud");
  Serial.println("~416 us = 4 symbols at 9.6 kBaud");
*/
  reconstructBitstream(times, levels, count);
}

//************************************* Setup ****************************

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("CC1101 Smoke Alarm Discovery");

  pinMode(PIN_GDO0, INPUT);
  pinMode(PIN_GDO2, INPUT);

  configureRadio();

  if (ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("CC1101 detected");
  } else {
    Serial.println("CC1101 NOT detected");

    while (true) {
      delay(1000);
    }
  }

  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), gdo0ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_GDO2), gdo2ISR, CHANGE);

  resetCapture();

  Serial.println("Listening...");
  Serial.println("GDO0 = raw demodulated serial data");
  Serial.println("GDO2 = carrier sense gate");
}

//************************************* Loop ****************************

void loop() {
  uint32_t now = micros();

  bool shouldDump = false;

  noInterrupts();

  bool hadCarrier = captureHadCarrier;
  bool active = carrierActive;
  uint32_t sinceCarrierChange = now - lastCarrierChangeUs;
  uint16_t count = edgeCount;
  uint32_t startedAt = captureStartUs;

  interrupts();

  bool timedOut =
    hadCarrier && ((now - startedAt) > MAX_CAPTURE_US);

  if ((hadCarrier && !active && sinceCarrierChange > TRAIN_END_US && count >= MIN_EDGES_TO_DUMP) || (timedOut && count >= MIN_EDGES_TO_DUMP)) {
    shouldDump = true;
  }

  if (shouldDump) {
    dumpCapture();
    resetCapture();
    ELECHOUSE_cc1101.SetRx();
  }

  int gdo2 = digitalRead(PIN_GDO2);

  if (gdo2 != lastGdo2State) {
    lastGdo2State = gdo2;

    if (VERBOSE) {
      Serial.print("GDO2 carrier=");
      Serial.println(gdo2);
    }
  }


  if (millis() - lastStatusPrint > 2000 && VERBOSE) {
    lastStatusPrint = millis();

    Serial.print("Waiting for carrier... ");
    Serial.print("RSSI=");
    Serial.print(ELECHOUSE_cc1101.getRssi());

    Serial.print(" dBm  LQI=");
    Serial.print(ELECHOUSE_cc1101.getLqi());

    Serial.print("  edges=");
    Serial.println(edgeCount);
  }
}