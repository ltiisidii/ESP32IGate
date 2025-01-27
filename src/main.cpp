/*
 Name:		ESP32 APRS Internet Gateway
 Created:	1-Nov-2021 14:27:23
 Author:	HS5TQA/Atten
 Support IS: host:aprs.dprns.com port:14580
 Support IS monitor: http://aprs.dprns.com:14501
 Support in LINE Group APRS Only
*/

#include <Arduino.h>
#include "driver/pcnt.h"
#include <esp_task_wdt.h>
#include "main.h"
#include <LibAPRSesp.h>
#include <limits.h>
#include <KISS.h>
#include "webservice.h"
#include <WiFiUdp.h>
#include "ESP32Ping.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClient.h>
#include "cppQueue.h"
#include "digirepeater.h"
#include "igate.h"
#include "driver/pcnt.h"

#include <TinyGPS++.h>
#include <pbuf.h>
#include <parse_aprs.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSerifItalic9pt7b.h>
#include <Fonts/Seven_Segment24pt7b.h>

#include <WiFiUdp.h>

#include <WiFiClientSecure.h>

#include <SoftwareSerial.h>
// SoftwareSerial ss(-1, -1);

#include <ModbusMaster.h>

#include "AFSK.h"

// #include <PPPoS.h>

#define EEPROM_SIZE 4096

#ifdef BLUETOOTH
#include "BluetoothSerial.h"
#endif

#include <Wire.h>
#ifdef OLED
#include "Adafruit_SSD1306.h"
#include <Adafruit_GFX.h>
#include <Adafruit_I2CDevice.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET 4        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#endif

struct pbuf_t aprs;
ParseAPRS aprsParse;

TinyGPSPlus gps;

// instantiate ModbusMaster object
ModbusMaster modbus;

#define VBAT_PIN 35
// #define WIRE 4
//  #define POWER_PIN 12
//  #define PULLDOWN_PIN 27
//  #define SQL_PIN 33

// #define MODEM_PWRKEY 5
// #define MODEM_TX 17
// #define MODEM_RX 16

#define LED_TX 4
#define LED_RX 2

#define PPP_APN "internet"
#define PPP_USER ""
#define PPP_PASS ""

const char *str_status[] = {
    "IDLE_STATUS",
    "NO_SSID_AVAIL",
    "SCAN_COMPLETED",
    "CONNECTED",
    "CONNECT_FAILED",
    "CONNECTION_LOST",
    "DISCONNECTED"};

// PPPoS ppp;

WiFiMulti wifiMulti;

time_t systemUptime = 0;
time_t wifiUptime = 0;

boolean KISS = false;
bool aprsUpdate = false;

boolean gotPacket = false;
AX25Msg incomingPacket;

bool lastPkg = false;
bool afskSync = false;
String lastPkgRaw = "";
float dBV = 0;
int mVrms = 0;

long timeNetwork, timeAprs, timeGui;

cppQueue PacketBuffer(sizeof(AX25Msg), 5, IMPLEMENTATION); // Instantiate queue
#ifdef OLED
cppQueue dispBuffer(300, 5, IMPLEMENTATION);
cppQueue queTxDisp(sizeof(txDisp), 5, IMPLEMENTATION); // Instantiate queue

void pushTxDisp(uint8_t ch, const char *name, char *info)
{
    if (!config.tx_display)
        return;

    txDisp pkg;

    pkg.tx_ch = ch;
    strcpy(pkg.name, name);
    strcpy(pkg.info, info);
    queTxDisp.push(&pkg); // ใส่แพ็จเก็จจาก TNC ลงคิวบัพเฟอร์
}
#endif

statusType status;
RTC_DATA_ATTR igateTLMType igateTLM;
RTC_DATA_ATTR dataTLMType systemTLM;
txQueueType *txQueue;
// #ifdef BOARD_HAS_PSRAM
// txQueueType *txQueue;
// #else
// RTC_DATA_ATTR txQueueType txQueue[PKGTXSIZE];
// #endif
RTC_DATA_ATTR double LastLat, LastLng;
RTC_DATA_ATTR time_t lastTimeStamp;
RTC_DATA_ATTR uint32_t COUNTER0_RAW;
RTC_DATA_ATTR uint32_t COUNTER1_RAW;

extern RTC_DATA_ATTR uint8_t digiCount;

String RF_VERSION;

Configuration config;

pkgListType *pkgList;

TelemetryType *Telemetry;

TaskHandle_t taskNetworkHandle;
TaskHandle_t taskAPRSHandle;
TaskHandle_t taskAPRSPollHandle;
TaskHandle_t taskSerialHandle;
TaskHandle_t taskGPSHandle;

unsigned long timerNetwork, timerNetwork_old;
unsigned long timerAPRS, timerAPRS_old;
unsigned long timerGPS, timerGPS_old;
unsigned long timerSerial, timerSerial_old;

SoftwareSerial SerialRF;

bool firstGpsTime = true;
time_t startTime = 0;

#ifdef BLUETOOTH
BluetoothSerial SerialBT;

void Bluetooth()
{
    if (SerialBT.available())
    {
        char raw[500];
        int i = 0;
        memset(raw, 0, sizeof(raw));
        i = SerialBT.readBytes((uint8_t *)&raw, 500);

        if (config.bt_mode == 1)
        { // TNC2RAW MODE
            pkgTxPush(raw, strlen(raw), 1);
        }
        else if (config.bt_mode == 2)
        {
            // KISS MODE
            for (int n = 0; n < i; n++)
                kiss_serial((uint8_t)raw[n]);
        }
    }
}
#endif

// Set your Static IP address for wifi AP
IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 254);
IPAddress subnet(255, 255, 255, 0);

IPAddress vpn_IP(192, 168, 44, 195);

int pkgTNC_count = 0;

xQueueHandle pcnt_evt_queue;              // A queue to handle pulse counter events
pcnt_isr_handle_t user_isr_handle = NULL; // user's ISR service handle

/* A sample structure to pass events from the PCNT
 * interrupt handler to the main program.
 */
typedef struct
{
    int unit;                // the PCNT unit that originated an interrupt
    uint32_t status;         // information on the event type that caused the interrupt
    unsigned long timeStamp; // The time the event occured
} pcnt_evt_t;

/* Decode what PCNT's unit originated an interrupt
 * and pass this information together with the event type
 * and timestamp to the main program using a queue.
 */
static void pcnt_intr_handler(void *arg)
{
    unsigned long currentMillis = millis(); // Tiempo actual
    uint32_t intr_status;
    int i;
    pcnt_evt_t evt;
    BaseType_t HPTaskAwoken = pdFALSE;

    // Obtener el estado de las interrupciones
    pcnt_get_event_status(PCNT_UNIT_0, &intr_status);

    for (i = 0; i < PCNT_UNIT_MAX; i++) // Iterar sobre todas las unidades PCNT
    {
        if (intr_status & (BIT(i)))
        {
            evt.unit = i;

            // Obtener el estado del evento
            pcnt_get_event_status((pcnt_unit_t)i, &evt.status);

            evt.timeStamp = currentMillis;

            // Limpiar la interrupción para esta unidad
            pcnt_intr_disable((pcnt_unit_t)i);
            pcnt_intr_enable((pcnt_unit_t)i);

            // Enviar el evento a la cola para el manejo en la tarea principal
            xQueueSendFromISR(pcnt_evt_queue, &evt, &HPTaskAwoken);

            if (HPTaskAwoken == pdTRUE)
            {
                portYIELD_FROM_ISR();
            }
        }
    }
}

/* Initialize PCNT functions for one channel:
 *  - configure and initialize PCNT with pos-edge counting
 *  - set up the input filter
 *  - set up the counter events to watch
 * Variables:
 * UNIT - Pulse Counter #, INPUT_SIG - Signal Input Pin, INPUT_CTRL - Control Input Pin,
 * Channel - Unit input channel, H_LIM - High Limit, L_LIM - Low Limit,
 * THRESH1 - configurable limit 1, THRESH0 - configurable limit 2,
 */
void pcnt_init_channel(pcnt_unit_t PCNT_UNIT, int PCNT_INPUT_SIG_IO, bool ACTIVE = false, int PCNT_INPUT_CTRL_IO = PCNT_PIN_NOT_USED, pcnt_channel_t PCNT_CHANNEL = PCNT_CHANNEL_0, int PCNT_H_LIM_VAL = 65535, int PCNT_L_LIM_VAL = 0, int PCNT_THRESH1_VAL = 50, int PCNT_THRESH0_VAL = -50)
{
    /* Prepare configuration for the PCNT unit */
    pcnt_config_t pcnt_config;
    // Set PCNT input signal and control GPIOs
    pcnt_config.pulse_gpio_num = PCNT_INPUT_SIG_IO;
    pcnt_config.ctrl_gpio_num = PCNT_INPUT_CTRL_IO;
    pcnt_config.channel = PCNT_CHANNEL;
    pcnt_config.unit = PCNT_UNIT;
    // What to do on the positive / negative edge of pulse input?
    if (ACTIVE)
    {
        pcnt_config.pos_mode = PCNT_COUNT_INC; // Count up on the positive edge
        pcnt_config.neg_mode = PCNT_COUNT_DIS; // Keep the counter value on the negative edge
    }
    else
    {
        pcnt_config.neg_mode = PCNT_COUNT_INC; // Count up on the positive edge
        pcnt_config.pos_mode = PCNT_COUNT_DIS; // Keep the counter value on the negative edge
    }
    // What to do when control input is low or high?
    pcnt_config.lctrl_mode = PCNT_MODE_REVERSE; // Reverse counting direction if low
    pcnt_config.hctrl_mode = PCNT_MODE_KEEP;    // Keep the primary counter mode if high
    // Set the maximum and minimum limit values to watch
    pcnt_config.counter_h_lim = PCNT_H_LIM_VAL;
    pcnt_config.counter_l_lim = PCNT_L_LIM_VAL;

    /* Initialize PCNT unit */
    pcnt_unit_config(&pcnt_config);
    /* Configure and enable the input filter */
    // pcnt_set_filter_value(PCNT_UNIT, 100);
    // pcnt_filter_enable(PCNT_UNIT);

    /* Set threshold 0 and 1 values and enable events to watch */
    // pcnt_set_event_value(PCNT_UNIT, PCNT_EVT_THRES_1, PCNT_THRESH1_VAL);
    // pcnt_event_enable(PCNT_UNIT, PCNT_EVT_THRES_1);
    // pcnt_set_event_value(PCNT_UNIT, PCNT_EVT_THRES_0, PCNT_THRESH0_VAL);
    // pcnt_event_enable(PCNT_UNIT, PCNT_EVT_THRES_0);
    /* Enable events on zero, maximum and minimum limit values */
    pcnt_event_enable(PCNT_UNIT, PCNT_EVT_ZERO);
    pcnt_event_enable(PCNT_UNIT, PCNT_EVT_H_LIM);

    /* Initialize PCNT's counter */
    pcnt_counter_pause(PCNT_UNIT);
    pcnt_counter_clear(PCNT_UNIT);
    /* Register ISR handler and enable interrupts for PCNT unit */
    // pcnt_isr_register(pcnt_intr_handler, NULL, 0, &user_isr_handle);
    // pcnt_intr_enable(PCNT_UNIT);

    /* Everything is set up, now go to counting */
    pcnt_counter_resume(PCNT_UNIT);
    // pcnt_counter_resume(PCNT_UNIT_1);
}

/* Count RPM Function - takes first timestamp and last timestamp,
number of pulses, and pulses per revolution */
int countRPM(int firstTime, int lastTime, int pulseTotal, int pulsePerRev)
{
    int timeDelta = (lastTime - firstTime); // lastTime - firstTime
    if (timeDelta <= 0)
    { // This means we've gotten something wrong
        return -1;
    }
    return ((60000 * (pulseTotal / pulsePerRev)) / timeDelta);
}

char EVENT_TX_POSITION = 0;
unsigned char SB_SPEED = 0, SB_SPEED_OLD = 0;
int16_t SB_HEADING = 0;
uint16_t tx_interval = 0, igate_tx_interval, digi_tx_interval; // How often we transmit, in seconds
unsigned int tx_counter, igate_tx_counter, digi_tx_counter;    // Incremented every second
int16_t last_heading, heading_change;                          // Keep track of corner pegging
uint16_t trk_interval = 15;

void smartbeacon(void)
{
    // Adaptive beacon rate
    if (SB_SPEED >= config.trk_hspeed)
    {
        tx_interval = (uint16_t)config.trk_maxinterval;
    }
    else
    {
        if (SB_SPEED <= config.trk_lspeed)
        {
            // Speed is below lower limit - use minimum rate
            tx_interval = (uint16_t)config.trk_slowinterval;
        }
        else
        {
            // In between, do SmartBeaconing calculations
            if (SB_SPEED <= 0)
                SB_SPEED = 1;
            tx_interval = (trk_interval * config.trk_hspeed) / SB_SPEED;
            if (tx_interval < 5)
                tx_interval = 5;
            if (tx_interval > config.trk_maxinterval)
                tx_interval = (uint16_t)config.trk_maxinterval;

            // Corner pegging - note that we use two-degree units
            if (last_heading > SB_HEADING)
                heading_change = last_heading - SB_HEADING;
            else
                heading_change = SB_HEADING - last_heading;
            if (heading_change > 90)
                heading_change = 180 - heading_change;
            if (heading_change > config.trk_minangle)
            {
                // Make sure last heading is updated during turns to avoid immedate xmit
                if ((tx_counter > config.trk_mininterval) && (SB_SPEED > 1))
                {
                    EVENT_TX_POSITION = 3;
                }
                else
                {
                    // last_heading = SB_HEADING;
                    tx_interval = config.trk_mininterval;
                }
            }
        }
    }
}

// (2^31 / 180) / 380926 semicircles per Base 91 unit
static unsigned long b91val[4] = {23601572L, 259358L, 2851L, 32L};
// Constants for converting lat/lon to semicircles
static long valtable[] = {1193046471L, 119304647L, 11930465L, 1988411L, 198841L, 19884L, 1988L, 199L, 20L, 2L};

// Takes degrees and fractional minutes and returns 2^31/180 degrees
// West and South values are negative
long semicircles(char *degstr, char hemi)
{
    char dgt = 0, *p;
    long ltemp = 0;
    if (degstr[4] == '.')
        dgt++; // Skip hundreds place if we know we're doing latitude
    p = degstr;
    for (; dgt < 10; p++)
    {
        if (*p == '.')
            continue;
        if (!isdigit(*p))
            break;
        ltemp += (*p & 0x0f) * valtable[dgt];
        dgt++;
    }
    if (hemi)
        return -ltemp;
    return ltemp;
}

// Converts semicircles to Base 91 units (must be properly biased first)
// Non-reentrant use of ltemp, but we only call this function in one place
void base91encode(long ltemp, char *s)
{
    // (2^31 / 180) / 380926 semicircles per Base 91 unit
    unsigned char c;

    for (c = 0; c < 4; c++)
    {
        s[c] = '!';
        while (ltemp >= b91val[c])
        {
            // reset_watchdog;
            ltemp -= b91val[c];
            s[c]++;
        }
    }
}

/// Degrees to radians.
#define DEG2RAD(x) (x / 360 * 2 * PI)
/// Radians to degrees.
#define RAD2DEG(x) (x * (180 / PI))

double direction(double lon0, double lat0, double lon1, double lat1)
{
    double direction;

    /* Convert degrees into radians. */
    lon0 = DEG2RAD(lon0);
    lat0 = DEG2RAD(lat0);
    lon1 = DEG2RAD(lon1);
    lat1 = DEG2RAD(lat1);

    /* Direction from Aviation Formulary V1.42 by Ed Williams by way of
     * http://mathforum.org/library/drmath/view/55417.html */
    direction = atan2(sin(lon1 - lon0) * cos(lat1), cos(lat0) * sin(lat1) - sin(lat0) * cos(lat1) * cos(lon1 - lon0));
    if (direction < 0)
    {
        /* Make direction positive. */
        direction += 2 * PI;
    }

    return RAD2DEG(direction);
}

double distance(double lon0, double lat0, double lon1, double lat1)
{
    double dlon;
    double dlat;
    double a, c;
    /* Convert degrees into radians. */
    lon0 = DEG2RAD(lon0);
    lat0 = DEG2RAD(lat0);
    lon1 = DEG2RAD(lon1);
    lat1 = DEG2RAD(lat1);

    /* Use the haversine formula for distance calculation
     * http://mathforum.org/library/drmath/view/51879.html */
    dlon = lon1 - lon0;
    dlat = lat1 - lat0;
    a = pow(sin(dlat / 2), 2) + cos(lat0) * cos(lat1) * pow(sin(dlon / 2), 2);
    c = 2 * atan2(sqrt(a), sqrt(1 - a));

    return c * 6366.71; /* in kilometers */
}

String deg2lat(double deg)
{
    char sign;
    if (deg > 0.0F)
    {
        sign = 'N';
    }
    else
    {
        sign = 'S';
        deg *= -1;
    }

    uint id = (uint)floor(deg);
    uint im = (uint)((deg - (double)id) * 60);
    uint imm = (uint)round((((deg - (double)id) * 60) - (double)im) * 100);
    char dmm[10];
    sprintf(dmm, "%02d%02d.%02d%c", id, im, imm, sign);
    return String(dmm);
}

String deg2lon(double deg)
{
    char sign;
    if (deg > 0.0F)
    {
        sign = 'E';
    }
    else
    {
        sign = 'W';
        deg *= -1;
    }
    uint id = (uint)floor(deg);
    uint im = (uint)((deg - (double)id) * 60);
    uint imm = (uint)round((((deg - (double)id) * 60) - (double)im) * 100);
    char dmm[10];
    sprintf(dmm, "%03d%02d.%02d%c", id, im, imm, sign);
    return String(dmm);
}

time_t setGpsTime()
{
    time_t time;
    tmElements_t timeinfo;
    if (gps.time.isValid())
    {
        timeinfo.Year = (gps.date.year()) - 1970;
        timeinfo.Month = gps.date.month();
        timeinfo.Day = gps.date.day();
        timeinfo.Hour = gps.time.hour();
        timeinfo.Minute = gps.time.minute();
        timeinfo.Second = gps.time.second();
        time_t timeStamp = makeTime(timeinfo);
        time = timeStamp + config.timeZone * SECS_PER_HOUR;
        setTime(time);
        // setTime(timeinfo.Hour,timeinfo.Minute,timeinfo.Second,timeinfo.Day, timeinfo.Month, timeinfo.Year);
        return time;
    }
    return 0;
}

time_t getGpsTime()
{
    time_t time;
    tmElements_t timeinfo;
    if (gps.time.isValid())
    {
        timeinfo.Year = (gps.date.year()) - 1970;
        timeinfo.Month = gps.date.month();
        timeinfo.Day = gps.date.day();
        timeinfo.Hour = gps.time.hour();
        timeinfo.Minute = gps.time.minute();
        timeinfo.Second = gps.time.second();
        time_t timeStamp = makeTime(timeinfo);
        time = timeStamp + config.timeZone * SECS_PER_HOUR;
        return time;
    }
    return 0;
}

String getValue(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = {0, -1};
    int maxIndex = data.length();

    for (int i = 0; i <= maxIndex && found <= index; i++)
    {
        if (data.charAt(i) == separator || i == maxIndex)
        {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
} // END

boolean isValidNumber(String str)
{
    for (byte i = 0; i < str.length(); i++)
    {
        if (isDigit(str.charAt(i)))
            return true;
    }
    return false;
}

uint8_t checkSum(uint8_t *ptr, size_t count)
{
    uint8_t lrc, tmp;
    uint16_t i;
    lrc = 0;
    for (i = 0; i < count; i++)
    {
        tmp = *ptr++;
        lrc = lrc ^ tmp;
    }
    return lrc;
}

void saveEEPROM()
{
    uint8_t chkSum = 0;
    byte *ptr;
    ptr = (byte *)&config;
    EEPROM.writeBytes(1, ptr, sizeof(Configuration));
    chkSum = checkSum(ptr, sizeof(Configuration));
    EEPROM.write(0, chkSum);
    EEPROM.commit();
#ifdef DEBUG
    Serial.print("Save EEPROM ChkSUM=");
    Serial.println(chkSum, HEX);
#endif
}

void defaultConfig()
{
    log_d("Default configure mode!");
    config.synctime = true;
    config.timeZone = 7;
    config.tx_timeslot = 2000; // ms

    config.wifi_mode = WIFI_AP_STA_FIX;
    // config.wifi_power = 78; // WIFI_POWER_11dBm
    config.wifi_power = 84; // WIFI_POWER_15dBm
    config.wifi_ap_ch = 6;
    config.wifi_sta[0].enable = true;
    sprintf(config.wifi_sta[0].wifi_ssid, "IGATE");
    sprintf(config.wifi_sta[0].wifi_pass, "aprsesp32");
    for (int i = 1; i < 5; i++)
    {
        config.wifi_sta[i].enable = false;
        config.wifi_sta[i].wifi_ssid[0] = 0;
        config.wifi_sta[i].wifi_pass[0] = 0;
    }
    sprintf(config.wifi_ap_ssid, "ESP32IGate");
    sprintf(config.wifi_ap_pass, "aprsesp32");

    //--RF Module
    config.rf_en = true;
    config.rf_type = RF_SA868_VHF;
    config.freq_rx = 144.3900;
    config.freq_tx = 144.3900;
    config.offset_rx = 0;
    config.offset_tx = 0;
    config.tone_rx = 0;
    config.tone_tx = 0;
    config.band = 0;
    config.sql_level = 1;
    config.rf_power = LOW;
    config.volume = 6;
    config.mic = 8;

    // IGATE
    config.igate_bcn = false;
    config.igate_en = false;
    config.rf2inet = true;
    config.inet2rf = false;
    config.igate_loc2rf = false;
    config.igate_loc2inet = true;
    config.rf2inetFilter = 0xFFF; // All
    config.digiFilter = FILTER_OBJECT | FILTER_ITEM | FILTER_MESSAGE | FILTER_MICE | FILTER_POSITION | FILTER_WX;
    config.inet2rfFilter = FILTER_MESSAGE;
    //--APRS-IS
    config.aprs_ssid = 1;
    config.aprs_port = 14580;
    sprintf(config.aprs_mycall, "NOCALL");
    sprintf(config.aprs_host, "rotate.aprs2.net");
    memset(config.aprs_passcode, 0, sizeof(config.aprs_passcode));
    sprintf(config.aprs_moniCall, "%s-%d", config.aprs_mycall, config.aprs_ssid);
    sprintf(config.aprs_filter, "m/10");
    //--Position
    config.igate_gps = false;
    config.igate_lat = 13.7555;
    config.igate_lon = 100.4930;
    config.igate_alt = 0;
    config.igate_interval = 600;
    sprintf(config.igate_symbol, "/&");
    memset(config.igate_object, 0, sizeof(config.igate_object));
    memset(config.igate_phg, 0, sizeof(config.igate_phg));
    config.igate_path = 8;
    sprintf(config.igate_comment, "ESP32 IGate MODE");

    // DIGI REPEATER
    config.digi_en = false;
    config.digi_loc2rf = true;
    config.digi_loc2inet = false;
    config.digi_ssid = 3;
    config.digi_timestamp = false;
    sprintf(config.digi_mycall, "NOCALL");
    config.digi_path = 8;
    //--Position
    config.digi_gps = false;
    config.digi_lat = 13.7555;
    config.digi_lon = 100.4930;
    config.digi_alt = 0;
    config.digi_interval = 600;
    config.igate_timestamp = false;
    config.digi_delay = 0;
    config.digiFilter = FILTER_OBJECT | FILTER_ITEM | FILTER_MESSAGE | FILTER_MICE | FILTER_POSITION | FILTER_WX;

    sprintf(config.digi_symbol, "/#");
    memset(config.digi_phg, 0, sizeof(config.digi_phg));
    sprintf(config.digi_comment, "DIGI MODE");

    // Tracker
    config.trk_en = false;
    config.trk_loc2rf = true;
    config.trk_loc2inet = false;
    config.trk_bat = false;
    config.trk_sat = false;
    config.trk_dx = false;
    config.trk_ssid = 7;
    config.trk_timestamp = false;
    sprintf(config.trk_mycall, "NOCALL");
    config.trk_path = 2;

    //--Position
    config.trk_gps = false;
    config.trk_lat = 13.7555;
    config.trk_lon = 100.4930;
    config.trk_alt = 0;
    config.trk_interval = 600;
    // Smart beacon
    config.trk_smartbeacon = true;
    config.trk_compress = true;
    config.trk_altitude = true;
    config.trk_cst = true;
    config.trk_hspeed = 120;
    config.trk_lspeed = 5;
    config.trk_maxinterval = 30;
    config.trk_mininterval = 5;
    config.trk_minangle = 25;
    config.trk_slowinterval = 600;

    sprintf(config.trk_symbol, "/[");
    sprintf(config.trk_symmove, "/>");
    sprintf(config.trk_symstop, "\\>");
    // sprintf(config.trk_btext, "");
    sprintf(config.trk_mycall, "NOCALL");
    sprintf(config.trk_comment, "TRACKER MODE");
    memset(config.trk_item, 0, sizeof(config.trk_item));

    // WX
    config.wx_en = false;
    config.wx_2rf = true;
    config.wx_2inet = true;
    config.wx_channel = 0;
    config.wx_ssid = 13;
    sprintf(config.wx_mycall, "NOCALL");
    config.wx_path = 8;
    sprintf(config.wx_comment, "WX MODE");
    memset(config.wx_object, 0, sizeof(config.wx_object));

    // Telemetry_0
    config.tlm0_en = false;
    config.tlm0_2rf = true;
    config.tlm0_2inet = true;
    config.tlm0_ssid = 0;
    sprintf(config.tlm0_mycall, "NOCALL");
    config.tlm0_path = 0;
    config.tlm0_data_interval = 600;
    config.tlm0_info_interval = 3600;
    sprintf(config.tlm0_PARM[0], "RF->INET");
    sprintf(config.tlm0_PARM[1], "INET->RF");
    sprintf(config.tlm0_PARM[2], "Repeater");
    sprintf(config.tlm0_PARM[3], "AllCount");
    sprintf(config.tlm0_PARM[4], "AllDrop");
    sprintf(config.tlm0_PARM[5], "IGATE");
    sprintf(config.tlm0_PARM[6], "DIGI");
    sprintf(config.tlm0_PARM[7], "WX");
    sprintf(config.tlm0_PARM[8], "SAT");
    sprintf(config.tlm0_PARM[9], "INET");
    sprintf(config.tlm0_PARM[10], "VPN");
    sprintf(config.tlm0_PARM[11], "4G");
    sprintf(config.tlm0_PARM[12], "MQTT");

    for (int i = 0; i < 5; i++)
    {
        sprintf(config.tlm0_UNIT[i], "Pkts");
        config.tlm0_EQNS[i][0] = 0; // a av2 + bv + c
        config.tlm0_EQNS[i][1] = 1; // b
        config.tlm0_EQNS[i][2] = 0; // c
    }
    sprintf(config.tlm0_UNIT[5], "En");
    sprintf(config.tlm0_UNIT[6], "En");
    sprintf(config.tlm0_UNIT[7], "En");
    sprintf(config.tlm0_UNIT[8], "En");
    sprintf(config.tlm0_UNIT[9], "ON");
    sprintf(config.tlm0_UNIT[10], "ON");
    sprintf(config.tlm0_UNIT[11], "ON");
    sprintf(config.tlm0_UNIT[12], "ON");
    config.tlm0_BITS_Active = 0xFF;
    config.tml0_data_channel[0] = 2;
    config.tml0_data_channel[1] = 3;
    config.tml0_data_channel[2] = 4;
    config.tml0_data_channel[3] = 1;
    config.tml0_data_channel[4] = 5;
    config.tml0_data_channel[5] = 1;
    config.tml0_data_channel[6] = 2;
    config.tml0_data_channel[7] = 3;
    config.tml0_data_channel[8] = 4;
    config.tml0_data_channel[9] = 5;
    config.tml0_data_channel[10] = 6;
    config.tml0_data_channel[11] = 7;
    config.tml0_data_channel[12] = 8;
    sprintf(config.tlm0_comment, "SYSTEM STATUS");

    //--Position
    config.wx_gps = false;
    config.wx_lat = 13.7555;
    config.wx_lon = 100.4930;
    config.wx_alt = 0;
    config.wx_interval = 600;
    config.wx_flage = 0;
    memset(config.wx_type, 0, sizeof(config.wx_type));

    // OLED DISPLAY
    config.oled_enable = true;
    config.oled_timeout = 60;
    config.dim = 0;
    config.contrast = 0;
    config.startup = 0;

    // Display
    config.dispDelay = 3; // Popup display 3 sec
    config.dispRF = true;
    config.dispINET = false;
    config.filterDistant = 0;
    config.dispFilter = 0xFFFF; // All filter
    config.h_up = false;
    config.tx_display = true;
    config.rx_display = true;

    // afsk,TNC
    config.modem_type = 1; // 0=AFSK300,1=AFSK1200
    config.audio_hpf = false;
    config.audio_bpf = false;
    config.preamble = 3;
    sprintf(config.ntp_host, "pool.ntp.org");

    sprintf(config.path[0], "WIDE1-1");
    sprintf(config.path[1], "WIDE1-1,WIDE2-1");
    sprintf(config.path[2], "TRACK3-3");
    sprintf(config.path[3], "RS0ISS");

    sprintf(config.http_username, "admin");
    sprintf(config.http_password, "admin");

    // config.gnss_baudrate = 9600;
    config.gnss_enable = false;
    // config.gnss_tx_gpio = 19;
    // config.gnss_rx_gpio = 18;
    // config.gnss_pps_gpio = -1;
    config.gnss_tcp_port = 8080;
    sprintf(config.gnss_tcp_host, "192.168.0.1");
    memset(config.gnss_at_command, 0, sizeof(config.gnss_at_command));

#ifdef CORE_DEBUG_LEVEL
    config.uart0_enable = true;
    config.uart0_baudrate = 921600;
    config.uart0_rx_gpio = 3;
    config.uart0_tx_gpio = 1;
    config.uart0_rts_gpio = -1;
#else
    config.uart0_enable = false;
    config.uart0_baudrate = 9600;
    config.uart0_rx_gpio = 3;
    config.uart0_tx_gpio = 1;
    config.uart0_rts_gpio = -1;
#endif

    config.uart1_enable = false;
    config.uart1_baudrate = 9600;
    config.uart1_rx_gpio = 18;
    config.uart1_tx_gpio = 19;
    config.uart1_rts_gpio = -1;

    config.uart2_enable = false;
    config.uart2_baudrate = 9600;
    config.uart2_rx_gpio = 16;
    config.uart2_tx_gpio = 17;
    config.uart2_rts_gpio = -1;

    config.modbus_enable = false;
    config.modbus_de_gpio = -1;
    config.modbus_address = 0;
    config.modbus_channel = 0;

    config.onewire_enable = false;
    config.onewire_gpio = -1;
    
#ifdef RFMODULE
    config.rf_en = true;
    config.rf_type = RF_SA868_VHF;
    config.rf_tx_gpio = 13;
    config.rf_rx_gpio = 14;
    config.rf_sql_gpio = 33;
    config.rf_pd_gpio = 27;
    config.rf_pwr_gpio = 12;
    config.rf_ptt_gpio = 32;
    config.rf_sql_active = 0;
    config.rf_pd_active = 1;
    config.rf_pwr_active = 1;
    config.rf_ptt_active = 1;
#else
    config.rf_en = false;
    config.rf_type = RF_NONE;
    config.rf_tx_gpio = -1;
    config.rf_rx_gpio = -1;
    config.rf_sql_gpio = -1;
    config.rf_pd_gpio = -1;
    config.rf_pwr_gpio = -1;
    config.rf_ptt_gpio = 32;
    config.rf_sql_active = 1;
    config.rf_pd_active = 1;
    config.rf_pwr_active = 1;
    config.rf_ptt_active = 1;
#endif    
    config.adc_atten = 0;
    config.adc_dc_offset = 625;
    config.rf_baudrate = 9600;

#ifdef OLED
    config.i2c_enable = true;
    config.i2c_sda_pin = 21;
    config.i2c_sck_pin = 22;
#else
    config.i2c_enable = false;
    config.i2c_sda_pin = -1;
    config.i2c_sck_pin = -1;
#endif
    config.i2c_freq = 400000;
    config.i2c1_enable = false;
    config.i2c1_sda_pin = -1;
    config.i2c1_sck_pin = -1;
    config.i2c1_freq = 100000;

    config.counter0_enable = false;
    config.counter0_active = 0;
    config.counter0_gpio = -1;

    config.counter1_enable = false;
    config.counter1_active = 0;
    config.counter1_gpio = -1;

    config.ext_tnc_enable = false;
    config.ext_tnc_channel = 0;
    config.ext_tnc_mode = 2;

    sprintf(config.path[0], "TRACE2-2");
    sprintf(config.path[1], "WIDE1-1");
    sprintf(config.path[2], "WIDE1-1,WIDE2-1");
    sprintf(config.path[3], "RFONLY");

    saveEEPROM();
}

unsigned long NTP_Timeout;
unsigned long pingTimeout;

bool psramBusy = false;

const char *lastTitle = "LAST HEARD";

int tlmList_Find(char *call)
{
    int i;
    for (i = 0; i < TLMLISTSIZE; i++)
    {
        if (strstr(Telemetry[i].callsign, call) != NULL)
            return i;
    }
    return -1;
}

int tlmListOld()
{
    int i, ret = 0;
    time_t minimum = Telemetry[0].time;
    for (i = 1; i < TLMLISTSIZE; i++)
    {
        if (Telemetry[i].time < minimum)
        {
            minimum = Telemetry[i].time;
            ret = i;
        }
        if (Telemetry[i].time > now())
            Telemetry[i].time = 0;
    }
    return ret;
}

int pkgList_Find(char *call)
{
    int i;
    for (i = 0; i < PKGLISTSIZE; i++)
    {
        if (strstr(pkgList[(int)i].calsign, call) != NULL)
            return i;
    }
    return -1;
}

int pkgList_Find(char *call, uint16_t type)
{
    int i;
    for (i = 0; i < PKGLISTSIZE; i++)
    {
        if ((strstr(pkgList[i].calsign, call) != NULL) && (pkgList[i].type == type))
            return i;
    }
    return -1;
}

int pkgListOld()
{
    int i, ret = -1;
    time_t minimum = time(NULL) + 86400; // pkgList[0].time;
    for (i = 0; i < PKGLISTSIZE; i++)
    {
        if (pkgList[(int)i].time < minimum)
        {
            minimum = pkgList[(int)i].time;
            ret = i;
        }
    }
    return ret;
}

void sort(pkgListType a[], int size)
{
    pkgListType t;
    char *ptr1;
    char *ptr2;
    char *ptr3;
    ptr1 = (char *)&t;
#ifdef BOARD_HAS_PSRAM
    while (psramBusy)
        delay(1);
    psramBusy = true;
#endif
    for (int i = 0; i < (size - 1); i++)
    {
        for (int o = 0; o < (size - (i + 1)); o++)
        {
            if (a[o].time < a[o + 1].time)
            {
                ptr2 = (char *)&a[o];
                ptr3 = (char *)&a[o + 1];
                memcpy(ptr1, ptr2, sizeof(pkgListType));
                memcpy(ptr2, ptr3, sizeof(pkgListType));
                memcpy(ptr3, ptr1, sizeof(pkgListType));
            }
        }
    }
    psramBusy = false;
}

void sortPkgDesc(pkgListType a[], int size)
{
    pkgListType t;
    char *ptr1;
    char *ptr2;
    char *ptr3;
    ptr1 = (char *)&t;
#ifdef BOARD_HAS_PSRAM
    while (psramBusy)
        delay(1);
    psramBusy = true;
#endif
    for (int i = 0; i < (size - 1); i++)
    {
        for (int o = 0; o < (size - (i + 1)); o++)
        {
            if (a[o].pkg < a[o + 1].pkg)
            {
                ptr2 = (char *)&a[o];
                ptr3 = (char *)&a[o + 1];
                memcpy(ptr1, ptr2, sizeof(pkgListType));
                memcpy(ptr2, ptr3, sizeof(pkgListType));
                memcpy(ptr3, ptr1, sizeof(pkgListType));
            }
        }
    }
    psramBusy = false;
}

uint16_t pkgType(const char *raw)
{
    uint16_t type = 0;
    char packettype = 0;
    const char *body;
    //int paclen = strlen(raw);
    char *ptr;

    if (*raw == 0)
        return 0;

    packettype = (char)raw[0];
    body = &raw[1];

    switch (packettype)
    {
    case '$': // NMEA
        type |= FILTER_POSITION;
        break;
    case 0x27: /* ' */
    case 0x60: /* ` */
        type |= FILTER_POSITION;
        type |= FILTER_MICE;
        break;
    case '!':
    case '=':
        type |= FILTER_POSITION;
        if (body[18] == '_' || body[10] == '_')
        {
            type |= FILTER_WX;
            break;
        }
    case '/':
    case '@':
        type |= FILTER_POSITION;
        if (body[25] == '_' || body[16] == '_')
        {
            type |= FILTER_WX;
            break;
        }
        if (strchr(body, 'r') != NULL)
        {
            if (strchr(body, 'g') != NULL)
            {
                if (strchr(body, 't') != NULL)
                {
                    if (strchr(body, 'P') != NULL)
                    {
                        type |= FILTER_WX;
                    }
                }
            }
        }
        break;
    case ':':
        if (body[9] == ':' &&
            (memcmp(body + 10, "PARM", 4) == 0 ||
             memcmp(body + 10, "UNIT", 4) == 0 ||
             memcmp(body + 10, "EQNS", 4) == 0 ||
             memcmp(body + 10, "BITS", 4) == 0))
        {
            type |= FILTER_TELEMETRY;
        }
        else
        {
            type |= FILTER_MESSAGE;
        }
        break;
    case '{': // User defind
    case '<': // statcapa
    case '>':
        type |= FILTER_STATUS;
        break;
    case '?':
        type |= FILTER_QUERY;
        break;
    case ';':
        if (body[28] == '_')
            type |= FILTER_WX;
        else
            type |= FILTER_OBJECT;
        break;
    case ')':
        type |= FILTER_ITEM;
        break;
    case '}':
        type |= FILTER_THIRDPARTY;
        ptr = strchr(raw, ':');
        if (ptr != NULL)
        {
            ptr++;
            type |= pkgType(ptr);
        }
        break;
    case 'T':
        type |= FILTER_TELEMETRY;
        break;
    case '#': /* Peet Bros U-II Weather Station */
    case '*': /* Peet Bros U-I  Weather Station */
    case '_': /* Weather report without position */
        type |= FILTER_WX;
        break;
    default:
        type = 0;
        break;
    }
    return type;
}

// uint16_t TNC2Raw[PKGLISTSIZE];
// int raw_count = 0, raw_idx_rd = 0, raw_idx_rw = 0;

// int pushTNC2Raw(int raw)
// {
//   if (raw < 0)
//     return -1;
//   if (raw_count > PKGLISTSIZE)
//     return -1;
//   if (++raw_idx_rw >= PKGLISTSIZE)
//     raw_idx_rw = 0;
//   TNC2Raw[raw_idx_rw] = raw;
//   raw_count++;
//   return raw_count;
// }

// int popTNC2Raw(int &ret)
// {
//   String raw = "";
//   int idx = 0;
//   if (raw_count <= 0)
//     return -1;
//   if (++raw_idx_rd >= PKGLISTSIZE)
//     raw_idx_rd = 0;
//   idx = TNC2Raw[raw_idx_rd];
//   if (idx < PKGLISTSIZE)
//     ret = idx;
//   if (raw_count > 0)
//     raw_count--;
//   return raw_count;
// }

pkgListType getPkgList(int idx)
{
    pkgListType ret;
#ifdef BOARD_HAS_PSRAM
    while (psramBusy)
        delay(1);
    psramBusy = true;
#endif
    memset(&ret, 0, sizeof(pkgListType));
    if (idx < PKGLISTSIZE)
        memcpy(&ret, &pkgList[idx], sizeof(pkgListType));
    psramBusy = false;
    return ret;
}

int pkgListUpdate(char *call, char *raw,size_t len, uint16_t type, bool channel)
{
    //size_t len;
    if (*call == 0)
        return -1;
    if (*raw == 0)
        return -1;

    char callsign[11];
    size_t sz = strlen(call);
    memset(callsign, 0, 11);
    if (sz > 10)
        sz = 10;
    // strncpy(callsign, call, sz);
    memcpy(callsign, call, sz);
#ifdef BOARD_HAS_PSRAM
    while (psramBusy)
        delay(1);
    psramBusy = true;
#endif
    int i = pkgList_Find(callsign, type);
    if (i > PKGLISTSIZE)
    {
        psramBusy = false;
        return -1;
    }
    if (i > -1)
    { // Found call in old pkg
        if ((channel == 1) || (channel == pkgList[i].channel))
        {
            pkgList[i].time = time(NULL);
            pkgList[i].pkg++;
            pkgList[i].type = type;
            if (channel == 0)
                pkgList[i].audio_level = (int16_t)mVrms;
            else
                pkgList[i].audio_level = 0;
            //len = strlen(raw);
            if (len > 300)
                len = 300;
            memset(pkgList[i].raw,0,300);
            memcpy(pkgList[i].raw, raw, len);
            log_d("Update: pkgList_idx=%d", i);
        }
    }
    else
    {
        i = pkgListOld(); // Search free in array
        if (i > PKGLISTSIZE || i < 0)
        {
            psramBusy = false;
            return -1;
        }
        // memset(&pkgList[i], 0, sizeof(pkgListType));
        pkgList[i].channel = channel;
        pkgList[i].time = time(NULL);
        pkgList[i].pkg = 1;
        pkgList[i].type = type;
        if (channel == 0)
            pkgList[i].audio_level = (int16_t)mVrms;
        else
            pkgList[i].audio_level = 0;
        // strcpy(pkgList[i].calsign, callsign);
        memset(pkgList[i].calsign,0,11);
        memcpy(pkgList[i].calsign, callsign, strlen(callsign));
        //len = strlen(raw);
        if (len > 300)
            len = 300;
        memset(pkgList[i].raw,0,300);
        memcpy(pkgList[i].raw, raw, len);
        // strcpy(pkgList[i].raw, raw);
        pkgList[i].calsign[10] = 0;
        log_d("New: pkgList_idx=%d", i);
        // SerialLOG.print("NEW: ");
    }
    psramBusy = false;
    event_lastHeard();
    return i;
}

bool pkgTxDuplicate(AX25Msg ax25)
{
#ifdef BOARD_HAS_PSRAM
    while (psramBusy)
        delay(1);
    psramBusy = true;
#endif
    char callsign[12];
    for (int i = 0; i < PKGTXSIZE; i++)
    {
        if (txQueue[i].Active)
        {
            if (ax25.src.ssid > 0)
                sprintf(callsign, "%s-%d", ax25.src.call, ax25.src.ssid);
            else
                sprintf(callsign, "%s", ax25.src.call);
            if (strncmp(&txQueue[i].Info[0], callsign, strlen(callsign)) >= 0) // Check duplicate src callsign
            {
                char *ecs1 = strstr(txQueue[i].Info, ":");
                if (ecs1 == NULL)
                    continue;
                ;
                if (strncmp(ecs1, (const char *)ax25.info, strlen(ecs1)) >= 0)
                { // Check duplicate aprs info
                    txQueue[i].Active = false;
                    psramBusy = false;
                    return true;
                }
            }
        }
    }

    psramBusy = false;
    return false;
}

bool pkgTxPush(const char *info, size_t len, int dly)
{
    char *ecs = strstr(info, ">");
    if (ecs == NULL)
        return false;
#ifdef BOARD_HAS_PSRAM
    while (psramBusy)
        delay(1);
    psramBusy = true;
#endif
    // for (int i = 0; i < PKGTXSIZE; i++)
    // {
    //   if (txQueue[i].Active)
    //   {
    //     if ((strncmp(&txQueue[i].Info[0], info, info - ecs)==0)) //Check src callsign
    //     {
    //       // strcpy(&txQueue[i].Info[0], info);
    //       memset(txQueue[i].Info, 0, sizeof(txQueue[i].Info));
    //       memcpy(&txQueue[i].Info[0], info, len);
    //       txQueue[i].Delay = dly;
    //       txQueue[i].timeStamp = millis();
    //       psramBusy = false;
    //       return true;
    //     }
    //   }
    // }

    // Add
    for (int i = 0; i < PKGTXSIZE; i++)
    {
        if (txQueue[i].Active == false)
        {
            memset(txQueue[i].Info, 0, sizeof(txQueue[i].Info));
            memcpy(&txQueue[i].Info[0], info, len);
            txQueue[i].Delay = dly;
            txQueue[i].Active = true;
            txQueue[i].timeStamp = millis();
            break;
        }
    }
    psramBusy = false;
    return true;
}

bool pkgTxSend()
{
//   if (getReceive())
//     return false;
#ifdef BOARD_HAS_PSRAM
    while (psramBusy)
        delay(1);
    psramBusy = true;
#endif

    for (int i = 0; i < PKGTXSIZE; i++)
    {
        if (txQueue[i].Active)
        {
            int decTime = millis() - txQueue[i].timeStamp;
            if (decTime > txQueue[i].Delay)
            {
                txQueue[i].Active = false;
                char *info = (char*)calloc(500,sizeof(char));
                if(info){
                    memset(info, 0, 500);
                    strcpy(info, txQueue[i].Info);
                    psramBusy = false;
                    if ((config.rf_type == RF_SR_1WV) || (config.rf_type == RF_SR_1WU) || (config.rf_type == RF_SR_1W350)){
                        digitalWrite(config.rf_pwr_gpio,LOW);
                        if(config.rf_power ^ !config.rf_pwr_active)
                            pinMode(config.rf_pwr_gpio,OPEN_DRAIN);
                        else
                            pinMode(config.rf_pwr_gpio,OUTPUT);
                    }else{
                        digitalWrite(config.rf_pwr_gpio, config.rf_power ^ !config.rf_pwr_active); // ON RF Power H/L
                    }
                    status.txCount++;

                    APRS_setPreamble(config.preamble * 100);
                    APRS_sendTNC2Pkt(String(info)); // Send packet to RF
                    txQueue[i].Active = false;
                    igateTLM.TX++;
                    log_d("TX->RF: %s\n", info);
                    free(info);
                    for (int i = 0; i < 100; i++)
                    {
                        // if (digitalRead(config.rf_ptt_gpio) ^ config.rf_ptt_active)
                        if (!getTransmit())
                            break;
                        delay(50); // TOT 5sec                        
                    }
                    digitalWrite(config.rf_pwr_gpio, !config.rf_pwr_active); // OFF RF Power H/L    
                    pinMode(config.rf_pwr_gpio,OUTPUT);            
                    return true;
                }
            }
        }
    }
    psramBusy = false;
    return false;
}

uint8_t *packetData;
// ฟังชั่นถูกเรียกมาจาก ax25_decode
//uint8_t swap=0;
void aprs_msg_callback(struct AX25Msg *msg)
{
    AX25Msg pkg;

    memcpy(&pkg, msg, sizeof(AX25Msg));
    int timeout=0;
    while(PacketBuffer.isFull()){
        delay(100);
        if(++timeout>10) return;
    }
    // if(++swap%3 == 0){
    // sprintf((char *)pkg.info,"`l-hl }>/`\"3q}144.937MHz Pirate Pilot on board!!!_%%");
    // pkg.len=52;
    // sprintf(pkg.src.call,"E25KFR");
    // pkg.src.ssid=9;
    // sprintf(pkg.dst.call,"QS4WU3");
    // pkg.dst.ssid=0;
    // pkg.rpt_count=1;
    // sprintf(pkg.rpt_list[0].call,"WIDE1");
    // pkg.rpt_list[0].ssid=1;
    // }

    PacketBuffer.push(&pkg); // ใส่แพ็จเก็จจาก TNC ลงคิวบัพเฟอร์
    status.rxCount++;
}

void printTime()
{
    struct tm tmstruct;
    getLocalTime(&tmstruct, 5000);
    Serial.print("[");
    Serial.print(tmstruct.tm_hour);
    Serial.print(":");
    Serial.print(tmstruct.tm_min);
    Serial.print(":");
    Serial.print(tmstruct.tm_sec);
    Serial.print("]");
}

uint8_t gwRaw[PKGLISTSIZE][66];
uint8_t gwRawSize[PKGLISTSIZE];
int gwRaw_count = 0, gwRaw_idx_rd = 0, gwRaw_idx_rw = 0;

void pushGwRaw(uint8_t *raw, uint8_t size)
{
    if (gwRaw_count > PKGLISTSIZE)
        return;
    if (++gwRaw_idx_rw >= PKGLISTSIZE)
        gwRaw_idx_rw = 0;
    if (size > 65)
        size = 65;
    memcpy(&gwRaw[gwRaw_idx_rw][0], raw, size);
    gwRawSize[gwRaw_idx_rw] = size;
    gwRaw_count++;
}

uint8_t popGwRaw(uint8_t *raw)
{
    uint8_t size = 0;
    if (gwRaw_count <= 0)
        return 0;
    if (++gwRaw_idx_rd >= PKGLISTSIZE)
        gwRaw_idx_rd = 0;
    size = gwRawSize[gwRaw_idx_rd];
    memcpy(raw, &gwRaw[gwRaw_idx_rd][0], size);
    if (gwRaw_count > 0)
        gwRaw_count--;
    return size;
}

bool SA868_waitResponse(String &data, String rsp, uint32_t timeout)
{
    uint32_t startMillis = millis();
    data.clear();
    do
    {
        while (SerialRF.available() > 0)
        {
            int8_t ch = SerialRF.read();
            data += static_cast<char>(ch);
            if (rsp.length() && data.endsWith(rsp))
            {
                return true;
            }
            esp_task_wdt_reset();
        }
        delay(1);
        esp_task_wdt_reset();
    } while (millis() - startMillis < timeout);
    return false;
}

int SA868_getRSSI()
{
    String data;
    int rssi;

    // Serial.printf("AT+RSSI?\r\n");
    SerialRF.printf("AT+RSSI?\r\n");
    if (SA868_waitResponse(data, "\r\n", 1000))
    {
        // Serial.println(INFO + data);
        String rssi = data.substring(data.indexOf("RSSI=") + strlen("RSSI="), data.indexOf("\r\n"));
        rssi = rssi.toInt();
        return rssi.toInt();
    }
    else
    {
        // timeout or error
        return 0;
    }
}

String SA868_getVERSION()
{
    String data;
    int rssi;

    SerialRF.printf("AT+VERSION\r\n");
    if (SA868_waitResponse(data, "\r\n", 1000))
    {
        String version = data.substring(0, data.indexOf("\r\n"));
        return version;
    }
    else
    {
        // timeout or error
        return "-";
    }
}

String FRS_getVERSION()
{
    String data;
    String version;

    SerialRF.printf("AT+DMOVERQ\r\n");
    if (SA868_waitResponse(data, "\r\n", 1000))
    {
        int st = data.indexOf("DMOVERQ:");
        if (st > 0)
        {
            version = data.substring(st + 8, data.indexOf("\r\n"));
        }
        else
        {
            version = "Not found";
        }
        return version;
    }
    else
    {
        // timeout or error
        return "-";
    }
}

unsigned long SA818_Timeout = 0;

void RF_MODULE_SLEEP()
{
    digitalWrite(config.rf_pwr_gpio, !config.rf_pwr_active);
    digitalWrite(config.rf_pd_gpio, LOW);
    // PMU.disableDC3();
}

String hexToString(String hex)
{ // for String to HEX conversion
    String text = "";
    for (int k = 0; k < hex.length(); k++)
    {
        if (k % 2 != 0)
        {
            char temp[3];
            sprintf(temp, "%c%c", hex[k - 1], hex[k]);
            int number = (int)strtol(temp, NULL, 16);
            text += char(number);
        }
    }
    return text;
}

String ctcssToHex(unsigned int decValue, int section)
{ // use to convert the CTCSS reading which RF module needed
    if (decValue == 7777)
    {
        return hexToString("FF");
    }
    String d1d0 = String(decValue);
    if (decValue < 1000)
    {
        d1d0 = "0" + d1d0;
    }
    if (section == 1)
    {
        return hexToString(d1d0.substring(2, 4));
    }
    else
    {
        return hexToString(d1d0.substring(0, 2));
    }
}

void RF_MODULE(bool boot)
{
    String data;
    if (config.rf_en == false)
    {
        RF_MODULE_SLEEP();
        return;
    }
    if (config.rf_type == RF_NONE)
        return;
    log_d("RF Module %s Init", RF_TYPE[config.rf_type]);
    //! DC3 Radio & Pixels VDD , Don't change
    // PMU.setDC3Voltage(3400);
    // PMU.disableDC3();
    pinMode(config.rf_pwr_gpio, OUTPUT);
    digitalWrite(config.rf_pwr_gpio, !config.rf_pwr_active); // RF POWER LOW

    pinMode(config.rf_ptt_gpio, OUTPUT);
    digitalWrite(config.rf_ptt_gpio, !config.rf_ptt_active); // PTT HIGH
    pinMode(config.rf_pd_gpio, OUTPUT);
    if (boot)
    {
        SerialRF.begin(config.rf_baudrate, SWSERIAL_8N1, config.rf_rx_gpio, config.rf_tx_gpio);
    }else{
        digitalWrite(config.rf_pd_gpio, !config.rf_pd_active); // PD LOW
        delay(500);
    }
    
    digitalWrite(config.rf_pd_gpio, config.rf_pd_active); // PD HIGH

    delay(1000);
    SerialRF.write("\r\n");

    char str[200];
    String rsp = "\r\n";
    if (config.sql_level > 8)
        config.sql_level = 8;
    
    esp_task_wdt_reset();
    if ((config.rf_type == RF_SR_1WV) || (config.rf_type == RF_SR_1WU) || (config.rf_type == RF_SR_1W350))
    {
        SerialRF.printf("AT+DMOCONNECT\r\n");
        if (SA868_waitResponse(data, rsp, 1000))
            log_d("%s", data.c_str());
        RF_VERSION = FRS_getVERSION();
        log_d("RF Module Version %s", RF_VERSION);
        sprintf(str, "AT+DMOSETGROUP=%01d,%0.4f,%0.4f,%d,%01d,%d,0", config.band, config.freq_tx + ((float)config.offset_tx / 1000000), config.freq_rx + ((float)config.offset_rx / 1000000), config.tone_rx, config.sql_level, config.tone_tx);
        SerialRF.println(str);
        log_d("Write to SR_FRS: %s", str);
        if (SA868_waitResponse(data, rsp, 1500))
            log_d("%s", data.c_str());
        // Module auto power save setting
        SerialRF.printf("AT+DMOAUTOPOWCONTR=1\r\n");
        if (SA868_waitResponse(data, rsp, 1500))
            log_d("%s", data.c_str());
        SerialRF.printf("AT+DMOSETVOX=0\r\n");
        if (SA868_waitResponse(data, rsp, 1500))
            log_d("%s", data.c_str());
        SerialRF.printf("AT+DMOSETMIC=6,0\r\n");
        if (SA868_waitResponse(data, rsp, 1500))
            log_d("%s", data.c_str());
        SerialRF.printf("AT+DMOSETVOLUME=%d\r\n", config.volume);
        if (SA868_waitResponse(data, rsp, 1500))
            log_d("%s", data.c_str());
    }
    else if ((config.rf_type == RF_SA868_VHF) || (config.rf_type == RF_SA868_UHF) || (config.rf_type == RF_SA868_350))
    {
        SerialRF.printf("AT+DMOCONNECT\r\n");
        if (SA868_waitResponse(data, rsp, 1000))
            log_d("%s", data.c_str());
        RF_VERSION = SA868_getVERSION();
        log_d("RF Module Version %s", RF_VERSION);
        sprintf(str, "AT+DMOSETGROUP=%01d,%0.4f,%0.4f,%04d,%01d,%04d\r\n", config.band, config.freq_tx, config.freq_rx, config.tone_tx, config.sql_level, config.tone_rx);
        SerialRF.print(str);
        log_d("Write to SA868: %s", str);
        if (SA868_waitResponse(data, rsp, 2000))
            log_d("%s", data.c_str());
        SerialRF.printf("AT+SETTAIL=0\r\n");
        if (SA868_waitResponse(data, rsp, 1000))
            log_d("%s", data.c_str());
        SerialRF.printf("AT+SETFILTER=1,1,1\r\n");
        if (SA868_waitResponse(data, rsp, 1000))
            log_d("%s", data.c_str());
        SerialRF.printf("AT+DMOSETVOLUME=%d\r\n", config.volume);
        if (SA868_waitResponse(data, rsp, 1000))
            log_d("%s", data.c_str());
    }
    else if ((config.rf_type == RF_SR_2WVS) || (config.rf_type == RF_SR_2WUS))
    {
        uint8_t flag = 0;  // Bit0:busy lock 0:OFF,1:ON Bit1:band 0:Wide,1:Narrow
        uint8_t flag1 = 0; // Bit0: Hi/Lo 0:2W,1:0.5W  Bit1:Middle 0:2W/0.5W 1:1W
        RF_VERSION = FRS_getVERSION();
        log_d("RF Module Version %s", RF_VERSION.c_str());

        String tone_rx, tone_tx;

        if (config.tone_rx > 0)
        {
            int idx = config.tone_rx;
            if (idx < sizeof(ctcss))
            {
                int tone = (int)(ctcss[config.tone_rx] * 10.0F);
                tone_rx = ctcssToHex(tone, 1);
                tone_rx += ctcssToHex(tone, 2);
                log_d("Tone RX[%d] rx=%0X %0X", tone, tone_rx.charAt(0), tone_rx.charAt(1));
            }
        }
        else
        {
            tone_rx = ctcssToHex(7777, 1);
            tone_rx += ctcssToHex(7777, 2);
        }

        if (config.tone_tx > 0)
        {
            int idx = config.tone_tx;
            if (idx < sizeof(ctcss))
            {
                int tone = (int)(ctcss[config.tone_tx] * 10.0F);
                tone_tx = ctcssToHex(tone, 1);
                tone_tx += ctcssToHex(tone, 2);
                log_d("Tone RX[%d] tx=%0X %0X", tone, tone_tx.charAt(0), tone_tx.charAt(1));
            }
        }
        else
        {
            tone_tx = ctcssToHex(7777, 1);
            tone_tx += ctcssToHex(7777, 2);
        }

        if (config.band == 0)
            flag |= 0x02;
        if (config.rf_power == 0)
            flag1 |= 0x02;
        sprintf(str, "AT+DMOGRP=%0.5f,%0.5f,%s,%s,%d,%d\r\n", config.freq_rx, config.freq_tx, tone_rx.c_str(), tone_tx.c_str(), flag, flag1);
        log_d("Write to SR_FRS_2W: %s", str);
        SerialRF.print(str);

        if (SA868_waitResponse(data, rsp, 1000))
            log_d("%s", data.c_str());
        SerialRF.printf("AT+DMOSAV=1\r\n");
        if (SA868_waitResponse(data, rsp, 1000))
            log_d("%s", data.c_str());
        SerialRF.printf("AT+DMOVOL=%d\r\n", config.volume);
        if (SA868_waitResponse(data, rsp, 1000))
            log_d("%s", data.c_str());
        SerialRF.printf("AT+DMOVOX=0\r\n");
        if (SA868_waitResponse(data, rsp, 1000))
            log_d("%s", data.c_str());
        SerialRF.printf("AT+DMOFUN=%d,5,1,0,0\r\n", config.sql_level);
        if (SA868_waitResponse(data, rsp, 1000))
            log_d("%s", data.c_str());
    }
    delay(1);
}

void RF_MODULE_CHECK()
{
    while (SerialRF.available() > 0)
        SerialRF.read();
    SerialRF.println("AT+DMOCONNECT");
    delay(100);
    if (SerialRF.available() > 0)
    {
        String ret = SerialRF.readString();
        if (ret.indexOf("DMOCONNECT") > 0)
        {
            SA818_Timeout = millis();
            // Serial.println(SerialRF.readString());
            log_d("RF Module %s Activate", RF_TYPE[config.rf_type]);
        }
    }
    else
    {
        log_d("RF Module %s sleep", RF_TYPE[config.rf_type]);
        digitalWrite(config.rf_pwr_gpio, !config.rf_pwr_active);
        digitalWrite(config.rf_pd_gpio, LOW);
        delay(500);
        RF_MODULE(true);
    }
}

WiFiClient aprsClient;

boolean APRSConnect()
{
    // Serial.println("Connect TCP Server");
    String login = "";
    int cnt = 0;
    uint8_t con = aprsClient.connected();
    // Serial.println(con);
    if (con <= 0)
    {
        if (!aprsClient.connect(config.aprs_host, config.aprs_port)) // เชื่อมต่อกับเซิร์ฟเวอร์ TCP
        {
            // Serial.print(".");
            delay(100);
            cnt++;
            if (cnt > 50) // วนร้องขอการเชื่อมต่อ 50 ครั้ง ถ้าไม่ได้ให้รีเทิร์นฟังค์ชั่นเป็น False
                return false;
        }
        // ขอเชื่อมต่อกับ aprsc
        if (strlen(config.igate_object) >= 3)
        {
            uint16_t passcode = aprsParse.passCode(config.igate_object);
            login = "user " + String(config.igate_object) + " pass " + String(passcode, DEC) + " vers ESP32IGate V" + String(VERSION) + String(VERSION_BUILD) + " filter " + String(config.aprs_filter);
        }
        else
        {
            uint16_t passcode = aprsParse.passCode(config.aprs_mycall);
            if (config.aprs_ssid == 0)
                login = "user " + String(config.aprs_mycall) + " pass " + String(passcode, DEC) + " vers ESP32IGate V" + String(VERSION) + String(VERSION_BUILD) + " filter " + String(config.aprs_filter);
            else
                login = "user " + String(config.aprs_mycall) + "-" + String(config.aprs_ssid) + " pass " + String(passcode, DEC) + " vers ESP32IGate V" + String(VERSION) + String(VERSION_BUILD) + " filter " + String(config.aprs_filter);
        }
        aprsClient.println(login);
        // Serial.println(login);
        // Serial.println("Success");
        delay(500);
    }
    return true;
}

long oledSleepTimeout = 0;
bool showDisp = false;
uint8_t curTab = 0;

void preTransmission()
{
    digitalWrite(config.modbus_de_gpio, 1);
}

void postTransmission()
{
    digitalWrite(config.modbus_de_gpio, 0);
}

// 3 seconds WDT
#define WDT_TIMEOUT 10

bool AFSKInitAct = false;
void setup()
{
    byte *ptr;
    //setCpuFrequencyMhz(160);
#ifdef BOARD_HAS_PSRAM
    pkgList = (pkgListType *)ps_malloc(sizeof(pkgListType) * PKGLISTSIZE);
    Telemetry = (TelemetryType *)malloc(sizeof(TelemetryType) * TLMLISTSIZE);
    txQueue = (txQueueType *)ps_malloc(sizeof(txQueueType) * PKGTXSIZE);
    // TNC2Raw = (int *)ps_malloc(sizeof(int) * PKGTXSIZE);
#else
    pkgList = (pkgListType *)malloc(sizeof(pkgListType) * PKGLISTSIZE);
    Telemetry = (TelemetryType *)malloc(sizeof(TelemetryType) * TLMLISTSIZE);
    txQueue = (txQueueType *)malloc(sizeof(txQueueType) * PKGTXSIZE);
    // TNC2Raw = (int *)malloc(sizeof(int) * PKGTXSIZE);
#endif

    memset(pkgList, 0, sizeof(pkgListType) * PKGLISTSIZE);
    memset(Telemetry, 0, sizeof(TelemetryType) * TLMLISTSIZE);
    memset(txQueue, 0, sizeof(txQueueType) * PKGTXSIZE);

    pinMode(0, INPUT_PULLUP); // BOOT Button
    pinMode(LED_RX, OUTPUT);
    pinMode(LED_TX, OUTPUT);

    // Set up serial port
#ifdef CORE_DEBUG_LEVEL
    Serial.begin(115200); // debug
#else
    Serial.begin(9600); // monitor
#endif

    // Inicializar SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to initialize SPIFFS");
        return;
    }
    Serial.println("SPIFFS initialized successfully");   

    Serial.println();
    Serial.println("Start ESP32IGate V" + String(VERSION));
    Serial.println("Push BOOT after 3 sec for Factory Default config.");

    if (!EEPROM.begin(EEPROM_SIZE))
    {
        Serial.println(F("failed to initialise EEPROM")); // delay(100000);
    }
    // ตรวจสอบคอนฟิกซ์ผิดพลาด
    ptr = (byte *)&config;
    EEPROM.readBytes(1, ptr, sizeof(Configuration));
    uint8_t chkSum = checkSum(ptr, sizeof(Configuration));
    Serial.printf("EEPROM Check %0Xh=%0Xh(%dByte)\n", EEPROM.read(0), chkSum, sizeof(Configuration));
    if (EEPROM.read(0) != chkSum)
    {
        Serial.println("CFG EEPROM Error!");
        Serial.println("Factory Default");
        defaultConfig();
    }

    if (config.i2c1_enable)
    {
        Wire1.begin(config.i2c1_sda_pin, config.i2c1_sck_pin, config.i2c1_freq);
    }

#ifdef OLED
    config.i2c_enable = true;
    Wire.begin(config.i2c_sda_pin, config.i2c_sck_pin, config.i2c_freq);

    // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false); // initialize with the I2C addr 0x3C (for the 128x64)
    // Initialising the UI will init the display too.
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);

    display.setTextSize(1);
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(0, 15);
    display.print("APRS");
    display.setCursor(60, 32);
    display.print("I-GATE");
    display.drawYBitmap(0, 16, LOGO, 48, 48, WHITE);
    display.drawRoundRect(52, 16, 75, 22, 3, WHITE);

    display.setFont();
    display.setTextColor(WHITE);

    display.setCursor(60, 40);
    display.printf("FW Ver %s%c", VERSION, VERSION_BUILD);
    display.setCursor(65, 5);
    display.print("Copy@2025");
    display.display();

    delay(1000);
    digitalWrite(LED_TX, HIGH);
    display.fillRect(49, 49, 50, 8, 0);
    display.setCursor(70, 50);
    display.print("3 Sec");
    display.display();
    delay(1000);
    digitalWrite(LED_RX, HIGH);
    display.fillRect(49, 49, 50, 8, 0);
    display.setCursor(70, 50);
    display.print("2 Sec");
    display.display();
    delay(1000);
    display.fillRect(49, 49, 50, 8, 0);
    display.setCursor(70, 50);
    display.print("1 Sec");
    display.display();
    delay(1000);
#else
    if (config.i2c_enable)
    {
        Wire.begin(config.i2c_sda_pin, config.i2c_sck_pin, config.i2c_freq);
    }
    delay(1000);
    digitalWrite(LED_TX, HIGH);
    delay(1000);
    digitalWrite(LED_RX, HIGH);
    delay(1000);
#endif
    if (digitalRead(0) == LOW)
    {
        defaultConfig();
        Serial.println("Manual Default configure!");
#ifdef OLED
        display.clearDisplay();
        display.setCursor(10, 22);
        display.print("Factory Reset!");
        display.display();
#endif
        while (digitalRead(0) == LOW)
        {
            delay(500);
            digitalWrite(LED_TX, LOW);
            digitalWrite(LED_RX, LOW);
            delay(500);
            digitalWrite(LED_TX, HIGH);
            digitalWrite(LED_RX, HIGH);
        }
    }
    digitalWrite(LED_TX, LOW);
    digitalWrite(LED_RX, LOW);

    if (config.counter0_enable)
    {
        pcnt_init_channel(PCNT_UNIT_0, config.counter0_gpio, config.counter0_active); // Initialize Unit 0 to pin 4
    }
    if (config.counter1_enable)
    {
        pcnt_init_channel(PCNT_UNIT_1, config.counter1_gpio, config.counter1_active); // Initialize Unit 0 to pin 4
    }

    if (config.uart0_enable)
    {
#ifndef CORE_DEBUG_LEVEL
        Serial.begin(config.uart0_baudrate, SERIAL_8N1, config.uart0_rx_gpio, config.uart0_tx_gpio);
#endif
    }
    if (config.uart1_enable)
    {
        Serial1.begin(config.uart1_baudrate, SERIAL_8N1, config.uart1_rx_gpio, config.uart1_tx_gpio);
    }
    if (config.uart2_enable)
    {
        Serial2.begin(config.uart2_baudrate, SERIAL_8N1, config.uart2_rx_gpio, config.uart2_tx_gpio);
    }

    if (config.modbus_enable)
    {
        if (config.modbus_channel == 1)
        {
            modbus.begin(config.modbus_address, Serial);
        }
        else if (config.modbus_channel == 2)
        {
            modbus.begin(config.modbus_address, Serial1);
        }
        else if (config.modbus_channel == 3)
        {
            modbus.begin(config.modbus_address, Serial2);
        }
        if (config.modbus_channel > 0 && config.modbus_channel < 4)
        {
            // Modbus slave ID 1
            if (config.modbus_de_gpio > -1)
            {
                pinMode(config.modbus_de_gpio, OUTPUT);
                // Callbacks allow us to configure the RS485 transceiver correctly
                modbus.preTransmission(preTransmission);
                modbus.postTransmission(postTransmission);
            }
        }
    }

    RF_MODULE(true);
    log_d("Free heap: %d", ESP.getHeapSize());
#ifdef BLUETOOTH
    if (config.bt_master)
        SerialBT.begin(config.bt_name); // Bluetooth device name
#endif

#ifdef OLED
    if (config.oled_enable == true)
    {
        display.clearDisplay();
        display.setTextSize(1);
        display.display();
    }
#endif

    showDisp = true;
    curTab = 3;
    oledSleepTimeout = millis() + (config.oled_timeout * 1000);

    // enableLoopWDT();
    // enableCore0WDT();
    // enableCore1WDT();
    esp_task_wdt_init(WDT_TIMEOUT, true); // enable panic so ESP32 restarts
    esp_task_wdt_add(NULL);               // add current thread to WDT watch

    oledSleepTimeout = millis() + (config.oled_timeout * 1000);
    AFSKInitAct = false;

    if (config.gnss_enable)
    {
        if ((config.gnss_channel > 0) && (config.gnss_channel < 4))
        {
            if (strstr("AT", config.gnss_at_command) >= 0)
            {
                if (config.gnss_channel == 1)
                {
                    Serial.println(config.gnss_at_command);
                }
                else if (config.gnss_channel == 2)
                {
                    Serial1.println(config.gnss_at_command);
                }
                else if (config.gnss_channel == 3)
                {
                    Serial2.println(config.gnss_at_command);
                }
            }
        }
    }

    // Task 1
    xTaskCreatePinnedToCore(
        taskAPRS,        /* Function to implement the task */
        "taskAPRS",      /* Name of the task */
        8192,            /* Stack size in words */
        NULL,            /* Task input parameter */
        2,               /* Priority of the task */
        &taskAPRSHandle, /* Task handle. */
        0);              /* Core where the task should run */

    // Task 2
    xTaskCreatePinnedToCore(
        taskNetwork,        /* Function to implement the task */
        "taskNetwork",      /* Name of the task */
        16384,              /* Stack size in words */
        NULL,               /* Task input parameter */
        1,                  /* Priority of the task */
        &taskNetworkHandle, /* Task handle. */
        1);                 /* Core where the task should run */

    xTaskCreatePinnedToCore(
        taskAPRSPoll,        /* Function to implement the task */
        "taskAPRSPoll",      /* Name of the task */
        4096,                /* Stack size in words */
        NULL,                /* Task input parameter */
        1,                   /* Priority of the task */
        &taskAPRSPollHandle, /* Task handle. */
        0);                  /* Core where the task should run */

    if (config.gnss_enable)
    {
        xTaskCreatePinnedToCore(
            taskGPS,        /* Function to implement the task */
            "taskGPS",      /* Name of the task */
            3072,           /* Stack size in words */
            NULL,           /* Task input parameter */
            2,              /* Priority of the task */
            &taskGPSHandle, /* Task handle. */
            1);             /* Core where the task should run */
    }

    if (config.ext_tnc_enable || (config.wx_en && (config.wx_channel > 0 && config.wx_channel < 4)))
    {
        xTaskCreatePinnedToCore(
            taskSerial,        /* Function to implement the task */
            "taskSerial",      /* Name of the task */
            2048,              /* Stack size in words */
            NULL,              /* Task input parameter */
            3,                 /* Priority of the task */
            &taskSerialHandle, /* Task handle. */
            1);                /* Core where the task should run */
    }
}

String getTimeStamp()
{
    char strtmp[50];
    time_t now;
    time(&now);
    struct tm *info = gmtime(&now);
    sprintf(strtmp, "%02d%02d%02dz", info->tm_mday, info->tm_hour, info->tm_min);
    return String(strtmp);
}

int pkgCount = 0;

float conv_coords(float in_coords)
{
    // Initialize the location.
    float f = in_coords;
    // Get the first two digits by turning f into an integer, then doing an integer divide by 100;
    // firsttowdigits should be 77 at this point.
    int firsttwodigits = ((int)f) / 100; // This assumes that f < 10000.
    float nexttwodigits = f - (float)(firsttwodigits * 100);
    float theFinalAnswer = (float)(firsttwodigits + nexttwodigits / 60.0);
    return theFinalAnswer;
}

void DD_DDDDDtoDDMMSS(float DD_DDDDD, int *DD, int *MM, int *SS)
{
    DD_DDDDD = abs(DD_DDDDD);
    *DD = (int)DD_DDDDD;
    *MM = (int)((DD_DDDDD - *DD) * 60);
    *SS = ((DD_DDDDD - *DD) * 60 - *MM) * 100;
}

String compress_position(double nowLat, double nowLng, int alt_feed, double course, uint16_t spdKnot, char table, char symbol, bool gps)
{
    String str_comp = "";
    String lat, lon;
    lat = deg2lat(nowLat);
    lon = deg2lon(nowLng);
    ESP_LOGE("GPS", "Aprs Compress");
    // Translate from semicircles to Base91 format
    char aprs_position[13];
    long latitude = semicircles((char *)lat.c_str(), (nowLat < 0));
    long longitude = semicircles((char *)lon.c_str(), (nowLng < 0));
    long ltemp = 1073741824L - latitude; // 90 degrees - latitude
    ESP_LOGE("GPS", "lat=%u lon=%u", latitude, longitude);
    memset(aprs_position, 0, sizeof(aprs_position));

    base91encode(ltemp, aprs_position);
    ltemp = 1073741824L + (longitude >> 1); // 180 degrees + longitude
    base91encode(ltemp, aprs_position + 4);
    // Encode heading
    uint8_t c = (uint8_t)(course / 4);
    // Scan lookup table to encode speed
    uint8_t s = (uint8_t)(log(spdKnot + 1) / log(1.08));
    if ((spdKnot <= 5) && (alt_feed > 0) && config.trk_altitude)
    {
        if (gps && config.trk_cst)
        {
            // Send Altitude
            aprs_position[11] = '!' + 0x30; // t current,GGA
            int alt = (int)alt_feed;
            int cs = (int)(log(alt) / log(1.002));
            c = (uint8_t)(cs / 91);
            s = (uint8_t)(cs - ((int)c * 91));
            if (s > 91)
                s = 91;
            aprs_position[9] = '!' + c;  // c
            aprs_position[10] = '!' + s; // s
        }
        else
        {
            // Send Range
            aprs_position[11] = '!' + 0x00; //
            aprs_position[9] = '{';         // c = {
            if (!config.rf_power)
            {
                s = 10;
            }
            else
            {
                s = 30;
            }
            aprs_position[10] = '!' + s; // s
        }
    }
    else
    {
        // Send course and speed
        aprs_position[9] = '!' + c;  // c
        aprs_position[10] = '!' + s; // s

        if (gps)
        {
            aprs_position[11] = '!' + 0x20 + 0x18 + 0x06; // t 0x20 1=current,0x18 11=RMC,0x06 110=Other tracker
        }
        else
        {
            aprs_position[11] = '!' + 0x00 + 0x18 + 0x06; // t
        }
    }
    aprs_position[12] = 0;
    // waveFlag = false;
    aprs_position[8] = symbol; // Symbol
    str_comp = String(table) + String(aprs_position);
    return str_comp;
}

String trk_gps_postion(String comment)
{
    String rawData = "";
    String lat, lon;
    double nowLat, nowLng;
    char rawTNC[300];
    char aprs_table, aprs_symbol;
    char timestamp[10];
    struct tm tmstruct;
    double dist, course, speed;
    time_t nowTime;

    memset(rawTNC, 0, sizeof(rawTNC));
    getLocalTime(&tmstruct, 5000);
    sprintf(timestamp, "%02d%02d%02d%02d", (tmstruct.tm_mon + 1), tmstruct.tm_mday, tmstruct.tm_hour, tmstruct.tm_min);
    time(&nowTime);
    // nowTime = gps.time.value();
    if (lastTimeStamp == 0)
        lastTimeStamp = nowTime;

    // lastTimeStamp=10000;
    // nowTime=lastTimeStamp+1800;
    time_t tdiff = nowTime - lastTimeStamp;

    // aprs_table = config.aprs_table;
    // aprs_symbol = config.aprs_symbol;
    if (config.trk_smartbeacon)
    {
        if (SB_SPEED < config.trk_lspeed)
        {
            aprs_table = config.trk_symstop[0];
            aprs_symbol = config.trk_symstop[1];
            SB_SPEED = 0;
        }
        else
        {
            aprs_table = config.trk_symmove[0];
            aprs_symbol = config.trk_symmove[1];
        }
    }
    else
    {
        aprs_table = config.trk_symbol[0];
        aprs_symbol = config.trk_symbol[1];
    }

    if (gps.location.isValid()) // && (gps.hdop.hdop() < 10.0))
    {
        nowLat = gps.location.lat();
        nowLng = gps.location.lng();

        uint16_t spdKnot;
        if (LastLng == 0 || LastLat == 0)
        {
            course = gps.course.deg();
            spdKnot = (uint16_t)gps.speed.knots();
        }
        else
        {
            dist = distance(LastLng, LastLat, nowLng, nowLat);
            course = direction(LastLng, LastLat, nowLng, nowLat);
            if (dist > 50.0F)
                dist = 0;
            if (tdiff > 10 && (nowTime > lastTimeStamp))
                speed = dist / ((double)tdiff / 3600);
            else
                speed = 0.0F;

            if (speed > 999)
                speed = 999.0F;
            // uint16_t spdMph=(uint16_t)(speed / 1.609344);
            spdKnot = (uint16_t)(speed * 0.53996F);
            if (spdKnot > 94)
                spdKnot = 0;
        }

        LastLat = nowLat;
        LastLng = nowLng;
        lastTimeStamp = nowTime;

        if (config.trk_compress)
        { // Compress DATA

            String compPosition = compress_position(nowLat, nowLng, gps.altitude.feet(), course, spdKnot, aprs_table, aprs_symbol, (gps.satellites.value() > 3));
            // ESP_LOGE("GPS", "Compress=%s", aprs_position);
            if (strlen(config.trk_item) >= 3)
            {
                char object[10];
                memset(object, 0x20, 10);
                memcpy(object, config.trk_item, strlen(config.trk_item));
                object[9] = 0;
                if (config.trk_timestamp)
                {
                    String timeStamp = getTimeStamp();
                    sprintf(rawTNC, ";%s*%s%s", object, timeStamp, compPosition.c_str());
                }
                else
                {
                    sprintf(rawTNC, ")%s!%s", config.trk_item, compPosition.c_str());
                }
            }
            else
            {
                if (config.trk_timestamp)
                {
                    String timeStamp = getTimeStamp();
                    sprintf(rawTNC, "/%s%s", timeStamp, compPosition.c_str());
                }
                else
                {
                    sprintf(rawTNC, "!%s", compPosition.c_str());
                }
            }
        }
        else
        { // None compress DATA
            int lat_dd, lat_mm, lat_ss, lon_dd, lon_mm, lon_ss;
            char lon_ew = 'E';
            char lat_ns = 'N';
            if (nowLat < 0)
                lat_ns = 'S';
            if (nowLng < 0)
                lon_ew = 'W';
            DD_DDDDDtoDDMMSS(nowLat, &lat_dd, &lat_mm, &lat_ss);
            DD_DDDDDtoDDMMSS(nowLng, &lon_dd, &lon_mm, &lon_ss);
            char csd_spd[8];
            memset(csd_spd, 0, sizeof(csd_spd));
            if (config.trk_cst)
            {
                sprintf(csd_spd, "%03d/%03d", (int)gps.course.deg(), (int)gps.speed.knots());
            }
            if (strlen(config.trk_item) >= 3)
            {
                char object[10];
                memset(object, 0x20, 10);
                memcpy(object, config.trk_item, strlen(config.trk_item));
                object[9] = 0;
                if (config.trk_timestamp)
                {
                    String timeStamp = getTimeStamp();
                    sprintf(rawTNC, ";%s*%s%02d%02d.%02d%c%c%03d%02d.%02d%c%c%s", object, timeStamp, lat_dd, lat_mm, lat_ss, lat_ns, aprs_table, lon_dd, lon_mm, lon_ss, lon_ew, aprs_symbol, csd_spd);
                }
                else
                {
                    sprintf(rawTNC, ")%s!%02d%02d.%02d%c%c%03d%02d.%02d%c%c%s", config.trk_item, lat_dd, lat_mm, lat_ss, lat_ns, aprs_table, lon_dd, lon_mm, lon_ss, lon_ew, aprs_symbol, csd_spd);
                }
            }
            else
            {
                if (config.trk_timestamp)
                {
                    String timeStamp = getTimeStamp();
                    sprintf(rawTNC, "/%s%02d%02d.%02d%c%c%03d%02d.%02d%c%c%s", timeStamp, lat_dd, lat_mm, lat_ss, lat_ns, aprs_table, lon_dd, lon_mm, lon_ss, lon_ew, aprs_symbol, csd_spd);
                }
                else
                {
                    sprintf(rawTNC, "!%02d%02d.%02d%c%c%03d%02d.%02d%c%c%s", lat_dd, lat_mm, lat_ss, lat_ns, aprs_table, lon_dd, lon_mm, lon_ss, lon_ew, aprs_symbol, csd_spd);
                }
            }
            if (config.trk_altitude)
            {

                if (gps.altitude.isValid())
                {
                    char strAltitude[10];
                    memset(strAltitude, 0, sizeof(strAltitude));
                    sprintf(strAltitude, "/A=%06d", (int)gps.altitude.feet());
                    strcat(rawTNC, strAltitude);
                }
            }
        }
    }
    else
    {
        sprintf(rawTNC, ">%s ", config.trk_item);
    }

    String tnc2Raw = "";
    char strtmp[300];
    if (config.trk_ssid == 0)
        sprintf(strtmp, "%s>APE32I", config.trk_mycall);
    else
        sprintf(strtmp, "%s-%d>APE32I", config.trk_mycall, config.trk_ssid);
    tnc2Raw = String(strtmp);
    if (config.trk_path < 5)
    {
        if (config.trk_path > 0)
            tnc2Raw += "-" + String(config.trk_path);
    }
    else
    {
        tnc2Raw += ",";
        tnc2Raw += getPath(config.trk_path);
    }
    tnc2Raw += ":";
    tnc2Raw += String(rawTNC);
    tnc2Raw += comment + " " + String(config.trk_comment);
    return tnc2Raw;
}

String trk_fix_position(String comment)
{
    char strtmp[500], loc[100];
    String tnc2Raw = "";
    memset(strtmp, 0, sizeof(strtmp));
    memset(loc, 0, sizeof(loc));
    if (config.trk_compress)
    { // Compress DATA

        String compPosition = compress_position(config.trk_lat, config.trk_lon, (int)(config.trk_alt * 3.28F), 0, 0, config.trk_symbol[0], config.trk_symbol[1], true);
        // ESP_LOGE("GPS", "Compress=%s", aprs_position);
        if (strlen(config.trk_item) >= 3)
        {
            char object[10];
            memset(object, 0x20, 10);
            memcpy(object, config.trk_item, strlen(config.trk_item));
            object[9] = 0;
            if (config.trk_timestamp)
            {
                String timeStamp = getTimeStamp();
                sprintf(loc, ";%s*%s%s", object, timeStamp, compPosition.c_str());
            }
            else
            {
                sprintf(loc, ")%s!%s", config.trk_item, compPosition.c_str());
            }
        }
        else
        {
            if (config.trk_timestamp)
            {
                String timeStamp = getTimeStamp();
                sprintf(loc, "/%s%s", timeStamp, compPosition.c_str());
            }
            else
            {
                sprintf(loc, "!%s", compPosition.c_str());
            }
        }
    }
    else
    {
        int lat_dd, lat_mm, lat_ss, lon_dd, lon_mm, lon_ss;

        char lon_ew = 'E';
        char lat_ns = 'N';
        if (config.trk_lat < 0)
            lat_ns = 'S';
        if (config.trk_lon < 0)
            lon_ew = 'W';

        DD_DDDDDtoDDMMSS(config.trk_lat, &lat_dd, &lat_mm, &lat_ss);
        DD_DDDDDtoDDMMSS(config.trk_lon, &lon_dd, &lon_mm, &lon_ss);

        if (strlen(config.trk_item) >= 3)
        {
            char object[10];
            memset(object, 0x20, 10);
            memcpy(object, config.trk_item, strlen(config.trk_item));
            object[9] = 0;
            if (config.trk_timestamp)
            {
                String timeStamp = getTimeStamp();
                sprintf(loc, ";%s*%s%02d%02d.%02d%c%c%03d%02d.%02d%c%c", object, timeStamp, lat_dd, lat_mm, lat_ss, lat_ns, config.trk_symbol[0], lon_dd, lon_mm, lon_ss, lon_ew, config.trk_symbol[1]);
            }
            else
            {
                sprintf(loc, ")%s!%02d%02d.%02d%c%c%03d%02d.%02d%c%c", config.trk_item, lat_dd, lat_mm, lat_ss, lat_ns, config.trk_symbol[0], lon_dd, lon_mm, lon_ss, lon_ew, config.trk_symbol[1]);
            }
        }
        else
        {
            if (config.trk_timestamp)
            {
                String timeStamp = getTimeStamp();
                sprintf(loc, "/%s%02d%02d.%02d%c%c%03d%02d.%02d%c%c", timeStamp, lat_dd, lat_mm, lat_ss, lat_ns, config.trk_symbol[0], lon_dd, lon_mm, lon_ss, lon_ew, config.trk_symbol[1]);
            }
            else
            {
                sprintf(loc, "!%02d%02d.%02d%c%c%03d%02d.%02d%c%c", lat_dd, lat_mm, lat_ss, lat_ns, config.trk_symbol[0], lon_dd, lon_mm, lon_ss, lon_ew, config.trk_symbol[1]);
            }
        }

        if (config.trk_alt > 0)
        {
            char strAltitude[12];
            memset(strAltitude, 0, sizeof(strAltitude));
            sprintf(strAltitude, "/A=%06d", (int)(config.trk_alt * 3.28F));
            strcat(loc, strAltitude);
        }
    }

    if (config.trk_ssid == 0)
        sprintf(strtmp, "%s>APE32I", config.trk_mycall);
    else
        sprintf(strtmp, "%s-%d>APE32I", config.trk_mycall, config.trk_ssid);
    tnc2Raw = String(strtmp);
    if (config.trk_path < 5)
    {
        if (config.trk_path > 0)
            tnc2Raw += "-" + String(config.trk_path);
    }
    else
    {
        tnc2Raw += ",";
        tnc2Raw += getPath(config.trk_path);
    }
    tnc2Raw += ":";
    tnc2Raw += String(loc);
    tnc2Raw += comment + " " + String(config.trk_comment);
    return tnc2Raw;
}

String igate_position(double lat, double lon, double alt, String comment)
{
    String tnc2Raw = "";
    int lat_dd, lat_mm, lat_ss, lon_dd, lon_mm, lon_ss;
    char strtmp[500], loc[100];
    char lon_ew = 'E';
    char lat_ns = 'N';
    if (lat < 0)
        lat_ns = 'S';
    if (lon < 0)
        lon_ew = 'W';
    memset(strtmp, 0, sizeof(strtmp));
    memset(loc, 0, sizeof(loc));
    DD_DDDDDtoDDMMSS(lat, &lat_dd, &lat_mm, &lat_ss);
    DD_DDDDDtoDDMMSS(lon, &lon_dd, &lon_mm, &lon_ss);
    char strAltitude[12];
    memset(strAltitude, 0, sizeof(strAltitude));
    if (alt > 0)
    {
        sprintf(strAltitude, "/A=%06d", (int)(alt * 3.28F));
    }
    if (strlen(config.igate_object) >= 3)
    {
        char object[10];
        memset(object, 0x20, 10);
        memcpy(object, config.igate_object, strlen(config.igate_object));
        object[9] = 0;
        if (config.igate_timestamp)
        {
            String timeStamp = getTimeStamp();
            sprintf(loc, ";%s*%s%02d%02d.%02d%c%c%03d%02d.%02d%c%c", object, timeStamp, lat_dd, lat_mm, lat_ss, lat_ns, config.igate_symbol[0], lon_dd, lon_mm, lon_ss, lon_ew, config.igate_symbol[1]);
        }
        else
        {
            sprintf(loc, ")%s!%02d%02d.%02d%c%c%03d%02d.%02d%c%c", config.igate_object, lat_dd, lat_mm, lat_ss, lat_ns, config.igate_symbol[0], lon_dd, lon_mm, lon_ss, lon_ew, config.igate_symbol[1]);
        }
    }
    else
    {
        if (config.igate_timestamp)
        {
            String timeStamp = getTimeStamp();
            sprintf(loc, "/%s%02d%02d.%02d%c%c%03d%02d.%02d%c%c", timeStamp.c_str(), lat_dd, lat_mm, lat_ss, lat_ns, config.igate_symbol[0], lon_dd, lon_mm, lon_ss, lon_ew, config.igate_symbol[1]);
        }
        else
        {
            sprintf(loc, "!%02d%02d.%02d%c%c%03d%02d.%02d%c%c", lat_dd, lat_mm, lat_ss, lat_ns, config.igate_symbol[0], lon_dd, lon_mm, lon_ss, lon_ew, config.igate_symbol[1]);
        }
    }
    if (config.aprs_ssid == 0)
        sprintf(strtmp, "%s>APE32I", config.aprs_mycall);
    else
        sprintf(strtmp, "%s-%d>APE32I", config.aprs_mycall, config.aprs_ssid);
    tnc2Raw = String(strtmp);
    if (config.igate_path < 5)
    {
        if (config.igate_path > 0)
            tnc2Raw += "-" + String(config.igate_path);
    }
    else
    {
        tnc2Raw += ",";
        tnc2Raw += getPath(config.igate_path);
    }
    tnc2Raw += ":";
    tnc2Raw += String(loc);
    tnc2Raw += String(config.igate_phg) + String(strAltitude);
    tnc2Raw += comment + " " + String(config.igate_comment);
    return tnc2Raw;
}

String digi_position(double lat, double lon, double alt, String comment)
{
    String tnc2Raw = "";
    int lat_dd, lat_mm, lat_ss, lon_dd, lon_mm, lon_ss;
    char strtmp[500], loc[100];
    char lon_ew = 'E';
    char lat_ns = 'N';
    if (lat < 0)
        lat_ns = 'S';
    if (lon < 0)
        lon_ew = 'W';
    memset(strtmp, 0, sizeof(strtmp));
    memset(loc, 0, sizeof(loc));
    DD_DDDDDtoDDMMSS(lat, &lat_dd, &lat_mm, &lat_ss);
    DD_DDDDDtoDDMMSS(lon, &lon_dd, &lon_mm, &lon_ss);
    char strAltitude[12];
    memset(strAltitude, 0, sizeof(strAltitude));
    if (alt > 0)
    {
        sprintf(strAltitude, "/A=%06d", (int)(alt * 3.28F));
    }
    if (config.digi_timestamp)
    {
        String timeStamp = getTimeStamp();
        sprintf(loc, "/%s%02d%02d.%02d%c%c%03d%02d.%02d%c%c", timeStamp, lat_dd, lat_mm, lat_ss, lat_ns, config.digi_symbol[0], lon_dd, lon_mm, lon_ss, lon_ew, config.digi_symbol[1]);
    }
    else
    {
        sprintf(loc, "!%02d%02d.%02d%c%c%03d%02d.%02d%c%c", lat_dd, lat_mm, lat_ss, lat_ns, config.digi_symbol[0], lon_dd, lon_mm, lon_ss, lon_ew, config.digi_symbol[1]);
    }
    if (config.digi_ssid == 0)
        sprintf(strtmp, "%s>APE32I", config.digi_mycall);
    else
        sprintf(strtmp, "%s-%d>APE32I", config.digi_mycall, config.digi_ssid);
    tnc2Raw = String(strtmp);
    if (config.digi_path < 5)
    {
        if (config.digi_path > 0)
            tnc2Raw += "-" + String(config.digi_path);
    }
    else
    {
        tnc2Raw += ",";
        tnc2Raw += getPath(config.digi_path);
    }
    tnc2Raw += ":";
    tnc2Raw += String(loc);
    tnc2Raw += String(config.digi_phg) + String(strAltitude);
    tnc2Raw += comment + " " + String(config.digi_comment);
    return tnc2Raw;
}

int packet2Raw(String &tnc2, AX25Msg &Packet)
{
    if (Packet.len < 5)
        return 0;
    tnc2 = String(Packet.src.call);
    if (Packet.src.ssid > 0)
    {
        tnc2 += String(F("-"));
        tnc2 += String(Packet.src.ssid);
    }
    tnc2 += String(F(">"));
    tnc2 += String(Packet.dst.call);
    if (Packet.dst.ssid > 0)
    {
        tnc2 += String(F("-"));
        tnc2 += String(Packet.dst.ssid);
    }
    for (int i = 0; i < Packet.rpt_count; i++)
    {
        if(i>=8) break;
        tnc2 += String(",");
        tnc2 += String(Packet.rpt_list[i].call);
        if (Packet.rpt_list[i].ssid > 0)
        {
            tnc2 += String("-");
            tnc2 += String(Packet.rpt_list[i].ssid);
        }
        if (Packet.rpt_flags & (1 << i))
            tnc2 += "*";
    }
    tnc2 += String(F(":"));
    tnc2 += String((const char *)Packet.info);

    return tnc2.length();
}

long sendTimer = 0;
int btn_count = 0;
long timeCheck = 0;
int timeHalfSec = 0;

unsigned long timeTask;
unsigned long timeSec;
char nmea[100];
int nmea_idx = 0;
void loop()
{
    if (millis() > timeTask)
    {
        timeTask = millis() + 10000;
        log_d("Task process APRS=%iuS\t NETWORK=%iuS\t GPS=%iuS\t SERIAL=%iuS\n", timerAPRS, timerNetwork, timerGPS, timerSerial);
        log_d("Free heap: %s KB \tWiFi:%s ,RSSI:%s dBm", String((float)ESP.getFreeHeap() / 1000, 1).c_str(), String(WiFi.SSID()).c_str(), String(WiFi.RSSI()).c_str());
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);

#ifdef BLUETOOTH
    if (config.bt_master)
        Bluetooth();
#endif

    if (digitalRead(0) == LOW)
    {
        btn_count++;
        if (btn_count > 1000) // Push BOOT 10sec
        {
            digitalWrite(LED_PIN, HIGH);
            digitalWrite(LED_TX_PIN, HIGH);
        }
    }
    else
    {
        if (btn_count > 0)
        {
            // Serial.printf("btn_count=%dms\n", btn_count * 10);
            if (btn_count > 1000) // Push BOOT 10sec to Factory Default
            {
                // digitalWrite(LED_RX, LOW);
                // digitalWrite(LED_TX, LOW);
                defaultConfig();
                log_d("SYSTEM REBOOT NOW!");
                esp_restart();
            }
            else
            {
                showDisp = true;
                timeSec = timeHalfSec = millis();
                // if (oledSleepTimeout > 0)
                //{
                curTab++;
                if (curTab > 3)
                    curTab = 0;
                //}
            }
            btn_count = 0;
        }
    }

#ifdef OLED
    // Popup Display
    if (config.oled_enable == true)
    {
        if (dispBuffer.getCount() > 0)
        {
            if (millis() > timeHalfSec)
            {
                char tnc2[300];
                dispBuffer.pop(&tnc2);
                dispWindow(String(tnc2), 0, false);
                timeHalfSec = millis() + (config.dispDelay * 1000);
                oledSleepTimeout = millis() + (config.oled_timeout * 1000);
            }
        }
        else
        {
            if (!getTransmit())
            {
                if (queTxDisp.getCount() > 0)
                { // have tx info display
                    txDisp txs;
                    if (queTxDisp.pop(&txs))
                    {
                        dispTxWindow(txs);
                        delay(1000);
                        // if (menuSel == 0)
                        //     curTabOld = curTab + 1;
                    }
                }
            }

            // Sleep display
            if (millis() > timeHalfSec)
            {
                if (millis() > timeSec && timeHalfSec > 0)
                {
                    timeSec = millis() + 10000;
                    showDisp = true;
                    // timeHalfSec = 0;
                    // oledSleepTimeout = millis() + (config.oled_timeout * 1000);
                }
                else
                {
                    if (millis() > oledSleepTimeout && oledSleepTimeout > 0)
                    {
                        showDisp = false;
                        timeHalfSec = 0;
                        oledSleepTimeout = 0;
                        display.clearDisplay();
                        display.display();
                    }
                }
            }
        }

        if (showDisp)
        {
            showDisp = false;
            timeHalfSec = millis();
            // oledSleepTimeout = millis() + (config.oled_timeout * 1000);
            switch (curTab)
            {
            case 0:
                statisticsDisp();
                timeSec = millis() + 10000;
                break;
            case 1:
                pkgLastDisp();
                timeSec = millis() + 10000;
                break;
            case 2:
                pkgCountDisp();
                timeSec = millis() + 10000;
                break;
            case 3:
                systemDisp();
                break;
            }
        }
    }
#endif

    // Tick one secound
    if (millis() > timeCheck)
    {
        esp_task_wdt_reset();
        timeCheck = millis() + 1000;
        if (ESP.getFreeHeap() < 60000)
            esp_restart();
        // Serial.println(String(ESP.getFreeHeap()));
    }
}

String sendIsAckMsg(String toCallSign, int msgId)
{
    char str[300];
    char call[11];
    int i;
    memset(&call[0], 0, 11);
    sprintf(call, "%s-%d", config.aprs_mycall, config.aprs_ssid);
    strcpy(&call[0], toCallSign.c_str());
    i = strlen(call);
    for (; i < 9; i++)
        call[i] = 0x20;
    memset(&str[0], 0, 300);

    sprintf(str, "%s-%d>APE32I%s::%s:ack%d", config.aprs_mycall, config.aprs_ssid, VERSION, call, msgId);
    //	client.println(str);
    return String(str);
}

void sendIsPkg(char *raw)
{
    char str[300];
    sprintf(str, "%s-%d>APE32I%s:%s", config.aprs_mycall, config.aprs_ssid, VERSION, raw);
    // client.println(str);
    String tnc2Raw = String(str);
    if (aprsClient.connected())
        aprsClient.println(tnc2Raw); // Send packet to Inet
    if (config.digi_en)
        pkgTxPush(str, strlen(str), 0);
}

void sendIsPkgMsg(char *raw)
{
    char str[300];
    char call[11];
    int i;
    memset(&call[0], 0, 11);
    if (config.aprs_ssid == 0)
        sprintf(call, "%s", config.aprs_mycall);
    else
        sprintf(call, "%s-%d", config.aprs_mycall, config.aprs_ssid);
    i = strlen(call);
    for (; i < 9; i++)
        call[i] = 0x20;

    if (config.aprs_ssid == 0)
        sprintf(str, "%s>APE32I::%s:%s", config.aprs_mycall, call, raw);
    else
        sprintf(str, "%s-%d>APE32I::%s:%s", config.aprs_mycall, config.aprs_ssid, call, raw);

    String tnc2Raw = String(str);
    if (aprsClient.connected())
        aprsClient.println(tnc2Raw); // Send packet to Inet
    // if (config.tnc && config.tnc_digi)
    //     pkgTxUpdate(str, 0);
    // APRS_sendTNC2Pkt(tnc2Raw); // Send packet to RF
}

void sendTelemetry_0(char *raw, bool header)
{
    char str[300];
    char call[11];
    int i;
    memset(&call[0], 0, 11);
    if (config.tlm0_ssid == 0)
        sprintf(call, "%s", config.tlm0_mycall);
    else
        sprintf(call, "%s-%d", config.tlm0_mycall, config.tlm0_ssid);
    i = strlen(call);
    for (; i < 9; i++)
        call[i] = 0x20;

    if (header)
    {
        if (config.tlm0_ssid == 0)
        {
            if (config.tlm0_path < 5)
            {
                if (config.tlm0_path > 0)
                    sprintf(str, "%s>APE32I-%d::%s:%s", config.tlm0_mycall, config.tlm0_path, call, raw);
                else
                    sprintf(str, "%s>APE32I::%s:%s", config.tlm0_mycall, call, raw);
            }
            else
            {
                sprintf(str, "%s>APE32I,%s::%s:%s", config.tlm0_mycall, getPath(config.tlm0_path).c_str(), call, raw);
            }
        }
        else
        {
            if (config.tlm0_path < 5)
            {
                if (config.tlm0_path > 0)
                    sprintf(str, "%s-%d>APE32I-%d::%s:%s", config.tlm0_mycall, config.tlm0_ssid, config.tlm0_path, call, raw);
                else
                    sprintf(str, "%s-%d>APE32I::%s:%s", config.tlm0_mycall, config.tlm0_ssid, call, raw);
            }
            else
            {
                sprintf(str, "%s-%d>APE32I,%s::%s:%s", config.tlm0_mycall, config.tlm0_ssid, getPath(config.tlm0_path).c_str(), call, raw);
            }
        }
    }
    else
    {
        if (config.tlm0_ssid == 0)
        {
            if (config.tlm0_path < 5)
            {
                if (config.tlm0_path > 0)
                    sprintf(str, "%s>APE32I-%d:%s", config.tlm0_mycall, config.tlm0_path, raw);
                else
                    sprintf(str, "%s>APE32I:%s", config.tlm0_mycall, raw);
            }
            else
            {
                sprintf(str, "%s>APE32I,%s:%s", config.tlm0_mycall, getPath(config.tlm0_path).c_str(), raw);
            }
        }
        else
        {
            if (config.tlm0_path < 5)
            {
                if (config.tlm0_path > 0)
                    sprintf(str, "%s-%d>APE32I-%d:%s", config.tlm0_mycall, config.tlm0_ssid, config.tlm0_path, raw);
                else
                    sprintf(str, "%s-%d>APE32I:%s", config.tlm0_mycall, config.tlm0_ssid, raw);
            }
            else
            {
                sprintf(str, "%s-%d>APE32I,%s:%s", config.tlm0_mycall, config.tlm0_ssid, getPath(config.tlm0_path).c_str(), raw);
            }
        }
    }

    if (config.tlm0_2rf)
    { // TLM SEND TO RF
        // char *rawP = (char *)malloc(rawData.length());
        //  rawData.toCharArray(rawP, rawData.length());
        // memcpy(rawP, rawData.c_str(), rawData.length());
        pkgTxPush(str, strlen(str), 0);
        // pushTxDisp(TXCH_RF, "TX DIGI POS", sts);
        // free(rawP);
    }
    if (config.tlm0_2inet)
    { // TLM SEND TO APRS-IS
        if (aprsClient.connected())
        {
            status.txCount++;
            aprsClient.printf("%s\r\n", str); // Send packet to Inet
            // pushTxDisp(TXCH_TCP, "TX DIGI POS", sts);
        }
    }
}

RTC_DATA_ATTR statusType statOld;
uint8_t getSensor(int ch)
{
    int val = 0;
    switch (ch)
    {
    case 0: // none
        val = 0;
        break;
    case 1: // allCount
        val = status.allCount - statOld.allCount;
        statOld.allCount = status.allCount;
        break;
    case 2: // rf2inet
        val = status.rf2inet - statOld.rf2inet;
        statOld.rf2inet = status.rf2inet;
        break;
    case 3: // inet2rf
        val = status.inet2rf - statOld.inet2rf;
        statOld.inet2rf = status.inet2rf;
        break;
    case 4: // digi
        val = status.digiCount - statOld.digiCount;
        statOld.digiCount = status.digiCount;
        break;
    case 5: // Drop
        val = status.dropCount - statOld.dropCount;
        statOld.dropCount = status.dropCount;
        break;
    }
    if (val < 0)
        val = 0;
    return val;
}

bool getBits(int ch)
{
    bool val = false;
    switch (ch)
    {
    case 0: // none
        val = 0;
        break;
    case 1: // IGATE Enable
        val = config.igate_en;
        break;
    case 2: // DIGI Enable
        val = config.digi_en;
        break;
    case 3: // WX Enable
        val = config.wx_en;
        break;
    case 4: // Sat Enable
        val = 0;
        break;
    case 5: // APRS-IS Status
        if (aprsClient.connected())
            val = 1;
        break;
    case 6: // VPN Status
        val = 0; // Elimina la referencia a wireguard_active()
        break;
    case 7: // 4G LTE
        val = 0;
        break;
    case 8: // MQTT
        val = 0;
        break;
    }
    return val;
}

void getTelemetry_0()
{
    systemTLM.A1 = getSensor(config.tml0_data_channel[0]);
    systemTLM.A2 = getSensor(config.tml0_data_channel[1]);
    systemTLM.A3 = getSensor(config.tml0_data_channel[2]);
    systemTLM.A4 = getSensor(config.tml0_data_channel[3]);
    systemTLM.A5 = getSensor(config.tml0_data_channel[4]);
    systemTLM.BITS = 0;
    if (getBits(config.tml0_data_channel[5]))
        systemTLM.BITS |= 0x01;
    if (getBits(config.tml0_data_channel[6]))
        systemTLM.BITS |= 0x02;
    if (getBits(config.tml0_data_channel[7]))
        systemTLM.BITS |= 0x04;
    if (getBits(config.tml0_data_channel[8]))
        systemTLM.BITS |= 0x08;
    if (getBits(config.tml0_data_channel[9]))
        systemTLM.BITS |= 0x10;
    if (getBits(config.tml0_data_channel[10]))
        systemTLM.BITS |= 0x20;
    if (getBits(config.tml0_data_channel[11]))
        systemTLM.BITS |= 0x40;
    if (getBits(config.tml0_data_channel[12]))
        systemTLM.BITS |= 0x80;
}

String getPath(int idx)
{
    String ret = "";
    switch (idx)
    {
    case 0: // OFF
        ret = "";
        break;
    case 1: // DST-TRACE1
    case 2: // DST-TRACE2
    case 3: // DST-TRACE3
    case 4: // DST-TRACE4
        ret = "DST" + String(idx);
        break;
    case 5: // TRACE1-1
        ret = "TRACE1-1";
        break;
    case 6:
        ret = "TRACE2-2";
        break;
    case 7:
        ret = "TRACE3-3";
        break;
    case 8:
        ret = "WIDE1-1";
        break;
    case 9:
        ret = "RFONLY";
        break;
    case 10:
        ret = "RELAY";
        break;
    case 11:
        ret = "GATE";
        break;
    case 12:
        ret = "ECHO";
        break;
    case 13: // UserDefine1
        ret = String(config.path[0]);
        break;
    case 14: // UserDefine2
        ret = String(config.path[1]);
        break;
    case 15: // UserDefine3
        ret = String(config.path[2]);
        break;
    case 16: // UserDefine4
        ret = String(config.path[3]);
        break;
    default:
        ret = "WIDE1-1";
        break;
    }
    return ret;
}

void GPS_INIT()
{
    // SerialGPS.begin(config.gnss_baudrate, SERIAL_8N1, config.gnss_rx_gpio, config.gnss_tx_gpio);
    //  SerialGPS.begin(config.gnss_baudrate, EspSoftwareSerial::SWSERIAL_8N1, config.gnss_rx_gpio, config.gnss_tx_gpio);
    // SerialGPS.flush();
    //  while (SerialGPS.available() > 0)
    //      SerialGPS.read();
}

WiFiClient gnssClient;
// WiFiClient tncClient;

extern AsyncWebSocket ws_gnss;

void taskGPSActive()
{
    int c;
    // log_d("GNSS Init");
    nmea_idx = 0;

    // if (config.gnss_enable)
    // {
    //     if ((config.gnss_channel > 0) && (config.gnss_channel < 4))
    //     {
    //         if (strstr("AT", config.gnss_at_command) >= 0)
    //         {
    //             if (config.gnss_channel == 1)
    //             {
    //                 Serial.println(config.gnss_at_command);
    //             }
    //             else if (config.gnss_channel == 2)
    //             {
    //                 Serial1.println(config.gnss_at_command);
    //             }
    //             else if (config.gnss_channel == 3)
    //             {
    //                 Serial2.println(config.gnss_at_command);
    //             }
    //         }
    //     }
    // }
    // for (;;)
    {
        if (config.gnss_enable)
        {
            if ((config.gnss_channel > 0) && (config.gnss_channel < 4))
            {
                do
                {
                    c = -1;
                    if (config.gnss_channel == 1)
                    {
                        c = Serial.read();
                    }
                    else if (config.gnss_channel == 2)
                    {
                        c = Serial1.read();
                    }
                    else if (config.gnss_channel == 3)
                    {
                        c = Serial2.read();
                    }
                    if (c > -1)
                    {
                        gps.encode((char)c);
                        if (webServiceBegin == false)
                        {
                            if (nmea_idx > 195)
                            {
                                nmea_idx = 0;
                                memset(nmea, 0, sizeof(nmea));
                                // SerialGNSS->flush();
                            }
                            else
                            {
                                nmea[nmea_idx++] = (char)c;
                                if ((char)c == '\r' || (char)c == '\n')
                                {
                                    nmea[nmea_idx] = 0;
                                    if (nmea_idx > 5)
                                    {
                                        if (ws_gnss.enabled() && !ws_gnss.getClients().isEmpty())
                                        // if (webServiceBegin == false)
                                        {
                                            handle_ws_gnss(nmea, nmea_idx);
                                        }
                                        // log_d("%s",nmea);
                                    }
                                    nmea_idx = 0;
                                    break;
                                }
                            }
                        }
                        //}
                    }
                    else
                    {
                        break;
                    }
                } while (1);
            }
            else if (config.gnss_channel == 4)
            { // TCP
                if (WiFi.isConnected())
                {
                    if (!gnssClient.connected())
                    {
                        gnssClient.connect(config.gnss_tcp_host, config.gnss_tcp_port);
                        log_d("GNSS TCP ReConnect to %s:%d", config.gnss_tcp_host, config.gnss_tcp_port);
                        delay(3000);
                    }
                    else
                    {
                        while (gnssClient.available())
                        {
                            c = (char)gnssClient.read();
                            // Serial.print(c);
                            gps.encode(c);
                            if (webServiceBegin == false)
                            {
                                if (nmea_idx > 195)
                                {
                                    nmea_idx = 0;
                                    memset(nmea, 0, sizeof(nmea));
                                }
                                else
                                {
                                    nmea[nmea_idx++] = c;
                                    if (c == '\r' || c == '\n')
                                    {
                                        nmea[nmea_idx] = 0;
                                        if (nmea_idx > 5)
                                        {
                                            if (ws_gnss.enabled() && !ws_gnss.getClients().isEmpty())
                                            {
                                                handle_ws_gnss(nmea, nmea_idx);
                                            }
                                            // log_d("%s",nmea);
                                        }
                                        nmea_idx = 0;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (firstGpsTime && gps.time.isValid())
            {
                if (gps.time.isUpdated())
                {
                    time_t timeGps = getGpsTime(); // Local gps time
                    if (timeGps > 1700000000 && timeGps < 2347462800)
                    {
                        setTime(timeGps);
                        time_t rtc = timeGps;
                        timeval tv = {rtc, 0};
                        timezone tz = {TZ_SEC + DST_MN, 0};
                        settimeofday(&tv, &tz);
#ifdef DEBUG
                        log_d("\nSET GPS Timestamp = %u Year=%d\n", timeGps, year());
#endif
                        // firstGpsTime = false;
                        firstGpsTime = false;
                        if (startTime == 0)
                            startTime = now();
                    }
                    else
                    {
                        startTime = 0;
                    }
                }
            }
        }
    }
}

void taskGPS(void *pvParameters)
{
    int c;
    log_d("GNSS Init");
    nmea_idx = 0;

    if (config.gnss_enable)
    {
        if ((config.gnss_channel > 0) && (config.gnss_channel < 4))
        {
            if (strstr("AT", config.gnss_at_command) >= 0)
            {
                if (config.gnss_channel == 1)
                {
                    Serial.println(config.gnss_at_command);
                }
                else if (config.gnss_channel == 2)
                {
                    Serial1.println(config.gnss_at_command);
                }
                else if (config.gnss_channel == 3)
                {
                    Serial2.println(config.gnss_at_command);
                }
            }
        }
    }
    for (;;)
    {
        timerGPS = micros() - timerGPS_old;
        vTaskDelay(10 / portTICK_PERIOD_MS);
        timerGPS_old = micros();

        if (config.gnss_enable)
        {
            if ((config.gnss_channel > 0) && (config.gnss_channel < 4))
            {
                do
                {
                    c = -1;
                    if (config.gnss_channel == 1)
                    {
                        c = Serial.read();
                    }
                    else if (config.gnss_channel == 2)
                    {
                        c = Serial1.read();
                    }
                    else if (config.gnss_channel == 3)
                    {
                        c = Serial2.read();
                    }
                    if (c > -1)
                    {
                        gps.encode((char)c);
                        if (webServiceBegin == false)
                        {
                            if (nmea_idx > 99)
                            {
                                nmea_idx = 0;
                                memset(nmea, 0, sizeof(nmea));
                                // SerialGNSS->flush();
                            }
                            else
                            {
                                nmea[nmea_idx++] = (char)c;
                                if ((char)c == '\r' || (char)c == '\n')
                                {
                                    //nmea[nmea_idx++] = 0;
                                    if (nmea_idx > 10)
                                    {
                                        if (ws_gnss.enabled() && !ws_gnss.getClients().isEmpty())
                                        // if (webServiceBegin == false)
                                        {
                                            handle_ws_gnss(nmea, nmea_idx);
                                        }
                                        // log_d("%s",nmea);
                                    }
                                    nmea_idx = 0;
                                    memset(nmea, 0, sizeof(nmea));
                                    vTaskDelay(50 / portTICK_PERIOD_MS);
                                    break;
                                }
                            }
                        }
                        //}
                    }
                    else
                    {
                        break;
                    }
                } while (1);
            }
            else if (config.gnss_channel == 4)
            { // TCP
                if (WiFi.isConnected())
                {
                    if (!gnssClient.connected())
                    {
                        gnssClient.connect(config.gnss_tcp_host, config.gnss_tcp_port);
                        log_d("GNSS TCP ReConnect to %s:%d", config.gnss_tcp_host, config.gnss_tcp_port);
                        delay(3000);
                    }
                    else
                    {
                        while (gnssClient.available())
                        {
                            c = (char)gnssClient.read();
                            // Serial.print(c);
                            gps.encode(c);
                            if (webServiceBegin == false)
                            {
                                if (nmea_idx > 99)
                                {
                                    nmea_idx = 0;
                                    memset(nmea, 0, sizeof(nmea));
                                }
                                else
                                {
                                    //nmea[nmea_idx++] = c;
                                    if (c == '\r' || c == '\n')
                                    {
                                        nmea[nmea_idx++] = 0;
                                        if (nmea_idx > 10)
                                        {
                                            if (ws_gnss.enabled() && !ws_gnss.getClients().isEmpty())
                                            {
                                                handle_ws_gnss(nmea, nmea_idx);
                                            }
                                            // log_d("%s",nmea);
                                        }
                                        nmea_idx = 0;
                                        memset(nmea, 0, sizeof(nmea));
                                        vTaskDelay(50 / portTICK_PERIOD_MS);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (firstGpsTime && gps.time.isValid())
            {
                if (gps.time.isUpdated())
                {
                    time_t timeGps = getGpsTime(); // Local gps time
                    if (timeGps > 1700000000 && timeGps < 2347462800)
                    {
                        setTime(timeGps);
                        time_t rtc = timeGps;
                        timeval tv = {rtc, 0};
                        timezone tz = {TZ_SEC + DST_MN, 0};
                        settimeofday(&tv, &tz);
#ifdef DEBUG
                        log_d("\nSET GPS Timestamp = %u Year=%d\n", timeGps, year());
#endif
                        // firstGpsTime = false;
                        firstGpsTime = false;
                        if (startTime == 0)
                            startTime = now();
                    }
                    else
                    {
                        startTime = 0;
                    }
                }
            }
        }
    }
}

void taskSerial(void *pvParameters)
{
    String raw;
    int c;
    char rawP[500];
    char call[11];
    log_d("Serial task Init");
    nmea_idx = 0;
    if (config.ext_tnc_enable)
    {
        if (config.ext_tnc_channel == 1)
        {
            Serial.setTimeout(10);
        }
        else if (config.ext_tnc_channel == 2)
        {
            Serial1.setTimeout(10);
        }
        else if (config.ext_tnc_channel == 3)
        {
            Serial2.setTimeout(10);
        }
    }
    if (config.wx_en)
    {
        if (config.wx_channel == 1)
        {
            Serial.setTimeout(10);
        }
        else if (config.wx_channel == 2)
        {
            Serial1.setTimeout(10);
        }
        else if (config.wx_channel == 3)
        {
            Serial2.setTimeout(10);
        }
    }
    for (;;)
    {
        timerSerial = millis() - timerSerial_old;
        vTaskDelay(10 / portTICK_PERIOD_MS);
        timerSerial_old = micros();

        if (config.wx_en)
        {
            if (config.wx_channel > 0 && config.wx_channel < 4)
            {
                String wx = "";
                if (config.wx_channel == 1)
                {
                    wx = Serial.readString();
                }
                else if (config.wx_channel == 2)
                {
                    wx = Serial1.readString();
                }
                else if (config.wx_channel == 3)
                {
                    wx = Serial2.readString();
                }
                // if (wx!="")
                //{
                // while (SerialWX->available())
                //{
                // String wx = SerialWX->readString();
                if (wx != "" && wx.indexOf("MQTT:") >= 0)
                {
                    log_d("WX Raw >> %d", wx.c_str());
                    getCSV2Wx(wx);
                }
                //}
                //}
            }
            // else if(config.wx_channel == 4){
            //     bool result=getM702Modbus(modbus);
            // }
        }

        if (config.ext_tnc_enable && (config.ext_tnc_mode > 0 && config.ext_tnc_mode < 4))
        {
            if (config.ext_tnc_mode == 1)
            { // KISS
                // KISS MODE
                do
                {
                    c = -1;
                    if (config.ext_tnc_channel == 1)
                    {
                        c = Serial.read();
                    }
                    else if (config.ext_tnc_channel == 2)
                    {
                        c = Serial1.read();
                    }
                    else if (config.ext_tnc_channel == 3)
                    {
                        c = Serial2.read();
                    }

                    if (c > -1)
                        kiss_serial((uint8_t)c);
                    else
                        break;
                } while (c > -1);
            }
            else if (config.ext_tnc_mode == 2)
            { // TNC2RAW
                raw.clear();
                if (config.ext_tnc_channel == 1)
                {
                    raw = Serial.readStringUntil(0x0D);
                }
                else if (config.ext_tnc_channel == 2)
                {
                    raw = Serial1.readStringUntil(0x0D);
                }
                else if (config.ext_tnc_channel == 3)
                {
                    raw = Serial2.readStringUntil(0x0D);
                }

                log_d("Ext TNC2RAW RX:%s", raw.c_str());
                String src_call = raw.substring(0, raw.indexOf('>'));
                if ((src_call != "") && (src_call.length() < 10) && (raw.length() < sizeof(rawP)))
                {
                    memset(call, 0, sizeof(call));
                    strcpy(call, src_call.c_str());
                    strcpy(rawP, raw.c_str());
                    uint16_t type = pkgType((const char *)rawP);
                    pkgListUpdate(call, rawP,raw.length(), type, 1);
                    if (config.rf2inet && aprsClient.connected())
                    {
                        // RF->INET
                        aprsClient.write(&rawP[0], strlen(rawP)); // Send binary frame packet to APRS-IS (aprsc)
                        aprsClient.write("\r\n");                 // Send CR LF the end frame packet
                        status.rf2inet++;
                        igateTLM.RF2INET++;
                        igateTLM.RX++;
                    }
                }
            }
            else if (config.ext_tnc_mode == 3)
            { // YAESU FTM-350,FTM-400
                String info = "";
                if (config.ext_tnc_channel == 1)
                {
                    info = Serial.readString();
                }
                else if (config.ext_tnc_channel == 2)
                {
                    info = Serial1.readString();
                }
                else if (config.ext_tnc_channel == 3)
                {
                    info = Serial2.readString();
                }

                //  log_d("Ext Yaesu Packet >> %s",info.c_str());
                int ed = info.indexOf(" [");
                if (info != "" && ed > 10)
                {
                    raw.clear();
                    raw = info.substring(0, ed);
                    int st = info.indexOf(">:");
                    if (st > ed)
                    {
                        int idx = 0;
                        st += 2;
                        for (int i = 0; i < 5; i++)
                        {
                            if (info.charAt(st + i) == 0x0A || info.charAt(st + i) == 0x0D)
                            {
                                idx++;
                            }
                            else
                            {
                                break;
                            }
                        }
                        st += idx;
                        ed = info.indexOf(0x0D, st + 1);
                        if (ed > info.length())
                            ed = info.length();
                        if (ed > st)
                        {
                            raw += ":" + info.substring(st, ed);

                            String src_call = raw.substring(0, raw.indexOf('>'));
                            if ((src_call != "") && (src_call.length() < 11) && (raw.length() < sizeof(rawP)))
                            {
                                memset(call, 0, sizeof(call));
                                strcpy(call, src_call.c_str());
                                memset(rawP, 0, sizeof(rawP));
                                strcpy(rawP, raw.c_str());
                                log_d("Yaesu Packet: CallSign:%s RAW:%s", call, rawP);
                                // String hstr="";
                                // for(int i=0;i<raw.length();i++){
                                //     hstr+=" "+String(rawP[i],HEX);
                                // }
                                // log_d("HEX: %s",hstr.c_str());
                                uint16_t type = pkgType((const char *)rawP);
                                pkgListUpdate(call, rawP,raw.length(), type, 1);
                                if (config.rf2inet && aprsClient.connected())
                                {
                                    // RF->INET
                                    aprsClient.write(&rawP[0], strlen(rawP)); // Send binary frame packet to APRS-IS (aprsc)
                                    aprsClient.write("\r\n");                 // Send CR LF the end frame packet
                                    status.rf2inet++;
                                    igateTLM.RF2INET++;
                                    igateTLM.RX++;
                                }
                            }
                        }
                    }
                }
            }
            //}
        }
    }
}

long timeSlot;
unsigned long iGatetickInterval;
bool initInterval = true;
void taskAPRS(void *pvParameters)
{
    //	long start, stop;
    char sts[50];
    // char *raw;
    // char *str;
    unsigned long tickInterval = 0;
    unsigned long DiGiInterval = 0;

    log_d("Task APRS has been start");
    PacketBuffer.clean();

    afskSetModem(config.modem_type);
    afskSetSQL(config.rf_sql_gpio, config.rf_sql_active);
    afskSetPTT(config.rf_ptt_gpio, config.rf_ptt_active);
    afskSetPWR(config.rf_pwr_gpio, config.rf_pwr_active);
    afskSetDCOffset(config.adc_dc_offset);
    afskSetADCAtten(config.adc_atten);
    APRS_init();
    APRS_setCallsign(config.aprs_mycall, config.aprs_ssid);
    // APRS_setPath1("WIDE1-1", 1);
    APRS_setPreamble(300);
    APRS_setTail(0);
    sendTimer = millis() - (config.igate_interval * 1000) + 30000;
    igateTLM.TeleTimeout = millis() + 60000; // 1Min

    timeSlot = millis();
    timeAprs = 0;
    afskSetHPF(config.audio_hpf);
    afskSetBPF(config.audio_bpf);
    timeSlot = millis();
    tx_interval = config.trk_interval;
    tx_counter = tx_interval - 10;
    initInterval = true;
    AFSKInitAct = true;
    for (;;)
    {
        long now = millis();
        // wdtSensorTimer = now;
        time_t timeStamp;
        time(&timeStamp);
        if (initInterval)
        {
            tickInterval = DiGiInterval = iGatetickInterval = millis() + 10000;
            systemTLM.ParmTimeout = millis() + 20000;
            systemTLM.TeleTimeout = millis() + 30000;
            initInterval = false;
            tx_interval = config.trk_interval;
            tx_counter = tx_interval - 10;
        }
        timerAPRS = micros() - timerAPRS_old;
        vTaskDelay(10 / portTICK_PERIOD_MS);
        timerAPRS_old = micros();

        // if (config.rf_en)
        { // RF Module enable
            // SEND RF in time slot
            if (now > (timeSlot + 10))
            {
                // Transmit in timeslot if enabled
                // if (config.rf_sql_gpio > -1)
                // { // Set SQL pin
                //     if (digitalRead(config.rf_sql_gpio) ^ config.rf_sql_active)
                //     { // RX State Fail
                //         if (pkgTxSend())
                //             timeSlot = millis() + config.tx_timeslot; // Tx Time Slot >2sec.
                //         else
                //             timeSlot = millis() + 3000;
                //     }
                //     else
                //     {
                //         timeSlot = millis() + 1500;
                //     }
                // }
                // else
                if (pkgTxSend())
                    timeSlot = millis() + config.tx_timeslot; // Tx Time Slot > 2sec.
                else
                    timeSlot = millis() + 3000;
            }
        }

        if (config.trk_en)
        { // TRACKER MODE

            if (millis() > tickInterval)
            {
                tickInterval = millis() + 1000;

                tx_counter++;
                // log_d("TRACKER tx_counter=%d\t INTERVAL=%d\n", tx_counter, tx_interval);
                //   Check interval timeout
                if (config.trk_smartbeacon && config.trk_gps)
                {
                    if (tx_counter > tx_interval)
                    {
                        if (tx_counter > config.trk_mininterval)
                            EVENT_TX_POSITION = 4;
                    }
                    else
                    {
                        if (tx_counter >= (tx_interval + 5))
                        {
                            EVENT_TX_POSITION = 5;
                        }
                    }
                }
                else if (tx_counter > tx_interval)
                {
                    EVENT_TX_POSITION = 6;
                    tx_interval = config.trk_interval;
                }

                // if (config.trk_gps && gps.speed.isValid() && gps.location.isValid() && gps.course.isValid() && (gps.hdop.hdop() < 10.0) && (gps.satellites.value() > 3))
                // if (config.trk_gps && gps.speed.isValid() && gps.location.isValid() && gps.course.isValid())
                if (config.trk_gps)
                {
                    SB_SPEED_OLD = SB_SPEED;
                    // if (gps.speed.isUpdated())
                    SB_SPEED = (unsigned char)gps.speed.kmph();
                    // if (gps.course.isUpdated())
                    if (gps.speed.kmph() > config.trk_lspeed)
                        SB_HEADING = (int16_t)gps.course.deg();
                    if (config.trk_smartbeacon) // SMART BEACON CAL
                    {
                        if (SB_SPEED < config.trk_lspeed && SB_SPEED_OLD > config.trk_lspeed) // Speed slow down to STOP
                        {                                                                     // STOPING
                            SB_SPEED_OLD = 0;
                            if (tx_counter > config.trk_mininterval)
                                EVENT_TX_POSITION = 7;
                            tx_interval = config.trk_slowinterval;
                        }
                        else
                        {
                            smartbeacon();
                        }
                    }
                    else if (tx_counter > tx_interval)
                    { // send gps location
                        EVENT_TX_POSITION = 8;
                        tx_interval = config.trk_interval;
                    }
                }
            }

            if (EVENT_TX_POSITION > 0)
            {
                String rawData;
                String cmn = "";
                if (config.trk_sat)
                    cmn += "SAT:" + String(gps.satellites.value()) + ",HDOP:" + String(gps.hdop.hdop(), 1);
                // if (config.trk_bat)
                // {
                // if (config.trk_sat)
                //     cmn += ",";
                // // cmn += "BAT:" + String((float)PMU.getBattVoltage() / 1000, 1) + "V";
                // cmn += "BAT:" + String(vbat, 1) + "V";
                // }
                if (config.trk_gps) // TRACKER by GPS
                {
                    rawData = trk_gps_postion(cmn);
                }
                else // TRACKER by FIX position
                {
                    rawData = trk_fix_position(cmn);
                }

                log_d("TRACKER RAW: %s\n", rawData.c_str());
                log_d("TRACKER EVENT_TX_POSITION=%d\t INTERVAL=%d\n", EVENT_TX_POSITION, tx_interval);
                tx_counter = 0;
                EVENT_TX_POSITION = 0;
                last_heading = SB_HEADING;

                if (config.trk_gps)
                {
                    // if (gps.location.isValid() && (gps.hdop.hdop() < 10.0))
                    sprintf(sts, "POSITION GPS\nSPD %dkPh/%d\nINTERVAL %ds", SB_SPEED, SB_HEADING, tx_interval);
                    // else
                    //     sprintf(sts, "POSITION GPS\nGPS INVALID\nINTERVAL %ds", tx_interval);
                }
                else
                {
                    sprintf(sts, "POSITION FIX\nINTERVAL %ds", tx_interval);
                }

                if (config.trk_loc2rf)
                { // TRACKER SEND TO RF
                    char *rawP = (char *)calloc(rawData.length(),sizeof(char));
                    memcpy(rawP, rawData.c_str(), rawData.length());
                    pkgTxPush(rawP, rawData.length(), 0);
#ifdef OLED
                    pushTxDisp(TXCH_RF, "TX TRACKER", sts);
#endif
                    free(rawP);
                }
                if (config.trk_loc2inet)
                { // TRACKER SEND TO APRS-IS
                    if (aprsClient.connected())
                    {
                        aprsClient.println(rawData); // Send packet to Inet
#ifdef OLED
                        pushTxDisp(TXCH_TCP, "TX TRACKER", sts);
#endif
                    }
                }
            }
        }

        // LOAD DATA incomming
        bool newIGatePkg = false;
        bool newDigiPkg = false;
        if (PacketBuffer.getCount() > 0)
        {
            String tnc2;
            // นำข้อมูลแพ็จเกจจาก TNC ออกจากคิว
            PacketBuffer.pop(&incomingPacket);
            packet2Raw(tnc2, incomingPacket);           
            //log_d("incommingPacket: %s",tnc2.c_str()); 
            newIGatePkg = true;
            newDigiPkg = true;
            if (config.ext_tnc_enable)
            {
                if (config.ext_tnc_channel > 0 && config.ext_tnc_channel < 3)
                {
                    if (config.ext_tnc_mode == 1)
                    {
                        // KISS MODE
                        uint8_t pkg[500];
                        int sz = kiss_wrapper(pkg);
                        if (config.ext_tnc_channel == 1)
                        {
                            Serial.write(pkg, sz);
                        }
                        else if (config.ext_tnc_channel == 2)
                        {
                            Serial1.write(pkg, sz);
                        }
                        else if (config.ext_tnc_channel == 3)
                        {
                            Serial2.write(pkg, sz);
                        }
                    }
                    else if (config.ext_tnc_mode == 2)
                    {
                        // TNC2
                        if (config.ext_tnc_channel == 1)
                        {
                            Serial.println(tnc2);
                        }
                        else if (config.ext_tnc_channel == 2)
                        {
                            Serial1.println(tnc2);
                        }
                        else if (config.ext_tnc_channel == 3)
                        {
                            Serial2.println(tnc2);
                        }
                    }
                }
            }
#ifdef BLUETOOTH
            if (config.bt_master)
            { // Output TNC2RAW to BT Serial
              // SerialBT.println(tnc2);
                if (config.bt_mode == 1)
                {
                    char *rawP = (char *)malloc(tnc2.length());
                    memcpy(rawP, tnc2.c_str(), tnc2.length());
                    SerialBT.write((uint8_t *)rawP, tnc2.length());
                    // pTxCharacteristic->setValue((uint8_t *)rawP, tnc2.length());
                    // pTxCharacteristic->notify();
                    free(rawP);
                }
                else if (config.bt_mode == 2)
                { // KISS
                    uint8_t pkg[500];
                    int sz = kiss_wrapper(pkg);
                    SerialBT.write(pkg, sz);
                    // pTxCharacteristic->setValue(pkg, sz);
                    // pTxCharacteristic->notify();
                }
            }
#endif
            log_d("RX: %s", tnc2.c_str());

            // SerialBT.println(tnc2);
            uint16_t type = pkgType((char *)incomingPacket.info);
            char call[11];
            if (incomingPacket.src.ssid > 0)
                sprintf(call, "%s-%d", incomingPacket.src.call, incomingPacket.src.ssid);
            else
                sprintf(call, "%s", incomingPacket.src.call);

            char *rawP = (char *)calloc(tnc2.length(),sizeof(char));
            memcpy(rawP, tnc2.c_str(), tnc2.length());
            int idx = pkgListUpdate(call, rawP,tnc2.length(), type, 0);
            free(rawP);
#ifdef OLED
            if (idx > -1)
            {

                if (config.rx_display && config.dispRF && (type & config.dispFilter))
                {
                    dispBuffer.push(tnc2.c_str());
                    log_d("RF_putQueueDisp:[pkgList_idx=%d,Type=%d] %s\n", idx, type, call);
                }
            }
#endif
            lastPkg = true;
            lastPkgRaw = tnc2;
            tnc2.clear();            
            handle_ws();
            // ESP_BT.println(tnc2);
            status.allCount++;
        }

        // IGate Process
        if (config.igate_en)
        {
            // IGATE Position
            if (config.igate_bcn)
            {
                if (millis() > iGatetickInterval)
                {

                    String rawData = "";
                    if (config.igate_gps)
                    { // IGATE Send GPS position
                        if (gps.location.isValid())
                            rawData = igate_position(gps.location.lat(), gps.location.lng(), gps.altitude.meters(), "");
                    }
                    else
                    { // IGATE Send fix position
                        rawData = igate_position(config.igate_lat, config.igate_lon, config.igate_alt, "");
                    }
                    if (rawData != "")
                    {
                        iGatetickInterval = millis() + (config.igate_interval * 1000);
                        log_d("IGATE_POSITION: %s", rawData.c_str());

                        if (config.igate_gps)
                            sprintf(sts, "POSITION GPS\nINTERVAL %ds", tx_interval);
                        else
                            sprintf(sts, "POSITION FIX\nINTERVAL %ds", tx_interval);
                        if (config.igate_loc2rf)
                        { // IGATE SEND POSITION TO RF
                            char *rawP = (char *)calloc(rawData.length(),sizeof(char));
                            // rawData.toCharArray(rawP, rawData.length());
                            memcpy(rawP, rawData.c_str(), rawData.length());
                            pkgTxPush(rawP, rawData.length(), 0);
#ifdef OLED
                            pushTxDisp(TXCH_RF, "TX IGATE", sts);
#endif
                            free(rawP);
                        }
                        if (config.igate_loc2inet)
                        { // IGATE SEND TO APRS-IS
                            if (aprsClient.connected())
                            {
                                status.txCount++;
                                aprsClient.println(rawData); // Send packet to Inet
#ifdef OLED
                                pushTxDisp(TXCH_TCP, "TX IGATE", sts);
#endif
                            }
                        }
                    }
                }
            }
            // IGATE send to inet
            if (newIGatePkg)
            {
                newIGatePkg = false;
                if (config.rf2inet && aprsClient.connected())
                {
                    int ret = 0;
                    uint16_t type = pkgType((const char *)&incomingPacket.info[0]);
                    // IGate Filter RF->INET
                    if ((type & config.rf2inetFilter)){
                        ret = igateProcess(incomingPacket);                        
                    }
                    if (ret == 0)
                    {
                        status.dropCount++;
                        igateTLM.DROP++;
                    }
                    else
                    {
                        status.rf2inet++;
                        igateTLM.RF2INET++;
                        igateTLM.TX++;
                    }
                }
            }
        }

        // Digi Repeater Process
        if (config.digi_en)
        {
            // DIGI Position
            if (config.digi_bcn)
            {
                if (millis() > DiGiInterval)
                {

                    String rawData;
                    if (config.digi_gps)
                    { // DIGI Send GPS position
                        if (gps.location.isValid())
                            rawData = digi_position(gps.location.lat(), gps.location.lng(), gps.altitude.meters(), "");
                    }
                    else
                    { // DIGI Send fix position
                        rawData = digi_position(config.digi_lat, config.digi_lon, config.digi_alt, "");
                    }
                    if (rawData != "")
                    {
                        DiGiInterval = millis() + (config.digi_interval * 1000);
                        log_d("DIGI_POSITION: %s", rawData.c_str());

                        if (config.digi_gps)
                            sprintf(sts, "POSITION GPS\nINTERVAL %ds", tx_interval);
                        else
                            sprintf(sts, "POSITION FIX\nINTERVAL %ds", tx_interval);
                        if (config.digi_loc2rf)
                        { // DIGI SEND POSITION TO RF
                            char *rawP = (char *)calloc(rawData.length(),sizeof(char));
                            // rawData.toCharArray(rawP, rawData.length());
                            memcpy(rawP, rawData.c_str(), rawData.length());
                            pkgTxPush(rawP, rawData.length(), 0);
#ifdef OLED
                            pushTxDisp(TXCH_RF, "TX DIGI POS", sts);
#endif
                            free(rawP);
                        }
                        if (config.digi_loc2inet)
                        { // DIGI SEND TO APRS-IS
                            if (aprsClient.connected())
                            {
                                status.txCount++;
                                aprsClient.println(rawData); // Send packet to Inet
#ifdef OLED
                                pushTxDisp(TXCH_TCP, "TX DIGI POS", sts);
#endif
                            }
                        }
                    }
                }
            }

            // Repeater packet
            if (newDigiPkg)
            {
                newDigiPkg = false;
                uint16_t type = pkgType((const char *)&incomingPacket.info[0]);
                // Digi repeater filter
                if ((type & config.digiFilter))
                {
                    // Packet recheck
                    pkgTxDuplicate(incomingPacket); // Search duplicate in tx and drop packet for renew
                    int dlyFlag = digiProcess(incomingPacket);
                    if (dlyFlag > 0)
                    {
                        int digiDelay;
                        status.digiCount++;
                        if (dlyFlag == 1)
                        {
                            digiDelay = 0;
                        }
                        else
                        {
                            if (config.digi_delay == 0)
                            { // Auto mode
                              // if (digiCount > 20)
                              //   digiDelay = random(5000);
                              // else if (digiCount > 10)
                              //   digiDelay = random(3000);
                              // else if (digiCount > 0)
                              //   digiDelay = random(1500);
                              // else
                                digiDelay = random(100);
                            }
                            else
                            {
                                digiDelay = random(config.digi_delay);
                            }
                        }

                        String digiPkg;
                        packet2Raw(digiPkg, incomingPacket);
                        log_d("DIGI_REPEAT: %s", digiPkg.c_str());
                        log_d("DIGI delay=%d ms.", digiDelay);
                        char *rawP = (char *)calloc(digiPkg.length(),sizeof(char));
                        // digiPkg.toCharArray(rawP, digiPkg.length());
                        memcpy(rawP, digiPkg.c_str(), digiPkg.length());
                        pkgTxPush(rawP, digiPkg.length(), digiDelay);
                        sprintf(sts, "--src call--\n%s\nDelay: %dms.", incomingPacket.src.call, digiDelay);
#ifdef OLED
                        pushTxDisp(TXCH_DIGI, "DIGI REPEAT", sts);
#endif
                        free(rawP);
                    }
                }
            }
        }

        if (config.tlm0_en)
        {
            if (systemTLM.ParmTimeout < millis())
            {
                systemTLM.ParmTimeout = millis() + (config.tlm0_info_interval * 1000);
                char rawInfo[100];
                char name[10];
                sprintf(rawInfo, "PARM.");
                for (int i = 0; i < 13; i++)
                {
                    if (i > 0)
                        strcat(rawInfo, ",");
                    sprintf(name, "%s", config.tlm0_PARM[i]);
                    strcat(rawInfo, name);
                }
                sendTelemetry_0(rawInfo, true);
                memset(rawInfo, 0, sizeof(rawInfo));
                sprintf(rawInfo, "UNIT.");
                for (int i = 0; i < 13; i++)
                {
                    if (i > 0)
                        strcat(rawInfo, ",");
                    sprintf(name, "%s", config.tlm0_UNIT[i]);
                    strcat(rawInfo, name);
                }
                sendTelemetry_0(rawInfo, true);
                memset(rawInfo, 0, sizeof(rawInfo));
                sprintf(rawInfo, "EQNS.");
                for (int i = 0; i < 5; i++)
                {
                    if (i > 0)
                        strcat(rawInfo, ",");
                    if (fmod(config.tlm0_EQNS[i][0], 1) == 0)
                        sprintf(name, "%0.f", config.tlm0_EQNS[i][0]);
                    else
                        sprintf(name, "%.3f", config.tlm0_EQNS[i][0]);
                    strcat(rawInfo, name);
                    if (fmod(config.tlm0_EQNS[i][1], 1) == 0)
                        sprintf(name, ",%0.f", config.tlm0_EQNS[i][1]);
                    else
                        sprintf(name, ",%.3f", config.tlm0_EQNS[i][1]);
                    strcat(rawInfo, name);
                    if (fmod(config.tlm0_EQNS[i][2], 1) == 0)
                        sprintf(name, ",%0.f", config.tlm0_EQNS[i][2]);
                    else
                        sprintf(name, ",%.3f", config.tlm0_EQNS[i][2]);
                    strcat(rawInfo, name);
                }
                sendTelemetry_0(rawInfo, true);
                memset(rawInfo, 0, sizeof(rawInfo));
                sprintf(rawInfo, "BITS.");
                uint8_t b = 1;
                for (int i = 0; i < 8; i++)
                {
                    if (config.tlm0_BITS_Active & b)
                    {
                        strcat(rawInfo, "1");
                    }
                    else
                    {
                        strcat(rawInfo, "0");
                    }
                    b <<= 1;
                }
                strcat(rawInfo, ",");
                strcat(rawInfo, config.tlm0_comment);
                sendTelemetry_0(rawInfo, true);
            }

            if (systemTLM.TeleTimeout < millis())
            {
                systemTLM.TeleTimeout = millis() + (config.tlm0_data_interval * 1000);
                char rawTlm[100];
                if (systemTLM.Sequence > 999)
                    systemTLM.Sequence = 0;
                else
                    systemTLM.Sequence++;
                getTelemetry_0();
                sprintf(rawTlm, "T#%03d,%03d,%03d,%03d,%03d,%03d,", systemTLM.Sequence, systemTLM.A1, systemTLM.A2, systemTLM.A3, systemTLM.A4, systemTLM.A5);
                uint8_t b = 1;
                for (int i = 0; i < 8; i++)
                {
                    if (!((systemTLM.BITS & b) ^ (config.tlm0_BITS_Active & b)))
                    {
                        strcat(rawTlm, "1");
                    }
                    else
                    {
                        strcat(rawTlm, "0");
                    }
                    b <<= 1;
                }
                sendTelemetry_0(rawTlm, false);
            }
        }
    }
}

void taskAPRSPoll(void *pvParameters)
{
    for (;;)
    {
        vTaskDelay(5 / portTICK_PERIOD_MS);

        if (AFSKInitAct == true)
        {
            AFSK_Poll(false, LOW);
        }
    }
}

int mqttRetry = 0;
long wifiTTL = 0;

// WiFi connect timeout per AP. Increase when connecting takes longer.
const uint32_t connectTimeoutMs = 30000;
uint8_t APStationNum = 0;

void taskNetwork(void *pvParameters)
{
    char raw[300];
    log_d("Task Network has been start");

    // WiFi.onEvent(Wifi_connected,SYSTEM_EVENT_STA_CONNECTED);
    // WiFi.onEvent(Get_IPAddress, SYSTEM_EVENT_STA_GOT_IP);
    // WiFi.onEvent(Wifi_disconnected, SYSTEM_EVENT_STA_DISCONNECTED);

    if (config.wifi_mode == WIFI_STA_FIX)
    { /**< WiFi station mode */
        WiFi.mode(WIFI_MODE_STA);
        WiFi.setTxPower((wifi_power_t)config.wifi_power);
    }
    else if (config.wifi_mode == WIFI_AP_FIX)
    { /**< WiFi soft-AP mode */
        WiFi.mode(WIFI_MODE_AP);
        WiFi.setTxPower((wifi_power_t)config.wifi_power);
    }
    else if (config.wifi_mode == WIFI_AP_STA_FIX)
    { /**< WiFi station + soft-AP mode */
        WiFi.mode(WIFI_MODE_APSTA);
        WiFi.setTxPower((wifi_power_t)config.wifi_power);
    }
    else
    {
        WiFi.mode(WIFI_MODE_NULL);
    }

    if (config.wifi_mode & WIFI_STA_FIX)
    {
        for (int i = 0; i < 5; i++)
        {
            if (config.wifi_sta[i].enable)
            {
                wifiMulti.addAP(config.wifi_sta[i].wifi_ssid, config.wifi_sta[i].wifi_pass);
            }
        }
        WiFi.setHostname("ESP32IGate");
    }

    if (config.wifi_mode & WIFI_AP_FIX)
    {
        // กำหนดค่าการทำงานไวไฟเป็นแอสเซสพ้อย
        WiFi.softAP(config.wifi_ap_ssid, config.wifi_ap_pass); // Start HOTspot removing password will disable security
        WiFi.softAPConfig(local_IP, gateway, subnet);
        Serial.print("Access point running. IP address: ");
        Serial.print(WiFi.softAPIP());
        Serial.println("");
    }

    if (wifiMulti.run() == WL_CONNECTED)
    {
        // Serial.println("");
        log_d("Wi-Fi CONNECTED!");
        log_d("IP address: %s", WiFi.localIP().toString().c_str());
        NTP_Timeout = millis() + 2000;
    }

    pingTimeout = millis() + 10000;
    unsigned long timeNetworkOld = millis();
    timeNetwork = 0;
    if (config.wifi_mode & WIFI_AP_STA_FIX)
        webService();
    for (;;)
    {
        unsigned long now = millis();
        timeNetwork = now - timeNetworkOld;
        timeNetworkOld = now;
        // wdtNetworkTimer = millis();
        serviceHandle();
        timerNetwork = micros() - timerNetwork_old;
        vTaskDelay(9 / portTICK_PERIOD_MS);
        timerNetwork_old = micros();

        // if (config.gnss_enable)
        //     taskGPSActive();

        if (config.wifi_mode & WIFI_AP_FIX)
        {
            APStationNum = WiFi.softAPgetStationNum();
            if (APStationNum > 0)
            {
                if (WiFi.isConnected() == false)
                {
                    vTaskDelay(9 / portTICK_PERIOD_MS);
                    continue;
                }
            }
        }

        if (wifiMulti.run(connectTimeoutMs) == WL_CONNECTED)
        {
            if (millis() > NTP_Timeout)
            {
                NTP_Timeout = millis() + 86400000;
                // Serial.println("Config NTP");
                // setSyncProvider(getNtpTime);
                log_d("Contacting Time Server\n");
                configTime(3600 * config.timeZone, 0, config.ntp_host);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                time_t systemTime;
                time(&systemTime);
                setTime(systemTime);
                if (systemUptime == 0)
                {
                    systemUptime = time(NULL);
                }
                pingTimeout = millis() + 2000;
            }

            if (config.igate_en)
            {
                if (aprsClient.connected() == false)
                {
                    APRSConnect();
                    if (config.igate_bcn)
                    {
                        iGatetickInterval = millis() + 10000; // send position after 10sec
                    }
                }
                else
                {
                    if (aprsClient.available())
                    {
                        pingTimeout = millis() + 300000;                // Reset ping timout
                        String line = aprsClient.readStringUntil('\n'); // อ่านค่าที่ Server ตอบหลับมาทีละบรรทัด
#ifdef DEBUG_IS
                        printTime();
                        Serial.print("APRS-IS ");
                        Serial.println(line);
#endif
                        status.isCount++;
                        int start_val = line.indexOf(">", 0); // หาตำแหน่งแรกของ >
                        if (start_val > 3)
                        {
                            String src_call = line.substring(0, start_val);
                            String msg_call = "::" + src_call;

                            status.allCount++;
                            status.rxCount++;
                            igateTLM.RX++;
                            
                            memset(&raw[0], 0, sizeof(raw));
                            start_val = line.indexOf(":", 10); // Search of info in ax25
                            if (start_val > 5)
                            {
                                String info = line.substring(start_val + 1);
                                // info.toCharArray(&raw[0], info.length(), 0);
                                memcpy(raw, info.c_str(), info.length());

                                uint16_t type = pkgType(&raw[0]);
                                int start_dstssid = line.indexOf("-", 1); // get SSID -
                                if (start_dstssid < 0)
                                    start_dstssid = line.indexOf(" ", 1); // get ssid space
                                char ssid = 0;
                                if (start_dstssid > 0)
                                    ssid = line.charAt(start_dstssid + 1);

                                if ((ssid > 47 && ssid < 58) && (type & config.inet2rfFilter))
                                {
                                    log_d("INET: %s\n", line.c_str());
                                    size_t len = src_call.length();
                                    char call[15];
                                    memset(call, 0, sizeof(call));
                                    if (len > 15)
                                        len = 15;
                                    memcpy(call, src_call.c_str(), len);
                                    call[14] = 0;
                                    memset(raw, 0, sizeof(raw));
                                    memcpy(raw, line.c_str(), line.length());
                                    raw[sizeof(raw) - 1] = 0;
                                    int idx = pkgListUpdate(call, raw,line.length(), type, 1);
#ifdef OLED
                                    if (idx > -1)
                                    {
                                        // Put queue affter filter for display popup
                                        if (config.rx_display && config.dispINET && (type & config.dispFilter))
                                        {
                                            dispBuffer.push(line.c_str());
                                            log_d("INET_putQueueDisp:[pkgList_idx=%d/queue=%d,Type=%d] %s\n", idx, dispBuffer.getCount(), type, call);
                                        }
                                    }
#endif
                                    // INET2RF affter filter
                                    if (config.inet2rf)
                                    {
                                        if (type & config.inet2rfFilter)
                                        {
                                            char strtmp[300];
                                            String tnc2Raw = "";
                                            if (config.aprs_ssid == 0)
                                                sprintf(strtmp, "%s>APE32I", config.aprs_mycall);
                                            else
                                                sprintf(strtmp, "%s-%d>APE32I", config.aprs_mycall, config.aprs_ssid);
                                            tnc2Raw = String(strtmp);
                                            tnc2Raw += ",RFONLY"; // fix path to rf only not send loop to inet
                                            tnc2Raw += ":}";      // 3rd-party frame
                                            tnc2Raw += line;
                                            pkgTxPush(tnc2Raw.c_str(), tnc2Raw.length(), 0);
                                            char sts[50];
                                            sprintf(sts, "--SRC CALL--\n%s\n", src_call.c_str());
#ifdef OLED
                                            pushTxDisp(TXCH_3PTY, "TX INET->RF", sts);
#endif
                                            status.inet2rf++;
                                            igateTLM.INET2RF++;
                                            log_d("INET2RF: %s\n", line);
                                        }
                                    }
                                }
                            }
                        }
                        // free(raw);
                    }
                }
            }

            if (millis() > pingTimeout)
            {
                pingTimeout = millis() + 300000;
                log_d("Ping GW to %s\n", WiFi.gatewayIP().toString().c_str());
                if (ping_start(WiFi.gatewayIP(), 3, 0, 0, 5) == true)
                {
                    log_d("GW Success!!\n");
                }
                else
                {
                    log_d("GW Fail!\n");
                    WiFi.disconnect();
                    wifiMulti.run(5000);
                    wifiTTL = 0;
                }
            }
        }
    } // for loop
}

#ifdef OLED
// Routine
void line_angle(signed int startx, signed int starty, unsigned int length, unsigned int angle, unsigned int color)
{
    display.drawLine(startx, starty, (startx + length * cosf(angle * 0.017453292519)), (starty + length * sinf(angle * 0.017453292519)), color);
}

int xSpiGlcdSelFontHeight = 8;
int xSpiGlcdSelFontWidth = 5;

void compass_label(signed int startx, signed int starty, unsigned int length, double angle, unsigned int color)
{
    double angleNew;
    // ushort Color[2];
    uint8_t x_N, y_N, x_S, y_S;
    int x[4], y[4], i;
    int xOffset, yOffset;
    yOffset = (xSpiGlcdSelFontHeight / 2);
    xOffset = (xSpiGlcdSelFontWidth / 2);
    // GLCD_WindowMax();
    angle += 270.0F;
    angleNew = angle;
    for (i = 0; i < 4; i++)
    {
        if (angleNew > 360.0F)
            angleNew -= 360.0F;
        x[i] = startx + (length * cosf(angleNew * 0.017453292519));
        y[i] = starty + (length * sinf(angleNew * 0.017453292519));
        x[i] -= xOffset;
        y[i] -= yOffset;
        angleNew += 90.0F;
    }
    angleNew = angle + 45.0F;
    for (i = 0; i < 4; i++)
    {
        if (angleNew > 360.0F)
            angleNew -= 360.0F;
        x_S = startx + ((length - 3) * cosf(angleNew * 0.017453292519));
        y_S = starty + ((length - 3) * sinf(angleNew * 0.017453292519));
        x_N = startx + ((length + 3) * cosf(angleNew * 0.017453292519));
        y_N = starty + ((length + 3) * sinf(angleNew * 0.017453292519));
        angleNew += 90.0F;
        display.drawLine(x_S, y_S, x_N, y_N, color);
    }
    display.drawCircle(startx, starty, length, color);
    display.setFont();
    display.drawChar((uint8_t)x[0], (uint8_t)y[0], 'N', WHITE, BLACK, 1);
    display.drawChar((uint8_t)x[1], (uint8_t)y[1], 'E', WHITE, BLACK, 1);
    display.drawChar((uint8_t)x[2], (uint8_t)y[2], 'S', WHITE, BLACK, 1);
    display.drawChar((uint8_t)x[3], (uint8_t)y[3], 'W', WHITE, BLACK, 1);
}

void compass_arrow(signed int startx, signed int starty, unsigned int length, double angle, unsigned int color)
{
    double angle1, angle2;
    int xdst, ydst, x1sta, y1sta, x2sta, y2sta;
    int length2 = length / 2;
    angle += 270.0F;
    if (angle > 360.0F)
        angle -= 360.0F;
    xdst = startx + length * cosf(angle * 0.017453292519);
    ydst = starty + length * sinf(angle * 0.017453292519);
    angle1 = angle + 135.0F;
    if (angle1 > 360.0F)
        angle1 -= 360.0F;
    angle2 = angle + 225.0F;
    if (angle2 > 360.0F)
        angle2 -= 360.0F;
    x1sta = startx + length2 * cosf(angle1 * 0.017453292519);
    y1sta = starty + length2 * sinf(angle1 * 0.017453292519);
    x2sta = startx + length2 * cosf(angle2 * 0.017453292519);
    y2sta = starty + length2 * sinf(angle2 * 0.017453292519);
    display.drawLine(startx, starty, xdst, ydst, color);
    display.drawLine(xdst, ydst, x1sta, y1sta, color);
    display.drawLine(x1sta, y1sta, startx, starty, color);
    display.drawLine(startx, starty, x2sta, y2sta, color);
    display.drawLine(x2sta, y2sta, xdst, ydst, color);
}

unsigned long disp_delay = 0;
void dispTxWindow(txDisp txs)
{
    if (config.tx_display == false)
        return;

    display.clearDisplay();
    disp_delay = config.dispDelay * 1000;
    timeHalfSec = millis() + disp_delay;
    // send_aprs_table = txs.table;
    // send_aprs_symbol = txs.symbol;

    // display.fillRect(0, 0, 128, 16, WHITE);
    // const uint8_t *ptrSymbol;
    // uint8_t symIdx = send_aprs_symbol - 0x21;
    // if (symIdx > 95)
    //     symIdx = 0;
    // if (send_aprs_table == '/')
    // {
    //     ptrSymbol = &Icon_TableA[symIdx][0];
    // }
    // else if (send_aprs_table == '\\')
    // {
    //     ptrSymbol = &Icon_TableB[symIdx][0];
    // }
    // else
    // {
    //     if (send_aprs_table < 'A' || send_aprs_table > 'Z')
    //     {
    //         send_aprs_table = 'N';
    //         send_aprs_symbol = '&';
    //         symIdx = 5; // &
    //     }
    //     ptrSymbol = &Icon_TableB[symIdx][0];
    // }
    // display.drawYBitmap(0, 0, ptrSymbol, 16, 16, WHITE);
    // if (!(send_aprs_table == '/' || send_aprs_table == '\\'))
    // {
    //     display.drawChar(5, 4, send_aprs_table, BLACK, WHITE, 1);
    //     display.drawChar(6, 5, send_aprs_table, BLACK, WHITE, 1);
    // }

    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(0, 14);
    txs.name[sizeof(txs.name) - 1] = 0;
    if (strlen(txs.name))
    {
        display.printf("%s", txs.name);
    }

    display.setFont(&FreeSerifItalic9pt7b);

    if (txs.tx_ch == TXCH_TCP)
    {
        display.setCursor(5, 42);
        display.print("TCP");
        display.setCursor(15, 57);
        display.print("IP");
    }
    else if (txs.tx_ch == TXCH_RF)
    {
        display.setCursor(3, 42);
        display.print("SEND");
        display.setCursor(15, 57);
        display.print("RF");
    }
    else if (txs.tx_ch == TXCH_DIGI)
    {
        display.setCursor(3, 42);
        display.print("RPT");
        display.setCursor(15, 57);
        display.print("RF");
    }
    else if (txs.tx_ch == TXCH_3PTY)
    {
        display.setCursor(3, 42);
        display.print("FWD");
        display.setCursor(15, 57);
        display.print("RF");
    }

    display.setFont();
    display.setTextColor(WHITE);
    // display.setCursor(115, 0);
    // display.print("TX");

    display.drawRoundRect(0, 16, 128, 48, 5, WHITE);
    display.fillRoundRect(1, 17, 126, 10, 2, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(40, 18);
    display.print("TX STATUS");

    display.setTextColor(WHITE);
    // display.setCursor(50, 30);

    char *pch;
    int y = 30;
    pch = strtok(txs.info, "\n");
    while (pch != NULL)
    {
        display.setCursor(50, y);
        display.printf("%s", pch);
        pch = strtok(NULL, "\n");
        y += 9;
    }

    display.display();
}

const char *directions[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
bool dispPush = 0;

void dispWindow(String line, uint8_t mode, bool filter)
{
    if (config.rx_display == false)
        return;

    struct pbuf_t aprs;
    bool Monitor = false;
    char text[300];
    unsigned char x = 0;
    char itemname[10];
    int start_val = line.indexOf(":}", 10);
    if (start_val > 0)
    {
        String new_info = line.substring(start_val + 2);
        start_val = new_info.indexOf(">", 0);
        if (start_val > 3 && start_val < 12)
            line = new_info;
    }
    start_val = line.indexOf(">", 0); // หาตำแหน่งแรกของ >
    if (start_val > 3 && start_val < 12)
    {
        // powerWakeup();
        //  Serial.println(line);
        // String src_call = line.substring(0, start_val);
        // memset(&aprs, 0, sizeof(pbuf_t));
        // aprs.buf_len = 300;
        // aprs.packet_len = line.length();
        // line.toCharArray(&aprs.data[0], aprs.packet_len);
        // int start_info = line.indexOf(":", 0);
        // int end_ssid = line.indexOf(",", 0);
        // int start_dst = line.indexOf(">", 2);
        // int start_dstssid = line.indexOf("-", start_dst);
        // if ((end_ssid < 0) || (end_ssid > start_info))
        //     end_ssid = start_info;
        // if ((start_dstssid > start_dst) && (start_dstssid < start_dst + 10))
        // {
        //     aprs.dstcall_end_or_ssid = &aprs.data[start_dstssid];
        // }
        // else
        // {
        //     aprs.dstcall_end_or_ssid = &aprs.data[end_ssid];
        // }
        String src_call = line.substring(0, start_val);
				memset(&aprs, 0, sizeof(pbuf_t));
				aprs.buf_len = 300;
				aprs.packet_len = line.length();
				line.toCharArray(&aprs.data[0], aprs.packet_len);
				int start_info = line.indexOf(":", 0);
				if(start_info<10) return;
				int start_dst = line.lastIndexOf(">", start_info);
				if(start_dst<5) return;
				int end_ssid = line.indexOf(",", 10);
				if(end_ssid>start_info || end_ssid<10) end_ssid=start_info;
				
				int start_dstssid = line.lastIndexOf("-",end_ssid);
				if(start_dstssid<start_dst) start_dstssid=-1;
				String path = "";

				if ((end_ssid > start_dst) && (end_ssid < start_info))
				{
					path = line.substring(end_ssid + 1, start_info);
				}

				if (start_dstssid > start_dst)
				{
					aprs.dstcall_end_or_ssid = &aprs.data[start_dstssid+1];
				}
				else
				{
					aprs.dstcall_end_or_ssid = &aprs.data[end_ssid];
				}
        aprs.info_start = &aprs.data[start_info + 1];
        aprs.dstname = &aprs.data[start_dst + 1];
        aprs.dstname_len = end_ssid - start_dst;
        aprs.dstcall_end = &aprs.data[end_ssid];
        aprs.srccall_end = &aprs.data[start_dst];

        // Serial.println(aprs.info_start);
        // aprsParse.parse_aprs(&aprs);
        if (aprsParse.parse_aprs(&aprs))
        {
            if (filter == true)
            {
                if ((config.dispFilter & FILTER_STATUS) && (aprs.packettype & T_STATUS))
                {
                    Monitor = true;
                }
                else if ((config.dispFilter & FILTER_MESSAGE) && (aprs.packettype & T_MESSAGE))
                {
                    Monitor = true;
                }
                else if ((config.dispFilter & FILTER_TELEMETRY) && (aprs.packettype & T_TELEMETRY))
                {
                    Monitor = true;
                }
                else if ((config.dispFilter & FILTER_WX) && ((aprs.packettype & T_WX) || (aprs.packettype & T_WAVE)))
                {
                    Monitor = true;
                }

                if ((config.dispFilter & FILTER_POSITION) && (aprs.packettype & T_POSITION))
                {
                    double lat, lon;
                    if (gps.location.isValid())
                    {
                        lat = gps.location.lat();
                        lon = gps.location.lng();
                    }
                    else
                    {
                        lat = config.igate_lat;
                        lon = config.igate_lon;
                    }
                    double dist = aprsParse.distance(lon, lat, aprs.lng, aprs.lat);
                    if (config.filterDistant == 0)
                    {
                        Monitor = true;
                    }
                    else
                    {
                        if (dist < config.filterDistant)
                            Monitor = true;
                        else
                            Monitor = false;
                    }
                }

                if ((config.dispFilter & FILTER_POSITION) && (aprs.packettype & T_POSITION))
                {
                    if (aprs.flags & F_CSRSPD)
                    {
                        double lat, lon;
                        if (gps.location.isValid())
                        {
                            lat = gps.location.lat();
                            lon = gps.location.lng();
                        }
                        else
                        {
                            lat = config.igate_lat;
                            lon = config.igate_lon;
                        }
                        double dist = aprsParse.distance(lon, lat, aprs.lng, aprs.lat);
                        if (config.filterDistant == 0)
                        {
                            Monitor = true;
                        }
                        else
                        {
                            if (dist < config.filterDistant)
                                Monitor = true;
                            else
                                Monitor = false;
                        }
                    }
                }

                if ((config.dispFilter & FILTER_POSITION) && (aprs.packettype & T_POSITION))
                {
                    if (aprs.flags & F_CSRSPD)
                    {
                        if (aprs.speed > 0)
                        {
                            double lat, lon;
                            if (gps.location.isValid())
                            {
                                lat = gps.location.lat();
                                lon = gps.location.lng();
                            }
                            else
                            {
                                lat = config.igate_lat;
                                lon = config.igate_lon;
                            }
                            double dist = aprsParse.distance(lon, lat, aprs.lng, aprs.lat);
                            if (config.filterDistant == 0)
                            {
                                Monitor = true;
                            }
                            else
                            {
                                if (dist < config.filterDistant)
                                    Monitor = true;
                                else
                                    Monitor = false;
                            }
                        }
                    }
                }
            }
            else
            {
                Monitor = true;
            }
        }
        else
        {
            return;
        }

        if (Monitor)
        {
            display.ssd1306_command(0xE4);
            delay(10);
            display.clearDisplay();
            if (dispPush)
            {
                disp_delay = 600 * 1000;
                display.drawRoundRect(0, 0, 128, 16, 3, WHITE);
            }
            else
            {
                disp_delay = config.dispDelay * 1000;
            }
            timeHalfSec = millis() + disp_delay;
            // display.fillRect(0, 0, 128, 16, WHITE);
            const uint8_t *ptrSymbol;
            uint8_t symIdx = aprs.symbol[1] - 0x21;
            if (symIdx > 95)
                symIdx = 0;
            if (aprs.symbol[0] == '/')
            {
                ptrSymbol = &Icon_TableA[symIdx][0];
            }
            else if (aprs.symbol[0] == '\\')
            {
                ptrSymbol = &Icon_TableB[symIdx][0];
            }
            else
            {
                if (aprs.symbol[0] < 'A' || aprs.symbol[0] > 'Z')
                {
                    aprs.symbol[0] = 'N';
                    aprs.symbol[1] = '&';
                    symIdx = 5; // &
                }
                ptrSymbol = &Icon_TableB[symIdx][0];
            }
            display.drawYBitmap(0, 0, ptrSymbol, 16, 16, WHITE);
            if (!(aprs.symbol[0] == '/' || aprs.symbol[0] == '\\'))
            {
                display.drawChar(5, 4, aprs.symbol[0], BLACK, WHITE, 1);
                display.drawChar(6, 5, aprs.symbol[0], BLACK, WHITE, 1);
            }
            display.setCursor(20, 7);
            display.setTextSize(1);
            display.setFont(&FreeSansBold9pt7b);

            if (aprs.srcname_len > 0)
            {
                memset(&itemname, 0, sizeof(itemname));
                memcpy(&itemname, aprs.srcname, aprs.srcname_len);
                //Serial.println(itemname);
                display.print(itemname);
            }
            else
            {
                display.print(src_call);
            }

            display.setFont();
            display.setTextColor(WHITE);
            // if (selTab < 10)
            //     display.setCursor(121, 0);
            // else
            //     display.setCursor(115, 0);
            // display.print(selTab);

            if (mode == 1)
            {
                display.drawRoundRect(0, 16, 128, 48, 5, WHITE);
                display.fillRoundRect(1, 17, 126, 10, 2, WHITE);
                display.setTextColor(BLACK);
                display.setCursor(40, 18);
                display.print("TNC2 RAW");

                display.setFont();
                display.setCursor(2, 30);
                display.setTextColor(WHITE);
                display.print(line);

                display.display();
                return;
            }

            if (aprs.packettype & T_TELEMETRY)
            {
                bool show = false;
                int idx = tlmList_Find((char *)src_call.c_str());
                if (idx < 0)
                {
                    idx = tlmListOld();
                    if (idx > -1)
                        memset(&Telemetry[idx], 0, sizeof(Telemetry_struct));
                }
                if (idx > -1)
                {
                    Telemetry[idx].time = now();
                    strcpy(Telemetry[idx].callsign, (char *)src_call.c_str());

                    // for (int i = 0; i < 3; i++) Telemetry[idx].UNIT[i][5] = 0;
                    if (aprs.flags & F_UNIT)
                    {
                        memcpy(Telemetry[idx].UNIT, aprs.tlm_unit.val, sizeof(Telemetry[idx].UNIT));
                    }
                    else if (aprs.flags & F_PARM)
                    {
                        memcpy(Telemetry[idx].PARM, aprs.tlm_parm.val, sizeof(Telemetry[idx].PARM));
                    }
                    else if (aprs.flags & F_EQNS)
                    {
                        for (int i = 0; i < 15; i++)
                            Telemetry[idx].EQNS[i] = aprs.tlm_eqns.val[i];
                    }
                    else if (aprs.flags & F_BITS)
                    {
                        Telemetry[idx].BITS_FLAG = aprs.telemetry.bitsFlag;
                    }
                    else if (aprs.flags & F_TLM)
                    {
                        for (int i = 0; i < 5; i++)
                            Telemetry[idx].VAL[i] = aprs.telemetry.val[i];
                        Telemetry[idx].BITS = aprs.telemetry.bits;
                        show = true;
                    }

                    for (int i = 0; i < 4; i++)
                    { // Cut length
                        if (strstr(Telemetry[idx].PARM[i], "RxTraffic") != 0)
                            sprintf(Telemetry[idx].PARM[i], "RX");
                        if (strstr(Telemetry[idx].PARM[i], "TxTraffic") != 0)
                            sprintf(Telemetry[idx].PARM[i], "TX");
                        if (strstr(Telemetry[idx].PARM[i], "RxDrop") != 0)
                            sprintf(Telemetry[idx].PARM[i], "DROP");
                        Telemetry[idx].PARM[i][6] = 0;
                        Telemetry[idx].UNIT[i][3] = 0;
                        for (int a = 0; a < 3; a++)
                        {
                            if (Telemetry[idx].UNIT[i][a] == '/')
                                Telemetry[idx].UNIT[i][a] = 0;
                        }
                    }

                    for (int i = 0; i < 5; i++)
                    {
                        if (Telemetry[idx].PARM[i][0] == 0)
                        {
                            sprintf(Telemetry[idx].PARM[i], "CH%d", i + 1);
                        }
                    }
                }
                if (show || filter == false)
                {
                    display.drawRoundRect(0, 16, 128, 48, 5, WHITE);
                    display.fillRoundRect(1, 17, 126, 10, 2, WHITE);
                    display.setTextColor(BLACK);
                    display.setCursor(40, 18);
                    display.print("TELEMETRY");
                    display.setFont();
                    display.setTextColor(WHITE);
                    display.setCursor(2, 28);
                    display.print(Telemetry[idx].PARM[0]);
                    display.print(":");

                    if (fmod(Telemetry[idx].VAL[0], 1) == 0)
                        display.print(Telemetry[idx].VAL[0], 0);
                    else
                        display.print(Telemetry[idx].VAL[0], 1);
                    display.print(Telemetry[idx].UNIT[0]);
                    display.setCursor(65, 28);
                    display.print(Telemetry[idx].PARM[1]);
                    display.print(":");
                    if (fmod(Telemetry[idx].VAL[1], 1) == 0)
                        display.print(Telemetry[idx].VAL[1], 0);
                    else
                        display.print(Telemetry[idx].VAL[1], 1);
                    display.print(Telemetry[idx].UNIT[1]);
                    display.setCursor(2, 37);
                    display.print(Telemetry[idx].PARM[2]);
                    display.print(":");
                    if (fmod(Telemetry[idx].VAL[2], 1) == 0)
                        display.print(Telemetry[idx].VAL[2], 0);
                    else
                        display.print(Telemetry[idx].VAL[2], 1);
                    display.print(Telemetry[idx].UNIT[2]);
                    display.setCursor(65, 37);
                    display.print(Telemetry[idx].PARM[3]);
                    display.print(":");
                    if (fmod(Telemetry[idx].VAL[3], 1) == 0)
                        display.print(Telemetry[idx].VAL[3], 0);
                    else
                        display.print(Telemetry[idx].VAL[3], 1);
                    display.print(Telemetry[idx].UNIT[3]);
                    display.setCursor(2, 46);
                    display.print(Telemetry[idx].PARM[4]);
                    display.print(":");
                    display.print(Telemetry[idx].VAL[4], 1);
                    display.print(Telemetry[idx].UNIT[4]);

                    display.setCursor(4, 55);
                    display.print("BIT");
                    uint8_t bit = Telemetry[idx].BITS;
                    for (int i = 0; i < 8; i++)
                    {
                        if (bit & 0x80)
                        {
                            display.fillCircle(30 + (i * 12), 58, 3, WHITE);
                        }
                        else
                        {
                            display.drawCircle(30 + (i * 12), 58, 3, WHITE);
                        }
                        bit <<= 1;
                    }
                    // display.print(Telemetry[idx].BITS, BIN);

                    // display.setFont();
                    // display.setCursor(2, 30);
                    // memset(&text[0], 0, sizeof(text));
                    // memcpy(&text[0], aprs.comment, aprs.comment_len);
                    // display.setTextColor(WHITE);
                    // display.print(aprs.comment);
                    display.display();
                }
                return;
            }
            else if (aprs.packettype & T_STATUS)
            {
                display.drawRoundRect(0, 16, 128, 48, 5, WHITE);
                display.fillRoundRect(1, 17, 126, 10, 2, WHITE);
                display.setTextColor(BLACK);
                display.setCursor(48, 18);
                display.print("STATUS");

                display.setFont();
                display.setCursor(2, 30);
                // memset(&text[0], 0, sizeof(text));
                // memcpy(&text[0], aprs.comment, aprs.comment_len);
                display.setTextColor(WHITE);
                display.print(aprs.comment);
                display.display();
                return;
            }
            else if (aprs.packettype & T_QUERY)
            {
                display.drawRoundRect(0, 16, 128, 48, 5, WHITE);
                display.fillRoundRect(1, 17, 126, 10, 2, WHITE);
                display.setTextColor(BLACK);
                display.setCursor(48, 18);
                display.print("?QUERY?");
                // memset(&text[0], 0, sizeof(text));
                // memcpy(&text[0], aprs.comment, aprs.comment_len);
                display.setFont();
                display.setTextColor(WHITE);
                display.setCursor(2, 30);
                display.print(aprs.comment);
                display.display();
                return;
            }
            else if (aprs.packettype & T_MESSAGE)
            {
                if (aprs.msg.is_ack == 1)
                {
                }
                else if (aprs.msg.is_rej == 1)
                {
                }
                else
                {
                    display.drawRoundRect(0, 16, 128, 48, 5, WHITE);
                    display.fillRoundRect(1, 17, 126, 10, 2, WHITE);
                    display.setTextColor(BLACK);
                    display.setCursor(48, 18);
                    display.print("MESSAGE");
                    display.setCursor(100, 18);
                    display.print("{");
                    char txtID[7];
                    memset(txtID, 0, sizeof(txtID));
                    strncpy(&txtID[0], aprs.msg.msgid, aprs.msg.msgid_len);
                    int msgid = atoi(txtID);
                    // display.print(msgid, DEC);
                    display.printf("%s", txtID);
                    display.print("}");
                    // memset(&text[0], 0, sizeof(text));
                    // memcpy(&text[0], aprs.comment, aprs.comment_len);
                    display.setFont();
                    display.setTextColor(WHITE);
                    display.setCursor(2, 30);
                    display.print("To: ");
                    memset(text, 0, sizeof(text));
                    strncpy(&text[0], aprs.dstname, aprs.dstname_len);
                    display.print(text);
                    String mycall = "";
                    if (config.aprs_ssid > 0)
                        mycall = String(config.aprs_mycall) + String("-") + String(config.aprs_ssid, DEC);
                    else
                        mycall = String(config.aprs_mycall);
                    // if (strcmp(mycall.c_str(), text) == 0)
                    // {
                    //     display.setCursor(2, 54);
                    //     display.print("ACK:");
                    //     display.println(msgid);
                    //     String rawData = sendIsAckMsg(src_call, txtID);
                    //     log_d("IGATE_MSG: %s", rawData.c_str());
                    //     //if (config.igate_loc2rf)
                    //     { // IGATE SEND POSITION TO RF
                    //         char *rawP = (char *)malloc(rawData.length());
                    //         memcpy(rawP, rawData.c_str(), rawData.length());
                    //         pkgTxPush(rawP, rawData.length(), 0);
                    //         free(rawP);
                    //     }
                    //     // if (config.igate_loc2inet)
                    //     // { // IGATE SEND TO APRS-IS
                    //     //     if (aprsClient.connected())
                    //     //     {
                    //     //         aprsClient.println(rawData); // Send packet to Inet
                    //     //     }
                    //     // }
                    // }
                    memset(text, 0, sizeof(text));
                    strncpy(&text[0], aprs.msg.body, aprs.msg.body_len);
                    display.setCursor(2, 40);
                    display.print("Msg: ");
                    display.println(text);

                    display.display();
                }
                return;
            }
            display.setFont();
            display.drawFastHLine(0, 16, 128, WHITE);
            display.drawFastVLine(48, 16, 48, WHITE);
            x = 8;

            if (aprs.srcname_len > 0)
            {
                x += 9;
                display.fillRoundRect(51, 16, 77, 9, 2, WHITE);
                display.setTextColor(BLACK);
                display.setCursor(53, x);
                display.print("By " + src_call);
                display.setTextColor(WHITE);
            }

            if (aprs.packettype & T_WAVE)
            {
                // Serial.println("WX Display");
                if (aprs.wave_report.flags & O_TEMP)
                {
                    display.setCursor(58, x += 10);
                    display.drawYBitmap(51, x, &Temperature_Symbol[0], 5, 8, WHITE);
                    display.printf("%.2fC", aprs.wave_report.Temp);
                }
                if (aprs.wave_report.flags & O_HS)
                {
                    // display.setCursor(102, x);
                    display.setCursor(58, x += 9);
                    display.printf("Hs:");
                    display.printf("%0.1f M", aprs.wave_report.Hs / 100);
                }
                if (aprs.wave_report.flags & O_TZ)
                {
                    display.setCursor(58, x += 9);
                    display.printf("Tz: ");
                    display.printf("%0.1f S", aprs.wave_report.Tz);
                }
                // if (aprs.wave_report.flags & O_TC)
                // {
                //     display.setCursor(58, x += 9);
                //     display.printf("Tc: ");
                //     display.printf("%0.1fS.", aprs.wave_report.Tc);
                // }
                if (aprs.wave_report.flags & O_BAT)
                {
                    display.setCursor(58, x += 9);
                    display.printf("BAT: ");
                    display.printf("%0.2fV", aprs.wave_report.Bat);
                }
            }
            if (aprs.packettype & T_WX)
            {
                // Serial.println("WX Display");
                if (aprs.wx_report.flags & W_TEMP)
                {
                    display.setCursor(58, x += 10);
                    display.drawYBitmap(51, x, &Temperature_Symbol[0], 5, 8, WHITE);
                    display.printf("%.1fC", aprs.wx_report.temp);
                }
                if (aprs.wx_report.flags & W_HUM)
                {
                    display.setCursor(102, x);
                    display.drawYBitmap(95, x, &Humidity_Symbol[0], 5, 8, WHITE);
                    display.printf("%d%%", aprs.wx_report.humidity);
                }
                if (aprs.wx_report.flags & W_BAR)
                {
                    display.setCursor(58, x += 9);
                    display.drawYBitmap(51, x, &Pressure_Symbol[0], 5, 8, WHITE);
                    display.printf("%.1fhPa", aprs.wx_report.pressure);
                }
                if (aprs.wx_report.flags & W_R24H)
                {
                    // if (aprs.wx_report.rain_1h > 0) {
                    display.setCursor(58, x += 9);
                    display.drawYBitmap(51, x, &Rain_Symbol[0], 5, 8, WHITE);
                    display.printf("%.1fmm.", aprs.wx_report.rain_24h);
                    //}
                }
                if (aprs.wx_report.flags & W_PAR)
                {
                    // if (aprs.wx_report.luminosity > 10) {
                    display.setCursor(51, x += 9);
                    display.printf("%c", 0x0f);
                    display.setCursor(58, x);
                    display.printf("%dW/m", aprs.wx_report.luminosity);
                    if (aprs.wx_report.flags & W_UV)
                    {
                        display.printf(" UV%d", aprs.wx_report.uv);
                    }
                    //}
                }
                if (aprs.wx_report.flags & W_WS)
                {
                    display.setCursor(58, x += 9);
                    display.drawYBitmap(51, x, &Wind_Symbol[0], 5, 8, WHITE);
                    // int dirIdx=map(aprs.wx_report.wind_dir, -180, 180, 0, 8); ((angle+22)/45)%8]
                    int dirIdx = ((aprs.wx_report.wind_dir + 22) / 45) % 8;
                    if (dirIdx > 8)
                        dirIdx = 8;
                    display.printf("%.1fkPh(%s)", aprs.wx_report.wind_speed, directions[dirIdx]);
                }
                // Serial.printf("%.1fkPh(%d)", aprs.wx_report.wind_speed, aprs.wx_report.wind_dir);
                if (aprs.flags & F_HASPOS)
                {
                    // Serial.println("POS Display");
                    double lat, lon;
                    if (gps.location.isValid())
                    {
                        lat = gps.location.lat();
                        lon = gps.location.lng();
                    }
                    else
                    {
                        lat = config.igate_lat;
                        lon = config.igate_lon;
                    }
                    double dtmp = aprsParse.direction(lon, lat, aprs.lng, aprs.lat);
                    double dist = aprsParse.distance(lon, lat, aprs.lng, aprs.lat);
                    if (config.h_up == true)
                    {
                        // double course = gps.course.deg();
                        double course = SB_HEADING;
                        if (dtmp >= course)
                        {
                            dtmp -= course;
                        }
                        else
                        {
                            double diff = dtmp - course;
                            dtmp = diff + 360.0F;
                        }
                        compass_label(25, 37, 15, course, WHITE);
                        display.setCursor(0, 17);
                        display.printf("H");
                    }
                    else
                    {
                        compass_label(25, 37, 15, 0.0F, WHITE);
                    }
                    // compass_label(25, 37, 15, 0.0F, WHITE);
                    compass_arrow(25, 37, 12, dtmp, WHITE);
                    display.drawFastHLine(1, 63, 45, WHITE);
                    display.drawFastVLine(1, 58, 5, WHITE);
                    display.drawFastVLine(46, 58, 5, WHITE);
                    display.setCursor(4, 55);
                    if (dist > 999)
                        display.printf("%.fKm", dist);
                    else
                        display.printf("%.1fKm", dist);
                }
                else
                {
                    display.setCursor(20, 30);
                    display.printf("NO\nPOSITION");
                }
            }
            else if (aprs.flags & F_HASPOS)
            {
                // display.setCursor(50, x += 10);
                // display.printf("LAT %.5f\n", aprs.lat);
                // display.setCursor(51, x+=9);
                // display.printf("LNG %.4f\n", aprs.lng);
                String str;
                int l = 0;
                display.setCursor(50, x += 10);
                display.print("LAT:");
                str = String(aprs.lat, 5);
                l = str.length() * 6;
                display.setCursor(128 - l, x);
                display.print(str);

                display.setCursor(50, x += 9);
                display.print("LON:");
                str = String(aprs.lng, 5);
                l = str.length() * 6;
                display.setCursor(128 - l, x);
                display.print(str);

                double lat, lon;
                if (gps.location.isValid())
                {
                    lat = gps.location.lat();
                    lon = gps.location.lng();
                }
                else
                {
                    lat = config.igate_lat;
                    lon = config.igate_lon;
                }
                double dtmp = aprsParse.direction(lon, lat, aprs.lng, aprs.lat);
                double dist = aprsParse.distance(lon, lat, aprs.lng, aprs.lat);
                if (config.h_up == true)
                {
                    // double course = gps.course.deg();
                    double course = SB_HEADING;
                    if (dtmp >= course)
                    {
                        dtmp -= course;
                    }
                    else
                    {
                        double diff = dtmp - course;
                        dtmp = diff + 360.0F;
                    }
                    compass_label(25, 37, 15, course, WHITE);
                    display.setCursor(0, 17);
                    display.printf("H");
                }
                else
                {
                    compass_label(25, 37, 15, 0.0F, WHITE);
                }
                compass_arrow(25, 37, 12, dtmp, WHITE);
                display.drawFastHLine(1, 55, 45, WHITE);
                display.drawFastVLine(1, 55, 5, WHITE);
                display.drawFastVLine(46, 55, 5, WHITE);
                display.setCursor(4, 57);
                if (dist > 999)
                    display.printf("%.fKm", dist);
                else
                    display.printf("%.1fKm", dist);
                if (aprs.flags & F_CSRSPD)
                {
                    display.setCursor(51, x += 9);
                    // display.printf("SPD %d/", aprs.course);
                    // display.setCursor(50, x += 9);
                    display.printf("SPD %.1fkPh\n", aprs.speed);
                    int dirIdx = ((aprs.course + 22) / 45) % 8;
                    if (dirIdx > 8)
                        dirIdx = 8;
                    display.setCursor(51, x += 9);
                    display.printf("CSD %d(%s)", aprs.course, directions[dirIdx]);
                }
                if (aprs.flags & F_ALT)
                {
                    display.setCursor(51, x += 9);
                    display.printf("ALT %.1fM\n", aprs.altitude);
                }
                if (aprs.flags & F_PHG)
                {
                    int power, height, gain;
                    unsigned char tmp;
                    power = (int)aprs.phg[0] - 0x30;
                    power *= power;
                    height = (int)aprs.phg[1] - 0x30;
                    height = 10 << (height + 1);
                    height = height / 3.2808;
                    gain = (int)aprs.phg[2] - 0x30;
                    display.setCursor(51, x += 9);
                    display.printf("PHG %dM.\n", height);
                    display.setCursor(51, x += 9);
                    display.printf("PWR %dWatt\n", power);
                    display.setCursor(51, x += 9);
                    display.printf("ANT %ddBi\n", gain);
                }
                if (aprs.flags & F_RNG)
                {
                    display.setCursor(51, x += 9);
                    display.printf("RNG %dKm\n", aprs.radio_range);
                }
                /*if (aprs.comment_len > 0) {
                    display.setCursor(0, 56);
                    display.print(aprs.comment);
                }*/
            }
            display.display();
        }
    }
}

void statisticsDisp()
{

    // uint8 wifi = 0, i;
    int x;
    String str;
    // display.fillRect(0, 16, 128, 10, WHITE);
    // display.drawLine(0, 16, 0, 63, WHITE);
    // display.drawLine(127, 16, 127, 63, WHITE);
    // display.drawLine(0, 63, 127, 63, WHITE);
    // display.fillRect(1, 25, 126, 38, BLACK);
    // display.setTextColor(BLACK);
    // display.setCursor(30, 17);
    // display.print("STATISTICS");
    // display.setCursor(108, 17);
    // display.print("1/5");
    // display.setTextColor(WHITE);

    display.fillRect(0, 0, 128, 15, WHITE);
    display.drawRect(0, 16, 128, 48, WHITE);
    display.fillRect(1, 17, 126, 46, BLACK);

    display.setCursor(5, 7);
    display.setTextSize(1);
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(BLACK);
    display.print("STATISTIC");
    display.setFont();
    display.setCursor(108, 7);
    display.print("1/4");
    display.setTextColor(WHITE);

    display.setCursor(3, 18);
    display.print("ALL DATA");
    str = String(status.allCount, DEC);
    x = str.length() * 6;
    display.setCursor(126 - x, 18);
    display.print(str);

    display.setCursor(3, 26);
    display.print("DIGI RPT");
    str = String(status.digiCount, DEC);
    x = str.length() * 6;
    display.setCursor(126 - x, 26);
    display.print(str);

    display.setCursor(3, 35);
    display.print("RF->INET");
    str = String(status.rf2inet, DEC);
    x = str.length() * 6;
    display.setCursor(126 - x, 35);
    display.print(str);

    display.setCursor(3, 44);
    display.print("INET->RF");
    str = String(status.inet2rf, DEC);
    x = str.length() * 6;
    display.setCursor(126 - x, 44);
    display.print(str);

    display.setCursor(3, 53);
    display.print("ERROR/DROP");
    str = String(status.errorCount + status.dropCount, DEC);
    x = str.length() * 6;
    display.setCursor(126 - x, 53);
    display.print(str);

    display.display();
}

void pkgLastDisp()
{

    uint8_t k = 0;
    int i;
    // char list[4];
    int x, y;
    String str;
    // String times;
    // pkgListType *ptr[100];

    display.fillRect(0, 0, 128, 15, WHITE);
    display.drawRect(0, 16, 128, 48, WHITE);
    display.fillRect(1, 17, 126, 46, BLACK);

    display.setCursor(15, 7);
    display.setTextSize(1);
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(BLACK);
    display.print("STATION");
    display.setFont();
    display.setCursor(108, 7);
    display.print("2/4");
    display.setTextColor(WHITE);

    // display.fillRect(0, 16, 128, 10, WHITE);
    // display.drawLine(0, 16, 0, 63, WHITE);
    // display.drawLine(127, 16, 127, 63, WHITE);
    // display.drawLine(0, 63, 127, 63, WHITE);
    // display.fillRect(1, 25, 126, 38, BLACK);
    // display.setTextColor(BLACK);
    // display.setCursor(27, 17);
    // display.print("LAST STATIONS");
    // display.setCursor(108, 17);
    // display.print("2/5");
    // display.setTextColor(WHITE);

    sort(pkgList, PKGLISTSIZE);
    k = 0;
    for (i = 0; i < PKGLISTSIZE; i++)
    {
        if (pkgList[i].time > 0)
        {
            y = 18 + (k * 9);
            // display.drawBitmap(3, y, &SYMBOL[0][0], 11, 6, WHITE);
            display.fillRoundRect(2, y, 7, 8, 2, WHITE);
            display.setCursor(3, y);
            pkgList[i].calsign[10] = 0;
            display.setTextColor(BLACK);
            switch (pkgList[i].type)
            {
            case PKG_OBJECT:
                display.print("O");
                break;
            case PKG_ITEM:
                display.print("I");
                break;
            case PKG_MESSAGE:
                display.print("M");
                break;
            case PKG_WX:
                display.print("W");
                break;
            case PKG_TELEMETRY:
                display.print("T");
                break;
            case PKG_QUERY:
                display.print("Q");
                break;
            case PKG_STATUS:
                display.print("S");
                break;
            default:
                display.print("*");
                break;
            }
            display.setTextColor(WHITE);
            display.setCursor(10, y);
            display.print(pkgList[i].calsign);
            display.setCursor(126 - 48, y);
            // display.printf("%02d:%02d:%02d", hour(pkgList[i].time), minute(pkgList[i].time), second(pkgList[i].time));

            // time_t tm = pkgList[i].time;
            struct tm tmstruct;
            localtime_r(&pkgList[i].time, &tmstruct);
            String str = String(tmstruct.tm_hour, DEC) + ":" + String(tmstruct.tm_min, DEC) + ":" + String(tmstruct.tm_sec, DEC);
            display.print(str);
            // str = String(hour(pkgList[i].time),DEC) + ":" + String(minute(pkgList[i].time), DEC) + ":" + String(second(pkgList[i].time), DEC);
            ////str = String(pkgList[pkgLast_array[i]].time, DEC);
            // x = str.length() * 6;
            // display.setCursor(126 - x, y);
            // display.print(str);
            k++;
            if (k >= 5)
                break;
        }
    }
    display.display();
}

void pkgCountDisp()
{

    // uint8 wifi = 0, k = 0, l;
    uint k = 0;
    int i;
    // char list[4];
    int x, y;
    String str;
    // String times;
    // pkgListType *ptr[100];

    display.fillRect(0, 0, 128, 15, WHITE);
    display.drawRect(0, 16, 128, 48, WHITE);
    display.fillRect(1, 17, 126, 46, BLACK);

    display.setCursor(20, 7);
    display.setTextSize(1);
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(BLACK);
    display.print("TOP PKG");
    display.setFont();
    display.setCursor(108, 7);
    display.print("3/4");
    display.setTextColor(WHITE);

    // display.setCursor(3, 18);

    // display.fillRect(0, 16, 128, 10, WHITE);
    // display.drawLine(0, 16, 0, 63, WHITE);
    // display.drawLine(127, 16, 127, 63, WHITE);
    // display.drawLine(0, 63, 127, 63, WHITE);
    // display.fillRect(1, 25, 126, 38, BLACK);
    // display.setTextColor(BLACK);
    // display.setCursor(30, 17);
    // display.print("TOP PACKAGE");
    // display.setCursor(108, 17);
    // display.print("3/5");
    // display.setTextColor(WHITE);

    sortPkgDesc(pkgList, PKGLISTSIZE);
    k = 0;
    for (i = 0; i < PKGLISTSIZE; i++)
    {
        if (pkgList[i].time > 0)
        {
            y = 18 + (k * 9);
            // display.drawBitmapV(2, y-1, &SYMBOL[pkgList[i].symbol][0], 11, 8, WHITE);
            pkgList[i].calsign[10] = 0;
            display.fillRoundRect(2, y, 7, 8, 2, WHITE);
            display.setCursor(3, y);
            pkgList[i].calsign[10] = 0;
            display.setTextColor(BLACK);
            switch (pkgList[i].type)
            {
            case PKG_OBJECT:
                display.print("O");
                break;
            case PKG_ITEM:
                display.print("I");
                break;
            case PKG_MESSAGE:
                display.print("M");
                break;
            case PKG_WX:
                display.print("W");
                break;
            case PKG_TELEMETRY:
                display.print("T");
                break;
            case PKG_QUERY:
                display.print("Q");
                break;
            case PKG_STATUS:
                display.print("S");
                break;
            default:
                display.print("*");
                break;
            }
            display.setTextColor(WHITE);
            display.setCursor(10, y);
            display.print(pkgList[i].calsign);
            str = String(pkgList[i].pkg, DEC);
            x = str.length() * 6;
            display.setCursor(126 - x, y);
            display.print(str);
            k++;
            if (k >= 5)
                break;
        }
    }
    display.display();
}

void systemDisp()
{

    // uint8 wifi = 0, k = 0, l;
    // char i;
    // char list[4];
    int x;
    String str;
    time_t upTime = now() - systemUptime;
    // String times;
    // pkgListType *ptr[100];

    // display.fillRect(0, 16, 128, 10, WHITE);
    // display.drawLine(0, 16, 0, 63, WHITE);
    display.fillRect(0, 0, 128, 15, WHITE);
    // display.drawLine(0, 16, 0, 63, WHITE);

    // display.drawLine(127, 16, 127, 63, WHITE);
    // display.drawLine(0, 63, 127, 63, WHITE);
    // display.fillRect(1, 25, 126, 38, BLACK);
    display.drawRect(0, 16, 128, 48, WHITE);
    display.fillRect(1, 17, 126, 46, BLACK);
    // display.fillRoundRect(1, 17, 126, 46, 2, WHITE);

    display.setCursor(20, 7);
    display.setTextSize(1);
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(BLACK);
    display.print("SYSTEM");
    display.setFont();
    display.setCursor(108, 7);
    display.print("4/4");
    display.setTextColor(WHITE);

    display.setCursor(3, 18);
    // display.print("HMEM:");
    display.print("MAC");
    // str = String(ESP.getFreeHeap(), DEC)+"Byte";
    str = String(WiFi.macAddress());
    x = str.length() * 6;
    display.setCursor(126 - x, 18);
    display.print(str);

    display.setCursor(3, 26);
    display.print("IP:");
    str = String(WiFi.localIP().toString());
    x = str.length() * 6;
    display.setCursor(126 - x, 26);
    display.print(str);

    display.setCursor(3, 35);
    display.print("UPTIME:");
    str = String(day(upTime) - 1, DEC) + "D " + String(hour(upTime), DEC) + ":" + String(minute(upTime), DEC) + ":" + String(second(upTime), DEC);
    x = str.length() * 6;
    display.setCursor(126 - x, 35);
    display.print(str);

    display.setCursor(3, 44);
    display.print("WiFi RSSI:");
    str = String(WiFi.RSSI()) + "dBm";
    // str = String(str_status[WiFi.status()]);
    x = str.length() * 6;
    display.setCursor(126 - x, 44);
    display.print(str);

    display.setCursor(3, 53);
    display.print("Firmware:");
    str = "V" + String(VERSION);
    x = str.length() * 6;
    display.setCursor(126 - x, 53);
    display.print(str);

    display.display();
}
#endif