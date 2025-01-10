# ESP32APRS Simple Project

ESP32APRS is a Internet Gateway(IGate)/Dital Repeater(DiGi)/Tracker/Weather(WX)/Telemetry(TLM) with TNC Built in that is implemented for Espressif ESP32 processor.
 

## Feature

* Supported hardware: ESP32DR Simple,ESP32DR,D.I.Y Other
* Supported RF Module: SA8x8/FRS VHF/UHF/350 model
* Support APRS internet gateway (IGATE)
* Support APRS digital repeater (DIGI)
* Support APRS tracker (TRACKER)
* Support GNSS External mod select UART0-2 and TCP Client
* Support TNC External mod select UART0-2 and Yaesu packet
* Support APRS IGATE/DIGI/WX with fix position for move position from GNSS
* Using ESP-Arduino development on Visual studio code + Platform IO
* Support AFSK 1600/1800Hz 300bps AFSK (For HF radio)
* Support AFSK Bell202 1200bps (For VHF/UHF radio)
* Implementing software modem, decoding and encoding
* Support monitor display information and statistices
* Support Wi-Fi multi station or WiFi Access point
* support Web Service config and control system
* support filter packet rx/tx on igate,digi,display
* support audio filter BPF,HPF
* support VPN wireguard
* support global time zone
* support web service auth login
* display received and transmit packet on the LED and display OLED

## Hardware screen short
![esp32dr_simple](image/ESP32DR_Simple_Test.png) ![esp32dr_sa868](image/ESP32DR_SA868_2.png)
![esp32dr_sa868_pcb](doc/ESP32DR_SA868/ESP32DR_SA868_Block.png)

## Hardware mod
![esp32dr_sql](image/ESP32IGate_SQL.jpg) 

## Web service screen short
![screen_dashboard](image/ESP32IGate_Screen_dashboard.png) ![screen_igate](image/ESP32IGate_Screen_igate.png) \
![screen_radio](image/ESP32IGate_Screen_radio.png) ![screen_mod](image/ESP32IGate_Screen_mod.png)

## ESP32DR_SA868
Share project [here](https://oshwlab.com/APRSTH/esp32sa818) \
Schematic [here](doc/ESP32DR_SA868/ESP32DR_SA868_sch.pdf) \
PCB Gerber [hare](doc/ESP32DR_SA868/ESP32DR_SA868_Gerber.zip)

## ESP32DR Simple

![esp32dr_simple_3d](image/ESP32DR_Simple_Model.png)

ESP32DR Simple Circut is small interface board for connecting to a transceiver.

* PCB size is 64x58mm
* PCB Single size
* RJ11 6 Pin out to Radio

### Schematic

[![schematic](image/ESP32DR_SimpleCircuit.png)](image/ESP32DR_SimpleCircuit.png)

### CAD data
 
The gerber data is [here](doc/Gerber_ESP32DR_Simple.zip)

The PCB film positive is [here](doc/PCB_Bottom.pdf)

The PCB film negative is [here](doc/PCB_Bottom_Invert.pdf)

The PCB Layout is [here](doc/PCB_Layout.pdf)

The Schematic PDF is [here](doc/ESP32DR_Simple_Schematic.pdf)

### BOM list  

|Reference|Value|Description|
|---|:---:|---|
|U1|ESP32 DEVKIT|DOIT ESP32 DEVKIT (โมดูล ESP32)|
|RP2|1K|VR 3362W (R ปรับค่าเสียงออก)|
|RP1|10K|VR 3362W (R ปรับค่าเสียงเข้า)|
|RJ11|RJ11-6P6C|แจ๊คโมดูล RJ11 แบบ 6ขา|
|R13,R12,R11,R5,R3,R9|1K|R 1K 1/4W (ค่าสี: น้ำตาล ดำ แดง)|
|R7,R18,R19|100R|R 100R  1/4W (ค่าสี: น้ำตาล ดำ ดำ)|
|R6,R2,R1|10K|R 10k  1/4W  (ค่าสี: น้ำตาล ดำ ส้ม)|
|R4|3K|R 3k 1/4W (ค่าสี: ส้ม ดำ แดง)|
|R10|33K|R 33K 1/4W (ค่าสี: ส้ม ส้ม ส้ม)|
|Q1|2N3904|ทรานซิสเตอร์ NPN (TO-92)|
|LED3|LED 3.5mm|สีเหลือง แสดงส่งสัญญาณ TX|
|LED2|LED 3.5mm|สีเขียว แสดงรับสัญญาณ RX|
|LED1|LED 3.5mm|สีแดง แสดงไฟเข้าทำงาน|
|L1|L or JMP|L Isolate or Jumper|
|C11|100uF/6.3V|ตัวเก็บประจุแบบอิเล็กโทรไลติก|
|C4,C5|100nF|ตัวเก็บประจุแบบเซรามิกมัลติเลเยอร์|
|C6|470uF/10V|ตัวเก็บประจุแบบอิเล็กโทรไลติก|
|C1,C3,C10|100nF หรือ 0.1uF|ตัวเก็บประจุแบบโพลีโพรไพลีน|
|C2|10nF หรือ 0.01uF|ตัวเก็บประจุแบบโพลีโพรไพลีน|
|D2,D1|1N4148|ไดโอด หรือใช้ C 0.01uF แทนได้|

*R18 and R19 ไม่ใส่ก็ได้.  
*D2,D1 เปลี่ยนเป็นตัวเก็บประจุแบบเซรามิกมัลติเลเยอร์ค่า 10nF แทนได้ 
*หากใช้ต่อกับวิทยุรับส่งเข้าขาไมค์นอก ให้เปลี่ยน R4 เป็น 100K

จัดซื้อชุดคิทผ่าน Shopee ได้ที่ [คลิ๊ก](https://shopee.co.th/product/45191268/13373396785)

The Howto DIY is [here](doc/ESP32DR_DIY-Thai.pdf)

### Mounting drawing

![mounting](image/ESP32DR_SimpleLayout.png)

### Transceiver connection

Solder jumper is needed depending on a transceiver.

![ESP32DR_Pinout](image/RJ12Pinout.png)

|Manufacture|RJ11-1 (+VIN)|RJ11-2 (SPK)|RJ11-3 (PTT)|RJ11-4 (GND)|RJ11-5 (MIC)|RJ11-6 (SQL)|
|---|---|---|---|---|---|---|
|Alinco DR-135(DB9)|-|2|7|5|9|1|
|IC2200(RJ45)|-|SP|4|5|6|-|
|FT-2800(RJ11)|-|SP|1|3|2|-|
|HT Mic Cable|-|SPK|PTT|GND|MIC|-|

for Alinco DR-135(DB9)

![Alinco](image/ESP32DR_DR135.png)

for ICOM IC2200(RJ45)

![IC2200](image/ESP32DR_IC2200.png)

for Yaesu FT-2800(RJ11)

![FT2800](image/ESP32DR_FT2800.png)

for Handheld

![Handheld](image/ESP32DR_HT.png)

![HT-RX](image/ESP32DR_RxOnly.png)


## ESP32IGate firmware installation (do it first time, next time via the web browser)
- 1.Connect the USB cable to the ESP32 Module.
- 2.Download firmware and open the program ESP32 DOWNLOAD TOOL, set it in the firmware upload program, set the firmware to ESP32IGate_Vxx.bin, location 0x10000 and partitions.bin at 0x8000 and bootloader.bin at 0x1000 and boot.bin at 0xe000, if not loaded, connect GPIO0 cable to GND, press START button finished, press power button or reset (red) again.
- 3.Then go to WiFi AP SSID: ESP32IGate and open a browser to the website. http://192.168.4.1 password: aprsthnetwork Can be fixed Or turn on your Wi-Fi router.
- 4.Push **BOOT** button long >100ms to TX Position and >10Sec to Factory Default

![ESP32Tool](image/ESP32Tool.png)

## ESP32 Flash Download Tools
https://www.espressif.com/en/support/download/other-tools


## PlatformIO Quick Start

1. Install [Visual Studio Code](https://code.visualstudio.com/) and [Python](https://www.python.org/)
2. Search for the `PlatformIO` plugin in the `VisualStudioCode` extension and install it.
3. After the installation is complete, you need to restart `VisualStudioCode`
4. After restarting `VisualStudioCode`, select `File` in the upper left corner of `VisualStudioCode` -> `Open Folder` -> select the `ESP32APRS_T-TWR` directory
5. Click on the `platformio.ini` file, and in the `platformio` column, cancel the sample line that needs to be used, please make sure that only one line is valid
6. Click the (✔) symbol in the lower left corner to compile
7. Connect the board to the computer USB
8. Click (→) to upload firmware and reboot again
9. After reboot display monitor and reconfig

## APRS Server service

- APRS SERVER of T2THAI at [aprs.dprns.com:14580](http://aprs.dprns.com:14501)
- APRS SERVER of T2THAI ampr host at [aprs.hs5tqa.ampr.org:14580](http://aprs.hs5tqa.ampr.org:14501)
- APRS MAP SERVICE [http://aprs.nakhonthai.net](http://aprs.nakhonthai.net)

## Donate

To support the development of ESP32APRS you can make us a donation using [github sponsors](https://github.com/sponsors/nakhonthai). \
If you want to donate some hardware to facilitate APRS porting and development, [contact us](https://www.facebook.com/atten). \
<a href="https://www.paypal.me/hs5tqa"><img src="blue.svg" height="40"></a> 

## ESP32 Flash Download Tools
https://www.espressif.com/en/support/download/other-tools

## Credits & Reference

- ESP32TNC project by amedes [ESP32TNC](https://github.com/amedes/ESP32TNC)
- APRS Library by markqvist [LibAPRS](https://github.com/markqvist/LibAPRS)

## HITH
This project implement by APRS text (TNC2 Raw) only,It not support null string(0x00) in the package.

## Version 1.4
- Change SSID name and SSID Password
- Compress with gzip style.css ( )
- Use Cache-Control in css for better performance (max-age=86400 // 1 day)
- Change aprs.dprns.com to rotate.aprs2.net