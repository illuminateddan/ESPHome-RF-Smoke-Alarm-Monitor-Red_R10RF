# ESPHome Red R10RF Smoke Alarm RF Detector

An ESPHome external component for detecting RF alarm transmissions from Red R10RF interconnected smoke alarms using a CC1101 radio transceiver and an ESP32, thus allowing you to send yourself notifications or trigger events if your house is on fire. 

This component passively monitors the R10RF RF network and exposes alarm events to Home Assistant as standard entities. It does not transmit RF signals and does not modify the operation of the smoke alarm system. I have not integrated a transmit function to set off the alarms from home assistant as this constitutes 'meddling' with the system and could prevent proper operation, invalidate warranties, insurance, etc, and possibly cause people to die in horrendous house fires. Lets not do that.

## Features

* Receive only operation
* Detects Red R10RF alarm transmissions
* CC1101 based receiver
* ESP32 compatible
* Native Home Assistant integration via ESPHome
* Exposes alarm status, received signal strength indication (RSSI), and last decoded frame
* No cloud services required

## Hardware

### Tested Hardware

* ESP32 S3 WROOM
* CC1101 433 MHz transceiver module

### Wiring

| CC1101 | ESP32 S3 |
| ------ | -------- |
| VCC    | 3.3 V    |
| GND    | GND      |
| SCK    | GPIO 15  |
| MISO   | GPIO 16  |
| MOSI   | GPIO 17  |
| CSN    | GPIO 18  |
| GDO0   | GPIO 7   |
| GDO2   | GPIO 6   |

### Radio Configuration

The component is currently configured for:

| Parameter         | Value      |
| ----------------- | ---------- |
| Frequency         | 433.92 MHz |
| Modulation        | 2 FSK      |
| Data Rate         | 9.6 kBaud  |
| Receive Bandwidth | 203.1 kHz  |
| Deviation         | 47.6 kHz   |

## Installation

### GitHub External Component

Add the repository to your ESPHome configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/illuminateddan/esphome-r10rf-smoke-detector
```

### Local Installation

Copy:

```text
components/r10rf_detector
```

to:

```text
/config/esphome/components/r10rf_detector
```

and add:

```yaml
external_components:
  - source:
      type: local
      path: components
```

## Example Configuration

```yaml
esphome:
  name: smoke_detector

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: arduino

external_components:
  - source:
      type: git
      url: https://github.com/YOUR_USERNAME/esphome-r10rf-smoke-detector

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

r10rf_detector:
  gdo0_pin: 7
  gdo2_pin: 6
  cs_pin: 18
  sck_pin: 15
  miso_pin: 16
  mosi_pin: 17
```

## Home Assistant Entities

The component creates the following entities:

### Binary Sensor

```text
binary_sensor.smoke_alarm_rf
```

Becomes ON when an R10RF alarm transmission is detected.

### Sensor

```text
sensor.smoke_alarm_rssi
```

Reports received signal strength.

### Text Sensor

```text
text_sensor.smoke_alarm_last_frame
```

Reports the most recently decoded frame (for testing).

## Detection Method

The detector was developed through reverse engineering of hundreds of captured R10RF transmissions (see research bit below).

Observed characteristics:

| Parameter   | Value      |
| ----------- | ---------- |
| Frequency   | 433.92 MHz |
| Modulation  | 2 FSK      |
| Symbol Rate | ~9.6 kBaud |
| Bit Period  | ~104 µs    |

Typical alarm frame:

```text
aaaa5ba9ce8a
```

Observed variants include:

```text
aaaa5ba9ce8a0
aaaa5ba9ce8a2
aaaa5ba9ce8a4
```
These appear to be frame boundary artefacts and are normalised internally.

## Example Automation

```yaml
alias: Smoke Alarm RF Triggered

triggers:
  - trigger: state
    entity_id: binary_sensor.smoke_alarm_rf
    to: "on"

actions:
  - action: notify.mobile_app_phone
    data:
      message: Smoke alarm RF signal detected
```

## Limitations

This project is not affiliated with Red Smoke Alarms.

This detector:

* Does not replace certified smoke alarm systems
* Does not provide life safety certification
* Should only be used as an auxiliary monitoring device
* Should not be relied upon as the sole means of smoke detection

## Safety Notice

Smoke alarms are life safety devices.

This software is intended for monitoring and integration purposes only. Installation, testing, and maintenance of smoke alarms should always comply with local regulations and manufacturer requirements.

## Contributing

Pull requests, issue reports, protocol analysis, and additional capture datasets are welcome.

Particularly useful contributions include:

* Additional R10RF firmware versions
* Long term capture datasets
* Pairing mode captures
* Fault and low battery captures
* Additional supported RF alarm models

## License

MIT License


************************** Research **************************
There was a bit of research to get this working which I thought I'd briefly document for those interested:
AIM: 
Discover how the Red R10RF interconnected smoke alarms connect and trigger each other with the eventual aim of creating a home assistant integration to detect when an alarm goes off.

Research Questions:
RQ1: What frequency?
RQ2: What is the protocol and format of the alarms
RQ3: do they have unique codes or just an "alarm" code?

Steps:
1) Use RTL-SDR (+SDR sharp) to capture RF from alarm when it goes off. (433.92Mhz) (RQ1)
2) Identify protocols, datarate, etc using UHR (ultimate Radio Hacker)
3) Use ESP32-S3 and CC1101 to capture and decode signal to discover preamble, payload, etc, and resolve protocol info
    3.1) Protocol fiindings: (RQ2)
    433.92 MHz
    2 FSK
    9600 baud
    104 µs symbol
    Repeated packet
    ~1.3 ms inter-packet gap   
4) Identify differences between Master unit and slaves. RQ3: Are they the same trigger code, or does each identify itself?
  4.1) 
    Canonical alarm frame:  aaaa5ba9ce8a
    Preamble:               aaaa
    Likely alarm payload:    5ba9ce8a
    Inverted equivalent:     5555a4563175
  4.2) boundary artefacts appear at end: (probs ignore)
      aaaa5ba9ce8a
      aaaa5ba9ce8a0
      aaaa5ba9ce8a2
      aaaa5ba9ce8a4
   4.3)
   summary: (RQ1,2,3)
    433.92 MHz
    2 FSK
    9.6 kbaud
    ~104 µs bit period
    AAAA preamble
    5BA9CE8A shared alarm interconnect frame
