# ESP32APRS Simple Project (Optimized Fork)

This is an optimized fork of the amazing [ESP32APRS project](https://github.com/nakhonthai/ESP32APRS) originally created by **HS5TQA**. The purpose of this fork is to improve and simplify its functionality while maintaining the core features of the original project.

## Overview

ESP32APRS is an Internet Gateway (IGate), Digital Repeater (Digi), Tracker, Weather Station (WX), and Telemetry (TLM) device with a built-in TNC implemented for the Espressif ESP32 processor.

---

## Features

- **Supported Hardware:** ESP32DR Simple, ESP32DR, DIY alternatives
- **Supported RF Modules:** SA8x8/FRS VHF/UHF/350 models
- **Supports:**  
  - APRS Internet Gateway (IGATE)  
  - APRS Digital Repeater (DIGI)  
  - APRS Tracker (TRACKER)  
  - GNSS External Module via UART0-2 and TCP Client  
  - External TNC Module via UART0-2 and Yaesu Packet  
  - IGATE/DIGI/WX with fixed or moving position via GNSS
- **Development Environment:** ESP-Arduino on Visual Studio Code + PlatformIO
- **Modem Support:**  
  - AFSK 1600/1800Hz 300bps (For HF Radio)  
  - AFSK Bell202 1200bps (For VHF/UHF Radio)  
  - Software modem for encoding and decoding
- **Monitoring and Configuration:**  
  - Display information and statistics  
  - Wi-Fi multi-station or Access Point  
  - Web Service for configuration and control  
  - Packet filtering for IGATE, DIGI, and display  
  - Audio filters: BPF, HPF  
  - VPN using WireGuard  
  - Global time zone support  
  - Web service authentication login  
  - Packet transmission and reception displayed on LED and OLED screen  

---

## Hardware Screenshots

![ESP32DR Simple Test](image/ESP32DR_Simple_Test.png) ![ESP32DR SA868](image/ESP32DR_SA868_2.png)  
![ESP32DR SA868 PCB](doc/ESP32DR_SA868/ESP32DR_SA868_Block.png)

---

## Hardware Modifications

![ESP32DR SQL](image/ESP32IGate_SQL.jpg)

---

## Web Service Screenshots

![Dashboard Screen](image/ESP32IGate_Screen_dashboard.png) ![IGate Screen](image/ESP32IGate_Screen_igate.png)  
![Radio Screen](image/ESP32IGate_Screen_radio.png) ![Mod Screen](image/ESP32IGate_Screen_mod.png)

---

## ESP32DR SA868

- Project shared [here](https://oshwlab.com/APRSTH/esp32sa818)  
- Schematic [here](doc/ESP32DR_SA868/ESP32DR_SA868_sch.pdf)  
- PCB Gerber files [here](doc/ESP32DR_SA868/ESP32DR_SA868_Gerber.zip)  

---

## ESP32DR Simple

![ESP32DR Simple 3D Model](image/ESP32DR_Simple_Model.png)

The ESP32DR Simple Circuit is a compact interface board for connecting to a transceiver.

- PCB dimensions: 64x58mm  
- Single-layer PCB  
- RJ11 6-pin output to Radio  

### Schematic

[![Schematic](image/ESP32DR_SimpleCircuit.png)](image/ESP32DR_SimpleCircuit.png)

### CAD Data

- Gerber data: [here](doc/Gerber_ESP32DR_Simple.zip)  
- PCB film positive: [here](doc/PCB_Bottom.pdf)  
- PCB film negative: [here](doc/PCB_Bottom_Invert.pdf)  
- PCB layout: [here](doc/PCB_Layout.pdf)  
- Schematic PDF: [here](doc/ESP32DR_Simple_Schematic.pdf)  

---

## Firmware Installation

1. Connect the USB cable to the ESP32 module.  
2. Use the ESP32 Download Tool to upload firmware.  
3. Set firmware configurations as described and connect GPIO0 to GND if required.  
4. After upload, connect to the AP SSID: `ESP32IGate` and open a browser at [http://192.168.4.1](http://192.168.4.1), password: `aprsthnetwork`.  

![ESP32 Tool](image/ESP32Tool.png)

---

## PlatformIO Quick Start

1. Install [Visual Studio Code](https://code.visualstudio.com/) and [Python](https://www.python.org/).  
2. Search for and install the PlatformIO plugin in Visual Studio Code.  
3. Restart Visual Studio Code and open the `ESP32APRS_T-TWR` directory.  
4. Configure the `platformio.ini` file for your build.  
5. Compile and upload firmware to the ESP32.  

---

## APRS Server Services

- APRS Server: [aprs.dprns.com:14580](http://aprs.dprns.com:14501)  
- APRS Map Service: [http://aprs.nakhonthai.net](http://aprs.nakhonthai.net)

---

## Donate

Support the development of ESP32APRS through [GitHub Sponsors](https://github.com/sponsors/nakhonthai) or [PayPal](https://www.paypal.me/hs5tqa).

---

## Credits & References

- [ESP32TNC Project by amedes](https://github.com/amedes/ESP32TNC)  
- [APRS Library by markqvist](https://github.com/markqvist/LibAPRS)
- [nakhonthai/ESP32APRS](https://github.com/nakhonthai/ESP32APRS)

---

## Changes in This Fork
- Improved stability in [WebServer].
- Added support for [Boostrap 5].
- Optimized [Wifi] for better performance.
- Optimized [WebServer] for better performance.

---

This fork aims to improve usability and enhance features while building on the fantastic work of **HS5TQA**.

---

## Disclaimer
This software is provided "as is", without any warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose, and noninfringement. See the [GNU General Public License](https://www.gnu.org/licenses/gpl-3.0.html) for more details.
