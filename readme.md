AIM: 
discover how the Red R10RF interconnected smoke alarms connect and trigger each other with the eventual aim of creating a home assistant integration to detect when an alarm goes off.

Research Questions:
RQ1: What frequency?
RQ2: What is the protocol and format of the alarms
RQ3: do they have unique codes or just an "alarm" code?

Steps so far:
1) Use SDR to capture RF from alarm when it goes off. (433.92Mhz) (RQ1)
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
