# ESPHome Red R10RF Smoke Alarm RF Detector

The detector passively listens for alarm transmissions and exposes them to Home Assistant as standard entities, thus allowing you to send yourself notifications or trigger events if your house is on fire. 

Unlike the original prototype, this version allows alarm signatures to be configured in YAML and exposes unknown RF frames to assist users in identifying additional alarm models and firmware revisions.

This component passively monitors the R10RF 433Mhz RF network and exposes alarm events to Home Assistant as standard entities. It does not transmit RF signals and does not modify the operation of the smoke alarm system. I have not integrated a transmit function to set off the alarms from home assistant as this constitutes 'meddling' with the system and could prevent proper operation, invalidate warranties, insurance, etc, and possibly cause people to die in horrendous house fires. Lets not do that.

## Features

* Receive only operation
* No RF transmission
* ESP32 + CC1101 based
* Home Assistant native integration via ESPHome
* User configurable alarm signatures
* Detection of known alarm frames
* Reporting of unknown alarm frames
* Reporting of last matched frame
* Reporting of received signal strength indication (RSSI)
* Community discoverable protocol support

---

# Hardware

## Tested Hardware

* ESP32 S3 WROOM
* CC1101 433 MHz transceiver

## Wiring

| CC1101 | ESP32   |
| ------ | ------- |
| VCC    | 3.3 V   |
| GND    | GND     |
| SCK    | GPIO 15 |
| MISO   | GPIO 16 |
| MOSI   | GPIO 17 |
| CSN    | GPIO 18 |
| GDO0   | GPIO 7  |
| GDO2   | GPIO 6  |

---

# Installation

## Files

Place the following files in:

```text
/config/esphome/
```

```text
smokealarmrfdetector-c1101.yaml
r10rf_cc1101.h
```

## ESPHome Configuration

The detector uses the SmartRC CC1101 library.

```yaml
esphome:
  name: smokealarmrfdetector-c1101
  friendly_name: Smoke Alarm RF Detector

  includes:
    - r10rf_cc1101.h

  libraries:
    - SPI
    - SmartRC-CC1101-Driver-Lib=https://github.com/LSatan/SmartRC-CC1101-Driver-Lib.git

  on_boot:
    priority: 600
    then:
      - lambda: |-
          r10rf_set_known_codes("${known_alarm_codes}");
          r10rf_setup();
```

---

# Alarm Signature Configuration

Known alarm signatures are configured entirely in YAML.

Example:

```yaml
substitutions:
  known_alarm_codes: "5ba9ce8a,aaaa5ba9ce8a,5555a4563175"
```

The detector will trigger if any decoded frame contains one of these signatures. The ones included were the ones I detected from my alarms in Australia. Additional codes may be added later without modifying the C++ source.

Example:

```yaml
substitutions:
  known_alarm_codes: "5ba9ce8a,b12cd34e,cafe1234"
```

---

# Home Assistant Entities

## Alarm State

```text
binary_sensor.r10rf_alarm
```

Turns on when a configured alarm frame is detected.

---

## RSSI

```text
sensor.r10rf_rssi
```

Received signal strength of the most recent detection.

---

## Last Matched Frame

```text
text_sensor.r10rf_last_frame
```

The frame that triggered alarm detection.

Example:

```text
aaaa5ba9ce8a
```

---

## Last Raw Frame

```text
text_sensor.r10rf_last_raw_frame
```

The most recently decoded RF frame regardless of whether it matched.

Useful for protocol exploration.

---

## Last Unknown Frame

```text
text_sensor.r10rf_last_unknown_frame
```

Stores the most recent frame that did not match any configured alarm signature.

Useful when testing other alarm models.

Example:

```text
aaaab12cd34e
```

---

# Discovering New Alarm Codes

The detector can assist in discovering previously unknown alarm signatures.

## Step 1

Temporarily configure a broad alarm signature list:

```yaml
substitutions:
  known_alarm_codes: "5ba9ce8a"
```

## Step 2

Trigger the alarm.

Possible methods:

* Test button
* Smoke test
* Pairing mode

## Step 3

Observe:

```text
text_sensor.r10rf_last_raw_frame
text_sensor.r10rf_last_unknown_frame
```

## Step 4

Identify the repeated stable frame.

Example captures:

```text
aaaa5ba9ce8a
aaaa5ba9ce8a0
aaaa5ba9ce8a2
```

Stable core:

```text
5ba9ce8a
```

## Step 5

Add the stable frame to:

```yaml
substitutions:
  known_alarm_codes: "5ba9ce8a,newcode1234"
```

Recompile and upload.

---

# Example Automation

```yaml
alias: Smoke Alarm RF Triggered

triggers:
  - trigger: state
    entity_id: binary_sensor.r10rf_alarm
    to: "on"

actions:
  - action: notify.mobile_app_phone
    data:
      message: Smoke alarm RF signal detected
```

---

# RF Characteristics

Current protocol observations:

| Parameter  | Value      |
| ---------- | ---------- |
| Frequency  | 433.92 MHz |
| Modulation | 2 FSK      |
| Data Rate  | ~9.6 kBaud |
| Bit Period | ~104 µs    |

Observed alarm frame:

```text
aaaa5ba9ce8a
```

Observed variants:

```text
aaaa5ba9ce8a0
aaaa5ba9ce8a2
aaaa5ba9ce8a4
```

These are believed to be frame boundary artefacts and are normalised during detection.

---

# Supporting Additional RF Devices

The detector framework can be extended to support:

* Other Red RF alarms
* Aico RF alarms
* RF pool controllers
* Weather stations
* Generic 433 MHz devices

Unknown frames are intentionally exposed through Home Assistant to simplify protocol analysis and community contribution.

---

# Limitations

This project is not affiliated with Red Smoke Alarms.

This detector:

* Is receive only
* Is not certified
* Is not a replacement for smoke alarms
* Should only be used for monitoring and automation

---

# Safety Notice

Smoke alarms are life safety devices.

This software is intended only as an auxiliary monitoring system. It must not be relied upon as the sole means of fire detection or warning.

---

# Contributing

Useful contributions include:

* Additional Red RF alarm captures
* Pairing mode captures
* Fault condition captures
* Low battery captures
* Additional RF alarm brands
* Protocol analysis

Please include:

* Raw frame captures
* Device model numbers
* Firmware revisions where available
* Capture circumstances

# Research Notes

This section documents the reverse engineering process used to identify the Red R10RF RF alarm signal and develop the ESPHome detector.

## Aim

The aim was to discover how Red R10RF interconnected smoke alarms communicate with each other over radio frequency (RF), then use that information to build a passive Home Assistant detector.

The goal was not to transmit to the alarm network, but to detect when the alarm network is active.

## Research Questions

| ID  | Question                                                                                          |
| --- | ------------------------------------------------------------------------------------------------- |
| RQ1 | What RF frequency do the Red R10RF alarms use?                                                    |
| RQ2 | What modulation, data rate, and packet format are used?                                           |
| RQ3 | Do individual alarms transmit unique device codes, or do they transmit a shared alarm event code? |

## Summary of Findings

| Parameter                | Finding                              |
| ------------------------ | ------------------------------------ |
| Frequency                | 433.92 MHz                           |
| Modulation               | 2 FSK                                |
| Data rate                | ~9.6 kBaud                           |
| Symbol period            | ~104 µs                              |
| Packet structure         | Repeated short frame                 |
| Inter packet gap         | ~1.3 ms observed in CC1101 captures  |
| Canonical alarm frame    | `aaaa5ba9ce8a`                       |
| Preamble                 | `aaaa`                               |
| Likely alarm payload     | `5ba9ce8a`                           |
| Inverted equivalent      | `5555a4563175`                       |
| Device specific identity | Not observed in decoded alarm frames |

The working interpretation is:

```text
AAAA        preamble
5BA9CE8A    shared alarm interconnect frame
```

## Safety Boundary

This project is receive only.

No transmission code is provided and no attempt is made to spoof, replay, or inject packets into the smoke alarm network. Smoke alarms are life safety systems. This detector should be treated only as an auxiliary monitoring signal for Home Assistant.

---

# Method

## 1. Frequency Identification

Initial investigation used an RTL SDR receiver and SDRSharp to monitor the 433 MHz ISM band while triggering the alarm test function.

A strong RF signal was observed around 433.92 MHz
<img width="1019" height="550" alt="image" src="https://github.com/user-attachments/assets/ad1772b9-d8f9-40e6-92d8-e993f298a44b" />


This aligned with the expected frequency for many 433 MHz RF interconnect and remote control devices.

This answered RQ1.

## 2. Initial RF Capture

The RF signal was captured using SDRSharp baseband recording.

The useful recording settings were:

```text
Centre frequency: 433.92 MHz
Recording type: baseband IQ
Sample format: 16 bit PCM IQ
Sample rate: 2.4 MSPS
```

Audio demodulated WAV recordings were not useful for detailed protocol analysis because they discard important phase and frequency information. Baseband IQ capture was required.
<img width="988" height="228" alt="image" src="https://github.com/user-attachments/assets/e68ec1a9-6944-4864-916a-7100151d5e82" />
<img width="919" height="256" alt="image" src="https://github.com/user-attachments/assets/52ec8950-625c-40c2-9406-3012e5b766fd" />


## 3. Visual Inspection

Waterfall and spectrum views showed that the signal was not simple on off keying (OOK).

The signal appeared as two frequency states around the carrier, suggesting:

```text
2 FSK
```

or a closely related shaped FSK mode.
The RF activity appeared as a train of repeated short packets rather than a single long packet.

## 4. URH Analysis

Universal Radio Hacker (URH) was then used to inspect the IQ captures.
<img width="984" height="576" alt="image" src="https://github.com/user-attachments/assets/ba3b32da-c6cd-4265-a571-2218819cf727" />
<img width="411" height="336" alt="image" src="https://github.com/user-attachments/assets/680016f4-3b78-4fe1-805f-6093e525d4ca" />


Several initial settings gave plausible but unstable results. Early interpretations around 19.2 ksym/s produced repeated looking frames, but the decoded symbols were not fully stable.

A more consistent result was obtained later using CC1101 edge timing, which showed that the effective bit period was approximately:

```text
104 µs
```

This corresponds to:

```text
1 / 104 µs ≈ 9.6 kBaud
```

This refined the protocol estimate from 19.2 kBaud to 9.6 kBaud.

## 5. CC1101 Capture

An ESP32 S3 and CC1101 module were then used to capture the demodulated data directly.

The CC1101 was configured for:

```text
Frequency:        433.92 MHz
Modulation:       2 FSK
Data rate:        9.6 kBaud
Deviation:        47.6 kHz
RX bandwidth:     203.1 kHz
Packet mode:      disabled
Sync detection:   disabled
CRC:              disabled
Whitening:        disabled
Manchester:       disabled
```

GDO pins were used as follows:

| CC1101 pin | Function                    |
| ---------- | --------------------------- |
| GDO0       | Raw demodulated serial data |
| GDO2       | Carrier sense gate          |

This allowed the ESP32 to record edge timings only while an RF carrier was present.

## 6. Edge Timing Analysis

The ESP32 recorded microsecond intervals between changes on GDO0.

The most important observation was that the transition intervals clustered around:

```text
104 µs
208 µs
312 µs
```

These are clean multiples of 104 µs.

This strongly indicated a non return to zero (NRZ) style bitstream at approximately:

```text
9.6 kBaud
```

The 208 µs and 312 µs intervals represent runs of two or three identical bits.

This answered the main modulation and data rate part of RQ2.

## 7. Bitstream Reconstruction

The decoder reconstructed the bitstream by expanding each GDO0 level run according to:

```text
bit count = round(duration_us / 104)
```

Frames were split when the interval exceeded a configured gap threshold.

The stable repeated decoded frame was:

```text
aaaa5ba9ce8a
```

The bitstream form was:

```text
10101010101010100101101110101001110011101000101
```

The start of the frame is a clear alternating preamble:

```text
aaaa
```

The remaining stable portion is interpreted as the alarm event frame:

```text
5ba9ce8a
```

This answered the packet format component of RQ2.

---

# Master and Slave Testing

## 8. Solo Unit Tests

A master alarm and several slave alarms were triggered individually.

The same canonical frame was observed:

```text
aaaa5ba9ce8a
```

No reliable master specific or slave specific code was identified.

## 9. Pairing Mode Tests

Pairing mode was also captured by placing a master into pairing mode and then joining a slave.

Pairing captures showed repeated fragments and shifted forms of the same bitstream, including:

```text
aaaa5ba9c
5554b7538
aaa96ea70
```

These appear to be bit shifted, inverted, truncated, or boundary affected variants of the same underlying frame family rather than independent serial numbers.

## 10. Smoke Activation Test

A real smoke activation was compared with test button activations.

The same canonical frame was observed:

```text
aaaa5ba9ce8a
```

The smoke activation produced longer or more numerous transmission trains, but did not reveal a different smoke specific payload.

---

# Answer to Research Questions

## RQ1: What frequency?

The Red R10RF signal was observed at:

```text
433.92 MHz
```

## RQ2: What protocol and format?

The observed signal is:

```text
433.92 MHz
2 FSK
9.6 kBaud
~104 µs bit period
Repeated short packet
```

The observed canonical alarm frame is:

```text
aaaa5ba9ce8a
```

Proposed frame interpretation:

```text
AAAA        preamble
5BA9CE8A    shared alarm interconnect frame
```

The inverted equivalent is:

```text
5555a4563175
```

## RQ3: Unique device codes or shared alarm code?
No unique device identifier was observed in the decoded alarm frames.
The evidence currently supports a shared alarm interconnect frame:

```text
5ba9ce8a
```

rather than separate master, slave, or device serial codes.
It is still possible that device identity exists in other message types such as low battery, fault, enrolment, or periodic supervision frames, but it was not observed in the alarm event captures. The batteries are sealed, non-replaceable 10 year type and I didnt want to trash or damage a working unit.

---

# Observed Frame Variants

The following variants were observed:

```text
aaaa5ba9ce8a
aaaa5ba9ce8a0
aaaa5ba9ce8a2
aaaa5ba9ce8a4
```

These are treated as boundary artefacts caused by frame splitting, trailing bits, or slicer timing. The stable core is:

```text
5ba9ce8a
```

The detector therefore matches stable substrings rather than requiring every captured frame to be exactly the same length.

---

# Practical Home Assistant Detection Strategy

The detector does not need to fully emulate the alarm protocol.
It only needs to recognise repeated RF evidence of an alarm event.
The current detection strategy is:

```text
1. Listen at 433.92 MHz.
2. Demodulate 2 FSK at 9.6 kBaud.
3. Reconstruct frames from GDO0 edge timings.
4. Match known alarm signatures such as 5ba9ce8a.
5. Expose a Home Assistant binary sensor when a match occurs.
```
This approach is intentionally conservative and receive only.

---

# Notes for Future Research

Useful future captures would include:
* Low battery warnings
* Fault warnings
* Long idle monitoring
* Pairing with other production batches
* Other Red RF alarm models
* Other firmware revisions
* Different RF interconnect products

These may reveal additional event codes or device specific identifiers not present in alarm activation packets.

---

# Reproducibility Notes

The key tools used were:
* RTL SDR with a simple monopole antenna
* SDRSharp
* Universal Radio Hacker
* ESP32 S3
* CC1101 RF module
* SmartRC CC1101 Arduino library
* ESPHome

The most useful capture workflow was:

```text
1 RTL SDR IQ capture 
2 URH exploratory analysis
3 CC1101 raw GDO0 edge capture
4 ESP32 timing reconstruction
5 ESPHome detector
```

The CC1101 edge timing method produced the clearest and most repeatable results.


