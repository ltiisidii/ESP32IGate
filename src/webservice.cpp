/*
 Name:		ESP32IGate
 Created:	13-10-2023 14:27:23
 Author:	HS5TQA/Atten
 Github:	https://github.com/nakhonthai
 Facebook:	https://www.facebook.com/atten
 Support IS: host:aprs.dprns.com port:14580 or aprs.hs5tqa.ampr.org:14580
 Support IS monitor: http://aprs.dprns.com:14501 or http://aprs.hs5tqa.ampr.org:14501
*/
#include <Arduino.h>
#include "AFSK.h"
#include "webservice.h"
#include "base64.hpp"
#include <LibAPRSesp.h>
#include <parse_aprs.h>
#include "jquery_min_js.h"

PacketCounter packetCounters[MAX_CALLSIGNS] = {};
int counterSize = 0;

AsyncWebServer async_server(80);
AsyncWebServer async_websocket(81);
AsyncWebSocket ws("/ws");
AsyncWebSocket ws_gnss("/ws_gnss");

// Create an Event Source on /events
AsyncEventSource lastheard_events("/eventHeard");

String loadHtmlTemplate(const char* path) {
    // Verifica que SPIFFS ya está montado
    if (!SPIFFS.begin(false)) { // No forces el formateo
        Serial.println("[ERROR] SPIFFS no montado. Verifica el montaje en setup().");
        return "";
    }

    // Intenta abrir el archivo
    File file = SPIFFS.open(path, "r");
    if (!file) {
        // Serial.printf("[ERROR] No se pudo abrir el archivo: %s\n", path);
        return "";
    }

    // Verifica que no sea un directorio
    if (file.isDirectory()) {
        // Serial.printf("[ERROR] La ruta es un directorio: %s\n", path);
        file.close();
        return "";
    }

    // Lee todo el contenido del archivo
    String html = file.readString();
    file.close();

    // Verifica que el contenido no esté vacío
    if (html.isEmpty()) {
        // Serial.printf("[ERROR] Archivo vacío: %s\n", path);
        return "";
    }

    // Serial.printf("[INFO] Archivo %s cargado correctamente (%d bytes)\n", path, html.length());
    return html;
}

String webString;

bool defaultSetting = false;

void serviceHandle()
{
	// server.handleClient();
}

String generateBaudrateOptions(int currentBaudrate) {
    String options = "";
    int baudrates[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};

    for (int i = 0; i < sizeof(baudrates) / sizeof(baudrates[0]); i++) {
        options += "<option value=\"" + String(baudrates[i]) + "\"";
        if (baudrates[i] == currentBaudrate) {
            options += " selected";
        }
        options += ">" + String(baudrates[i]) + "</option>";
    }

    return options;
}

void notFound(AsyncWebServerRequest *request)
{
	request->send(404, "text/plain", "Not found");
}

void setMainPage(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    // Servir index.html desde SPIFFS
    request->send(SPIFFS, "/index.html", "text/html");
}


void handle_list_files(AsyncWebServerRequest *request) {
    // Obtener información de SPIFFS
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;

    // Función para formatear tamaños en B, KB, MB
    auto formatSize = [](size_t bytes) {
        String result;
        if (bytes >= 1048576) { // MB
            result = String((float)bytes / 1048576, 2) + " MB";
        } else if (bytes >= 1024) { // KB
            result = String((float)bytes / 1024, 2) + " KB";
        } else { // Bytes
            result = String(bytes) + " B";
        }
        return result;
    };

    // Crear HTML dinámico
    String html = "<!DOCTYPE html><html lang='en'><head><title>Lista de Archivos</title></head><body>";
    html += "<h1>Archivos en SPIFFS</h1>";
    html += "<p><b>Espacio Total:</b> " + formatSize(totalBytes) + "</p>";
    html += "<p><b>Espacio Usado:</b> " + formatSize(usedBytes) + "</p>";
    html += "<p><b>Espacio Libre:</b> " + formatSize(freeBytes) + "</p>";
    html += "<ul>";

    // Listar archivos en SPIFFS
    File root = SPIFFS.open("/");
    File file = root.openNextFile();

    while (file) {
        html += "<li>";
        html += file.name(); // Nombre del archivo
        html += " - ";
        html += formatSize(file.size()); // Tamaño del archivo en formato amigable
        html += "</li>";
        file = root.openNextFile();
    }

    html += "</ul>";
    html += "</body></html>";

    // Enviar HTML como respuesta
    request->send(200, "text/html", html);
}

////////////////////////////////////////////////////////////
// handler for web server request: http://IpAddress/      //
////////////////////////////////////////////////////////////

void handle_css(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/style.css.gz", "text/css");
    response->addHeader("Cache-Control", "max-age=604800"); // 1 semana de caché
    response->addHeader("Content-Encoding", "gzip"); // Especifica que está comprimido
    request->send(response);
}

void handle_jquery(AsyncWebServerRequest *request)
{
	AsyncWebServerResponse *response = request->beginResponse_P(200, String(F("application/javascript")), (const uint8_t *)jquery_3_7_1_min_js_gz, jquery_3_7_1_min_js_gz_len);
	response->addHeader(String(F("Content-Encoding")), String(F("gzip")));
	response->setContentLength(jquery_3_7_1_min_js_gz_len);
	request->send(response);
}

void handle_dashboard(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    // Servir dashboard.html desde SPIFFS
    request->send(SPIFFS, "/dashboard.html", "text/html");
}

void handle_sidebar(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}
	String html = "<table style=\"background:white;border-collapse: unset;\">\n";
	html += "<tr>\n";
	html += "<th colspan=\"2\">Modes Enabled</th>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	if (config.igate_en)
		html += "<th style=\"background:#0b0; color:#030; width:50%;border-radius: 10px;border: 2px solid white;\">IGATE</th>\n";
	else
		html += "<th style=\"background:#606060; color:#b0b0b0;border-radius: 10px;border: 2px solid white;\">IGATE</th>\n";

	if (config.digi_en)
		html += "<th style=\"background:#0b0; color:#030; width:50%;border-radius: 10px;border: 2px solid white;\">DIGI</th>\n";
	else
		html += "<th style=\"background:#606060; color:#b0b0b0;border-radius: 10px;border: 2px solid white;\">DIGI</th>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	if (config.wx_en)
		html += "<th style=\"background:#0b0; color:#030; width:50%;border-radius: 10px;border: 2px solid white;\">WX</th>\n";
	else
		html += "<th style=\"background:#606060; color:#b0b0b0;border-radius: 10px;border: 2px solid white;\">WX</th>\n";
	// html += "<th style=\"background:#606060; color:#b0b0b0;border-radius: 10px;border: 2px solid white;\">SAT</th>\n";
	if (config.trk_en)
		html += "<th style=\"background:#0b0; color:#030; width:50%;border-radius: 10px;border: 2px solid white;\">TRACKER</th>\n";
	else
		html += "<th style=\"background:#606060; color:#b0b0b0;border-radius: 10px;border: 2px solid white;\">TRACKER</th>\n";
	html += "</tr>\n";
	html += "</table>\n";
	html += "<br />\n";
	html += "<table style=\"background:white;border-collapse: unset;\">\n";
	html += "<tr>\n";
	html += "<th colspan=\"2\">Network Status</th>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	if (aprsClient.connected() == true)
		html += "<th style=\"background:#0b0; color:#030; width:50%;border-radius: 10px;border: 2px solid white;\">APRS-IS</th>\n";
	else
		html += "<th style=\"background:#606060; color:#b0b0b0;border-radius: 10px;border: 2px solid white;\" aria-disabled=\"true\">APRS-IS</th>\n";
	// if (wireguard_active() == true)
	// 	html += "<th style=\"background:#0b0; color:#030; width:50%;border-radius: 10px;border: 2px solid white;\">VPN</th>\n";
	// else
	// 	html += "<th style=\"background:#606060; color:#b0b0b0;border-radius: 10px;border: 2px solid white;\" aria-disabled=\"true\">VPN</th>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<th style=\"background:#606060; color:#b0b0b0;border-radius: 10px;border: 2px solid white;\" aria-disabled=\"true\">4G LTE</th>\n";
	html += "<th style=\"background:#606060; color:#b0b0b0;border-radius: 10px;border: 2px solid white;\" aria-disabled=\"true\">MQTT</th>\n";
	html += "</tr>\n";
	html += "</table>\n";
	html += "<br />\n";
	html += "<table>\n";
	html += "<tr>\n";
	html += "<th colspan=\"2\">STATISTICS</th>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td style=\"width: 60px;text-align: right;\">PACKET RX:</td>\n";
	html += "<td style=\"background: #ffffff;\">" + String(status.rxCount) + "</td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td style=\"width: 60px;text-align: right;\">PACKET TX:</td>\n";
	html += "<td style=\"background: #ffffff;\">" + String(status.txCount) + "</td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td style=\"width: 60px;text-align: right;\">RF2INET:</td>\n";
	html += "<td style=\"background: #ffffff;\">" + String(status.rf2inet) + "</td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td style=\"width: 60px;text-align: right;\">INET2RF:</td>\n";
	html += "<td style=\"background: #ffffff;\">" + String(status.inet2rf) + "</td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td style=\"width: 60px;text-align: right;\">DIGI:</td>\n";
	html += "<td style=\"background: #ffffff;\">" + String(status.digiCount) + "</td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td style=\"width: 60px;text-align: right;\">DROP/ERR:</td>\n";
	html += "<td style=\"background: #ffffff;\">" + String(status.dropCount) + "/" + String(status.errorCount) + "</td>\n";
	html += "</tr>\n";
	html += "</table>\n";
	if (config.gnss_enable == true)
	{
		html += "<br />\n";
		html += "<table>\n";
		html += "<tr>\n";
		html += "<th colspan=\"2\">GPS Info <a href=\"/gnss\" target=\"_gnss\" style=\"color: yellow;font-size:8pt\">[View]</a></th>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td>LAT:</td>\n";
		html += "<td style=\"background: #ffffff;text-align: left;\">" + String(gps.location.lat(), 5) + "</td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td>LON:</td>\n";
		html += "<td style=\"background: #ffffff;text-align: left;\">" + String(gps.location.lng(), 5) + "</td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td>ALT:</td>\n";
		html += "<td style=\"background: #ffffff;text-align: left;\">" + String(gps.altitude.meters(), 1) + "</td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td>SAT:</td>\n";
		html += "<td style=\"background: #ffffff;text-align: left;\">" + String(gps.satellites.value()) + "</td>\n";
		html += "</tr>\n";
		html += "</table>\n";
	}
	html += "<script>\n";
	html += "$(window).trigger('resize');\n";
	html += "</script>\n";
	// request->send(200, "text/html", html); // send to someones browser when asked
	// delay(100);
	request->send(200, "text/html", html);
	html.clear();
}

void handle_symbol(AsyncWebServerRequest *request)
{
	int i;
	char *web = (char *)malloc(25000);
	int sel = -1;
	for (i = 0; i < request->args(); i++)
	{
		if (request->argName(i) == "sel")
		{
			if (request->arg(i) != "")
			{
				if (isValidNumber(request->arg(i)))
				{
					sel = request->arg(i).toInt();
				}
			}
		}
	}

	if (web)
	{
		memset(web, 0, 25000);
		strcat(web, "<table border=\"1\" align=\"center\">\n");
		strcat(web, "<tr><th colspan=\"16\">Table '/'</th></tr>\n");
		strcat(web, "<tr>\n");
		for (i = 33; i < 129; i++)
		{
			//<td><img onclick="window.opener.setValue(113,2);" src="https://aprs.p00lack.cc/symbols/icons/113-2.png"></td>
			char lnk[128];
			if (sel == -1)
				sprintf(lnk, "<td><img onclick=\"window.opener.setValue(%d,1);\" src=\"https://aprs.p00lack.cc/symbols/icons/%d-1.png\"></td>", i, i);
			else
				sprintf(lnk, "<td><img onclick=\"window.opener.setValue(%d,%d,1);\" src=\"https://aprs.p00lack.cc/symbols/icons/%d-1.png\"></td>", sel, i, i);
			strcat(web, lnk);

			if (((i % 16) == 0) && (i < 126))
				strcat(web, "</tr>\n<tr>\n");
		}
		strcat(web, "</tr>");
		strcat(web, "</table>\n<br />");
		strcat(web, "<table border=\"1\" align=\"center\">\n");
		strcat(web, "<tr><th colspan=\"16\">Table '\\'</th></tr>\n");
		strcat(web, "<tr>\n");
		for (i = 33; i < 129; i++)
		{
			char lnk[128];
			if (sel == -1)
				sprintf(lnk, "<td><img onclick=\"window.opener.setValue(%d,2);\" src=\"https://aprs.p00lack.cc/symbols/icons/%d-2.png\"></td>", i, i);
			else
				sprintf(lnk, "<td><img onclick=\"window.opener.setValue(%d,%d,2);\" src=\"https://aprs.p00lack.cc/symbols/icons/%d-2.png\"></td>", sel, i, i);
			strcat(web, lnk);
			if (((i % 16) == 0) && (i < 126))
				strcat(web, "</tr>\n<tr>\n");
		}
		strcat(web, "</tr>");
		strcat(web, "</table>\n");
		request->send_P(200, "text/html", web);
		free(web);
	}
}

void handle_sysinfo(AsyncWebServerRequest *request)
{
	// Calcula el tiempo de actividad
	time_t tn = now() - systemUptime;

	// Calcula días, horas y minutos
	int days = day(tn) - 1;  // Resta 1 porque 'day()' empieza en 1
	int hours = hour(tn);
	int minutes = minute(tn);

	// Formatea el uptime como "XD HH:MM"
	String uptime = String(days, DEC) + "D " +
					(hours < 10 ? "0" : "") + String(hours, DEC) + ":" +
					(minutes < 10 ? "0" : "") + String(minutes, DEC);

    // Calcula memoria RAM y PSRAM disponibles y totales
    float freeHeap = (float)ESP.getFreeHeap() / 1024; // en KB
    float totalHeap = (float)ESP.getHeapSize() / 1024; // en KB
    float freePsram = (float)ESP.getFreePsram() / 1024; // en KB
    float totalPsram = (float)ESP.getPsramSize() / 1024; // en KB

    // Calcula espacio en tarjeta SD
    uint32_t cardTotal = SD.totalBytes() / (1024 * 1024); // en MB
    uint32_t cardUsed = SD.usedBytes() / (1024 * 1024); // en MB

    // Calcula la temperatura de la CPU
    float cpuTemp = (float)(temprature_sens_read() - 32) / 1.8F; // Convertir a Celsius

    // Crea el JSON
    String json = "{";
    json += "\"uptime\":\"" + uptime + "\",";
    json += "\"free_ram_kb\":" + String(freeHeap, 1) + ",";
    json += "\"total_ram_kb\":" + String(totalHeap, 1) + ",";
    json += "\"free_psram_kb\":" + String(freePsram, 1) + ",";
    json += "\"total_psram_kb\":" + String(totalPsram, 1) + ",";
    json += "\"sd_card_used_mb\":" + String(cardUsed) + ",";
    json += "\"sd_card_total_mb\":" + String(cardTotal) + ",";
    json += "\"cpu_temp_c\":" + String(cpuTemp, 1);
    json += "}";

    // Envía el JSON como respuesta
    request->send(200, "application/json", json);
}

void handleModesEnabled(AsyncWebServerRequest *request) 
{
    // Crear JSON manualmente
    String json = "{";
    json += "\"igate\":" + String(config.igate_en ? "true" : "false") + ",";
    json += "\"digi\":" + String(config.digi_en ? "true" : "false") + ",";
    json += "\"wx\":" + String(config.wx_en ? "true" : "false") + ",";
    json += "\"tracker\":" + String(config.trk_en ? "true" : "false");
    json += "}";

    // Enviar respuesta como JSON
    request->send(200, "application/json", json);
}

void handleNetworkStatus(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"aprs_is\":" + String(aprsClient.connected() ? "true" : "false") + ",";
    // json += "\"vpn\":" + String(wireguard_active() ? "true" : "false") + ",";
    json += "\"lte_4g\":false,"; // Usa valores booleanos directamente
    json += "\"mqtt\":false";    // Usa valores booleanos directamente
    json += "}";

    request->send(200, "application/json", json);
}

void handleStatistics(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"packet_rx\":" + String(status.rxCount) + ",";
    json += "\"packet_tx\":" + String(status.txCount) + ",";
    json += "\"rf2inet\":" + String(status.rf2inet) + ",";
    json += "\"inet2rf\":" + String(status.inet2rf) + ",";
    json += "\"digi\":" + String(status.digiCount) + ",";
    json += "\"drop\":" + String(status.dropCount) + ",";
    json += "\"error\":" + String(status.errorCount);
    json += "}";

    request->send(200, "application/json", json);
}

void handleRadioInfo(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"freq_tx\":\"" + String(config.freq_tx, 4) + " MHz\",";
    json += "\"freq_rx\":\"" + String(config.freq_rx, 4) + " MHz\",";
    json += "\"power\":\"" + String(config.rf_power == 0 ? "LOW" : "HIGH") + "\"";
    json += "}";

    request->send(200, "application/json", json);
}

void handleAPRSServer(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"host\":\"" + String(config.aprs_host) + "\",";
    json += "\"port\":" + String(config.aprs_port);
    json += "}";

    request->send(200, "application/json", json);
}

void handleWiFiInfo(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"mode\":\"" + String(WiFi.getMode() == WIFI_MODE_APSTA ? "AP+STA" : 
                  WiFi.getMode() == WIFI_MODE_AP ? "AP" : 
                  WiFi.getMode() == WIFI_MODE_STA ? "STA" : "Unknown") + "\",";
    json += "\"ssid\":\"" + String(WiFi.SSID()) + "\",";
    json += "\"rssi\":\"" + String(WiFi.RSSI()) + " dBm\"";
    json += "}";

    request->send(200, "application/json", json);
}

String extractViaPath(const String& raw) {
    int start = raw.indexOf('>'); // Encuentra el inicio del path
    if (start != -1) {
        int end = raw.indexOf(':', start); // Encuentra el final del path
        if (end != -1) {
            return raw.substring(start + 1, end); // Extrae el camino entre '>' y ':'
        }
    }
    return "N/A"; // Devuelve un valor predeterminado si no hay path
}

float extractAudioLevel(const String& raw) {
    int start = raw.indexOf('[');
    int end = raw.indexOf(']', start);
    if (start != -1 && end != -1) {
        String audioData = raw.substring(start + 1, end);
        int separator = audioData.indexOf(',');
        if (separator != -1) {
            String dBV = audioData.substring(separator + 2);
            dBV.replace("dBV", "");
            dBV.trim();
            return dBV.toFloat();
        }
    }
    // Si no hay corchetes, devuelve un valor predeterminado
    // Serial.println("Brackets '[' and ']' not found in raw data: " + raw);
    return 0.0;
}

String extractTime(const String& raw) {
    int start = 0; // Supón que el tiempo comienza al inicio de la línea
    int end = raw.indexOf('['); // Encuentra el final del tiempo antes del bloque de audio
    if (end != -1) {
        String time = raw.substring(start, end); // Extrae el tiempo
        time.trim(); // Limpia espacios en blanco
        return time;
    }
    return "N/A"; // Devuelve "N/A" si no se encuentra el tiempo
}

void handle_lastHeard(AsyncWebServerRequest* request) {
    // Tamaño máximo de *callsigns* únicos que esperamos procesar
    String callsigns[MAX_CALLSIGNS];
    int counts[MAX_CALLSIGNS] = {0}; // Inicializar a 0
    int callsignCount = 0;

    // Iterar sobre los paquetes en pkgList
    for (int i = 0; i < PKGLISTSIZE; i++) {
        pkgListType pkg = getPkgList(i);

        if (String(pkg.raw).length() > 0) { // Solo procesar si el paquete no está vacío
            Serial.println("Raw packet data: " + String(pkg.raw)); // Depuración
        } else {
            continue; // Saltar paquetes vacíos
        }

        if (pkg.time > 0) {
            String line = String(pkg.raw);
            int start_val = line.indexOf(">");
            if (start_val > 3) {
                String callsign = line.substring(0, start_val);
                String viaPath = extractViaPath(pkg.raw); // Extraer vía
                float audioLevel = extractAudioLevel(pkg.raw); // Nivel de audio
                String timeString = extractTime(pkg.raw); // Tiempo de recepción

                int index = -1;
                for (int j = 0; j < callsignCount; j++) {
                    if (callsigns[j] == callsign) {
                        index = j;
                        break;
                    }
                }

                if (index == -1) {
                    if (callsignCount < MAX_CALLSIGNS) {
                        callsigns[callsignCount] = callsign;
                        counts[callsignCount] = 1;

                        // Asignar valores adicionales al packetCounter
                        packetCounters[callsignCount].callsign = callsign;
                        packetCounters[callsignCount].viaPath = viaPath;
                        packetCounters[callsignCount].audio = audioLevel;
                        packetCounters[callsignCount].time = timeString;
                        packetCounters[callsignCount].raw = pkg.raw; // Asignar raw
                        callsignCount++;
                    }
                } else {
                    counts[index]++;
                    packetCounters[index].audio = audioLevel; // Actualiza nivel de audio
                    packetCounters[index].time = timeString; // Actualiza el tiempo
                }
            }
        }
    }

    // Construir JSON
    String jsonResponse = "{ \"lastHeard\": [";
    for (int i = 0; i < callsignCount; i++) {
        jsonResponse += "{";
        jsonResponse += "\"callsign\":\"" + packetCounters[i].callsign + "\",";
        jsonResponse += "\"count\":" + String(counts[i]) + ",";
        jsonResponse += "\"viaPath\":\"" + packetCounters[i].viaPath + "\",";
        jsonResponse += "\"dx\":" + String(packetCounters[i].dx) + ",";
        jsonResponse += "\"audio\":" + String(packetCounters[i].audio, 2) + ",";
        jsonResponse += "\"time\":\"" + packetCounters[i].time + "\",";
        jsonResponse += "\"icon\":" + String(packetCounters[i].icon) + ",";
        jsonResponse += "\"raw\":\"" + packetCounters[i].raw + "\"";
        jsonResponse += "}";
        if (i < callsignCount - 1) {
            jsonResponse += ","; // Agregar coma entre objetos JSON
        }
    }
    jsonResponse += "] }";

    // Enviar la respuesta
    request->send(200, "application/json", jsonResponse);
}

void event_lastHeard() {
    for (int i = 0; i < PKGLISTSIZE; i++) {
        pkgListType pkg = getPkgList(i);
        if (pkg.time > 0) {
            String line = String(pkg.raw);
            int start_val = line.indexOf(">");
            if (start_val > 3) {
                String callsign = line.substring(0, start_val);
                String viaPath = extractViaPath(pkg.raw); // Extrae el viaPath
                float audioLevel = extractAudioLevel(pkg.raw); // Extrae el nivel de audio en dBV
                int counterIndex = -1;

                // Busca si ya existe el callsign
                for (int j = 0; j < counterSize; j++) {
                    if (packetCounters[j].callsign == callsign) {
                        counterIndex = j;
                        break;
                    }
                }

                // Si no existe, agrega uno nuevo
                if (counterIndex == -1 && counterSize < MAX_CALLSIGNS) {
                    packetCounters[counterSize].callsign = callsign;
                    packetCounters[counterSize].viaPath = viaPath;
                    packetCounters[counterSize].count = 1;
                    packetCounters[counterSize].audio = audioLevel; // Asigna el nivel de audio
                    counterSize++;
                } else if (counterIndex != -1) {
                    packetCounters[counterIndex].count++;
                    packetCounters[counterIndex].audio = audioLevel; // Actualiza el nivel de audio
                }
            }
        }
    }

    // Construir y enviar JSON
    String jsonResponse = "{ \"lastHeard\": [";
    for (int i = 0; i < counterSize; i++) {
        jsonResponse += "{";
        jsonResponse += "\"callsign\":\"" + packetCounters[i].callsign + "\",";
        jsonResponse += "\"count\":" + String(packetCounters[i].count) + ",";
        jsonResponse += "\"viaPath\":\"" + packetCounters[i].viaPath + "\",";
        jsonResponse += "\"dx\":" + String(packetCounters[i].dx) + ",";
        jsonResponse += "\"packet\":" + String(packetCounters[i].packet) + ",";
        jsonResponse += "\"audio\":" + String(packetCounters[i].audio) + ",";
        jsonResponse += "\"icon\":\"https://aprs.p00lack.cc/symbols/icons/" + String(packetCounters[i].icon) + "-1.png\"";
        jsonResponse += "}";
        if (i < counterSize - 1) {
            jsonResponse += ",";
        }
    }
    jsonResponse += "] }";

    lastheard_events.send(jsonResponse.c_str(), "lastHeard");
}

void handle_radio_get(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/radio.html");

    // Generar opciones del select dinámicamente
    String rfTypeOptions;
    rfTypeOptions += "<option value=0 " + String(config.rf_type == 0 ? "selected" : "") + ">NONE</option>";
    rfTypeOptions += "<option value=1 " + String(config.rf_type == 1 ? "selected" : "") + ">SA868_VHF</option>";
    rfTypeOptions += "<option value=2 " + String(config.rf_type == 2 ? "selected" : "") + ">SA868_UHF</option>";

    // Reemplazar valores dinámicos
    html.replace("%VOLUME%", String(config.volume));
    html.replace("%SQL_LEVEL%", String(config.sql_level));
    html.replace("%TX_FREQ%", String(config.freq_tx, 4));
    html.replace("%RX_FREQ%", String(config.freq_rx, 4));
    html.replace("%TX_CTCSS%", String(config.tone_tx));
    html.replace("%RX_CTCSS%", String(config.tone_rx));
    html.replace("%RF_TYPE_OPTIONS%", rfTypeOptions);
    html.replace("%RADIO_ENABLE%", config.rf_en ? "checked" : "");
    html.replace("%TX_POWER_SELECTED_HIGH%", config.rf_power ? "selected" : "");
    html.replace("%TX_POWER_SELECTED_LOW%", !config.rf_power ? "selected" : "");
    html.replace("%NARROW_WIDE_SELECTED_NARROW%", !config.band ? "selected" : "");
    html.replace("%NARROW_WIDE_SELECTED_WIDE%", config.band ? "selected" : "");

    request->send(200, "text/html", html);
}

void handle_radio_post(AsyncWebServerRequest *request) {
    if (request->hasArg("volume")) {
        config.volume = request->arg("volume").toInt();
    }

    if (request->hasArg("sqlLevel")) {
        config.sql_level = request->arg("sqlLevel").toInt();
    }

    if (request->hasArg("tx_freq")) {
        config.freq_tx = request->arg("tx_freq").toFloat();
    }

    if (request->hasArg("rx_freq")) {
        config.freq_rx = request->arg("rx_freq").toFloat();
    }

    if (request->hasArg("tx_ctcss")) {
        config.tone_tx = request->arg("tx_ctcss").toInt();
    }

    if (request->hasArg("rx_ctcss")) {
        config.tone_rx = request->arg("rx_ctcss").toInt();
    }

    if (request->hasArg("rf_type")) {
        config.rf_type = request->arg("rf_type").toInt();
    }

    if (request->hasArg("radioEnable")) {
        config.rf_en = true;
    } else {
        config.rf_en = false;
    }

    if (request->hasArg("txPower")) {
        config.rf_power = (request->arg("txPower") == "HIGH");
    }

    if (request->hasArg("narrowWide")) {
        config.band = (request->arg("narrowWide") == "1");
    }

    saveEEPROM(); // Guardar cambios
    request->redirect("/radio"); // Redirigir a la página
}


String getLocalDateTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "No disponible";
    }
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return String(buffer);
}

void handle_uart_config(AsyncWebServerRequest *request) {
    // Cargar el archivo HTML desde SPIFFS o LittleFS
    String html = loadHtmlTemplate("/mod_uart.html");

    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo /mod_uart.html no encontrado");
        return;
    }

    // Reemplazar valores dinámicos para UART0
    html.replace("%UART0_ENABLED%", config.uart0_enable ? "checked" : "");
    html.replace("%UART0_RX_GPIO%", String(config.uart0_rx_gpio));
    html.replace("%UART0_TX_GPIO%", String(config.uart0_tx_gpio));
    html.replace("%UART0_RTS_GPIO%", String(config.uart0_rts_gpio));
    html.replace("%UART0_BAUDRATE%", generateBaudrateOptions(config.uart0_baudrate));

    // Reemplazar valores dinámicos para UART1
    html.replace("%UART1_ENABLED%", config.uart1_enable ? "checked" : "");
    html.replace("%UART1_RX_GPIO%", String(config.uart1_rx_gpio));
    html.replace("%UART1_TX_GPIO%", String(config.uart1_tx_gpio));
    html.replace("%UART1_RTS_GPIO%", String(config.uart1_rts_gpio));
    html.replace("%UART1_BAUDRATE%", generateBaudrateOptions(config.uart1_baudrate));

    // Reemplazar valores dinámicos para UART2
    html.replace("%UART2_ENABLED%", config.uart2_enable ? "checked" : "");
    html.replace("%UART2_RX_GPIO%", String(config.uart2_rx_gpio));
    html.replace("%UART2_TX_GPIO%", String(config.uart2_tx_gpio));
    html.replace("%UART2_RTS_GPIO%", String(config.uart2_rts_gpio));
    html.replace("%UART2_BAUDRATE%", generateBaudrateOptions(config.uart2_baudrate));

    // Enviar el HTML ensamblado al cliente
    request->send(200, "text/html", html);
}

void handle_uart_post(AsyncWebServerRequest *request) {
    // Variables para almacenar los valores recibidos con valores por defecto
    bool uart0_enabled = config.uart0_enable;
    bool uart1_enabled = config.uart1_enable;
    bool uart2_enabled = config.uart2_enable;
    int uart0_rx_gpio = config.uart0_rx_gpio;
    int uart0_tx_gpio = config.uart0_tx_gpio;
    int uart0_rts_gpio = config.uart0_rts_gpio;
    int uart0_baudrate = config.uart0_baudrate;
    int uart1_rx_gpio = config.uart1_rx_gpio;
    int uart1_tx_gpio = config.uart1_tx_gpio;
    int uart1_rts_gpio = config.uart1_rts_gpio;
    int uart1_baudrate = config.uart1_baudrate;
    int uart2_rx_gpio = config.uart2_rx_gpio;
    int uart2_tx_gpio = config.uart2_tx_gpio;
    int uart2_rts_gpio = config.uart2_rts_gpio;
    int uart2_baudrate = config.uart2_baudrate;

    // Función lambda para leer parámetros de tipo int
    auto getIntParam = [&](const String &param, int &var) {
        if (request->hasParam(param, true)) {
            var = request->getParam(param, true)->value().toInt();
        }
    };

    // Leer parámetros para UART0
    if (request->hasParam("uart0_enable", true)) {
        uart0_enabled = request->getParam("uart0_enable", true)->value() == "on";
    }
    getIntParam("uart0_rx_gpio", uart0_rx_gpio);
    getIntParam("uart0_tx_gpio", uart0_tx_gpio);
    getIntParam("uart0_rts_gpio", uart0_rts_gpio);
    getIntParam("uart0_baudrate", uart0_baudrate);

    // Leer parámetros para UART1
    if (request->hasParam("uart1_enable", true)) {
        uart1_enabled = request->getParam("uart1_enable", true)->value() == "on";
    }
    getIntParam("uart1_rx_gpio", uart1_rx_gpio);
    getIntParam("uart1_tx_gpio", uart1_tx_gpio);
    getIntParam("uart1_rts_gpio", uart1_rts_gpio);
    getIntParam("uart1_baudrate", uart1_baudrate);

    // Leer parámetros para UART2
    if (request->hasParam("uart2_enable", true)) {
        uart2_enabled = request->getParam("uart2_enable", true)->value() == "on";
    }
    getIntParam("uart2_rx_gpio", uart2_rx_gpio);
    getIntParam("uart2_tx_gpio", uart2_tx_gpio);
    getIntParam("uart2_rts_gpio", uart2_rts_gpio);
    getIntParam("uart2_baudrate", uart2_baudrate);

    // Guardar valores en la configuración
    config.uart0_enable = uart0_enabled;
    config.uart0_rx_gpio = uart0_rx_gpio;
    config.uart0_tx_gpio = uart0_tx_gpio;
    config.uart0_rts_gpio = uart0_rts_gpio;
    config.uart0_baudrate = uart0_baudrate;

    config.uart1_enable = uart1_enabled;
    config.uart1_rx_gpio = uart1_rx_gpio;
    config.uart1_tx_gpio = uart1_tx_gpio;
    config.uart1_rts_gpio = uart1_rts_gpio;
    config.uart1_baudrate = uart1_baudrate;

    config.uart2_enable = uart2_enabled;
    config.uart2_rx_gpio = uart2_rx_gpio;
    config.uart2_tx_gpio = uart2_tx_gpio;
    config.uart2_rts_gpio = uart2_rts_gpio;
    config.uart2_baudrate = uart2_baudrate;

    // Confirmar que se actualizaron los valores
    request->send(200, "application/json", "{\"status\":\"success\"}");
}

void handle_rf_config(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/mod_rf.html");

    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo /mod_rf.html no encontrado");
        return;
    }

    // Reemplazar valores dinámicos de RF
    html.replace("%RF_BAUDRATE%", generateBaudrateOptions(config.rf_baudrate));
    html.replace("%RF_RX_GPIO%", String(config.rf_rx_gpio));
    html.replace("%RF_TX_GPIO%", String(config.rf_tx_gpio));
    html.replace("%RF_PD_GPIO%", String(config.rf_pd_gpio));
    html.replace("%RF_PWR_GPIO%", String(config.rf_pwr_gpio));
    html.replace("%RF_PTT_GPIO%", String(config.rf_ptt_gpio));
    html.replace("%RF_SQL_GPIO%", String(config.rf_sql_gpio));
    html.replace("%RF_ATTEN%", String(config.adc_atten));
    html.replace("%RF_DC_OFFSET%", String(config.adc_dc_offset));

    // Reemplazar dinámicamente los valores para "Active" (LOW y HIGH)
    html.replace("%rf_pd_active_low%", config.rf_pd_active == 0 ? "checked" : "");
    html.replace("%rf_pd_active_high%", config.rf_pd_active == 1 ? "checked" : "");

    html.replace("%rf_pwr_active_low%", config.rf_pwr_active == 0 ? "checked" : "");
    html.replace("%rf_pwr_active_high%", config.rf_pwr_active == 1 ? "checked" : "");

    html.replace("%rf_sql_active_low%", config.rf_sql_active == 0 ? "checked" : "");
    html.replace("%rf_sql_active_high%", config.rf_sql_active == 1 ? "checked" : "");

    html.replace("%rf_ptt_active_low%", config.rf_ptt_active == 0 ? "checked" : "");
    html.replace("%rf_ptt_active_high%", config.rf_ptt_active == 1 ? "checked" : "");

    // Enviar respuesta al cliente
    request->send(200, "text/html", html);
}

void handle_rf_post(AsyncWebServerRequest *request) {
    // Variables para almacenar los valores recibidos con valores por defecto
    int rf_baudrate = config.rf_baudrate;
    int rf_rx_gpio = config.rf_rx_gpio;
    int rf_tx_gpio = config.rf_tx_gpio;
    int rf_pd_gpio = config.rf_pd_gpio;
    int rf_pwr_gpio = config.rf_pwr_gpio;
    int rf_ptt_gpio = config.rf_ptt_gpio;
    int rf_sql_gpio = config.rf_sql_gpio;
    int rf_pd_active = config.rf_pd_active;
    int rf_pwr_active = config.rf_pwr_active;
    int rf_sql_active = config.rf_sql_active;
    int rf_ptt_active = config.rf_ptt_active;
    int rf_atten = config.adc_atten;
    int rf_dc_offset = config.adc_dc_offset;

    // Función lambda para leer parámetros y evitar repetición de código
    auto getParamValue = [&](const String &param, int &var) {
        if (request->hasParam(param, true)) {
            var = request->getParam(param, true)->value().toInt();
        }
    };

    // Leer todos los parámetros
    getParamValue("rf_baudrate", rf_baudrate);
    getParamValue("rf_rx_gpio", rf_rx_gpio);
    getParamValue("rf_tx_gpio", rf_tx_gpio);
    getParamValue("rf_pd_gpio", rf_pd_gpio);
    getParamValue("rf_pwr_gpio", rf_pwr_gpio);
    getParamValue("rf_ptt_gpio", rf_ptt_gpio);
    getParamValue("rf_sql_gpio", rf_sql_gpio);
    getParamValue("rf_pd_active", rf_pd_active);
    getParamValue("rf_pwr_active", rf_pwr_active);
    getParamValue("rf_sql_active", rf_sql_active);
    getParamValue("rf_ptt_active", rf_ptt_active);
    getParamValue("rf_adc_atten", rf_atten);
    getParamValue("rf_adc_dc_offset", rf_dc_offset);

    // Guardar valores en la configuración
    config.rf_baudrate = rf_baudrate;
    config.rf_rx_gpio = rf_rx_gpio;
    config.rf_tx_gpio = rf_tx_gpio;
    config.rf_pd_gpio = rf_pd_gpio;
    config.rf_pwr_gpio = rf_pwr_gpio;
    config.rf_ptt_gpio = rf_ptt_gpio;
    config.rf_sql_gpio = rf_sql_gpio;
    config.rf_pd_active = rf_pd_active;
    config.rf_pwr_active = rf_pwr_active;
    config.rf_sql_active = rf_sql_active;
    config.rf_ptt_active = rf_ptt_active;
    config.adc_atten = rf_atten;
    config.adc_dc_offset = rf_dc_offset;

    // Confirmar que se actualizaron los valores
    request->send(200, "application/json", "{\"status\":\"success\"}");
}

void handle_gnss_config(AsyncWebServerRequest *request) {
    // Cargar el archivo HTML desde SPIFFS o LittleFS
    String html = loadHtmlTemplate("/mod_gnss.html");

    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo /mod_gnss.html no encontrado");
        return;
    }

    // Reemplazo de valores dinámicos
    html.replace("%GNSS_ENABLED%", config.gnss_enable ? "checked" : "");
    html.replace("%GNSS_CHANNEL%", String(config.gnss_channel));
    html.replace("%GNSS_AT_COMMAND%", String(config.gnss_at_command));
    html.replace("%GNSS_TCP_HOST%", String(config.gnss_tcp_host));
    html.replace("%GNSS_TCP_PORT%", String(config.gnss_tcp_port));

    // Configurar el dropdown para GNSS_CHANNEL
    for (int i = 0; i <= 4; i++) {
        String placeholder = "%GNSS_CHANNEL_SELECTED_" + String(i) + "%";
        String replacement = (config.gnss_channel == i) ? "selected" : "";
        html.replace(placeholder, replacement);
    }

    // Enviar la respuesta al cliente
    request->send(200, "text/html", html);
}

void handle_gnss_post(AsyncWebServerRequest *request) {
    // Variables para almacenar los valores recibidos
    bool gnss_enabled = config.gnss_enable;
    int gnss_channel = config.gnss_channel;
    int gnss_tcp_port = config.gnss_tcp_port;
    String gnss_at_command, gnss_tcp_host;

    // Función lambda para leer parámetros de tipo int
    auto getIntParam = [&](const String &param, int &var) {
        if (request->hasParam(param, true)) {
            var = request->getParam(param, true)->value().toInt();
        }
    };

    // Leer parámetros
    if (request->hasParam("gnss_enable", true)) {
        gnss_enabled = request->getParam("gnss_enable", true)->value() == "on";
    }
    getIntParam("gnss_channel", gnss_channel);
    getIntParam("gnss_tcp_port", gnss_tcp_port);

    if (request->hasParam("gnss_at_command", true)) {
        gnss_at_command = request->getParam("gnss_at_command", true)->value();
        strncpy(config.gnss_at_command, gnss_at_command.c_str(), sizeof(config.gnss_at_command) - 1);
        config.gnss_at_command[sizeof(config.gnss_at_command) - 1] = '\0'; // Asegurar terminador nulo
    }

    if (request->hasParam("gnss_tcp_host", true)) {
        gnss_tcp_host = request->getParam("gnss_tcp_host", true)->value();
        strncpy(config.gnss_tcp_host, gnss_tcp_host.c_str(), sizeof(config.gnss_tcp_host) - 1);
        config.gnss_tcp_host[sizeof(config.gnss_tcp_host) - 1] = '\0'; // Asegurar terminador nulo
    }

    // Guardar valores en la configuración
    config.gnss_enable = gnss_enabled;
    config.gnss_channel = gnss_channel;
    config.gnss_tcp_port = gnss_tcp_port;

    // Confirmar que se actualizaron los valores
    request->send(200, "application/json", "{\"status\":\"success\"}");
}

void handle_modbus_config(AsyncWebServerRequest *request) {
    // Cargar el archivo HTML desde SPIFFS o LittleFS
    String html = loadHtmlTemplate("/mod_modbus.html");

    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo /mod_modbus.html no encontrado");
        return;
    }

    // Reemplazo de valores dinámicos para MODBUS
    html.replace("%MODBUS_ENABLED%", config.modbus_enable ? "checked" : "");
    html.replace("%MODBUS_ADDRESS%", String(config.modbus_address));
    html.replace("%MODBUS_DE_GPIO%", String(config.modbus_de_gpio));

    // Configurar el dropdown para MODBUS_CHANNEL
    for (int i = 0; i <= 4; i++) {
        String placeholder = "%MODBUS_CHANNEL_SELECTED_" + String(i) + "%";
        String replacement = (config.modbus_channel == i) ? "selected" : "";
        html.replace(placeholder, replacement);
    }

    // Reemplazo de valores dinámicos para TNC
    html.replace("%TNC_ENABLED%", config.ext_tnc_enable ? "checked" : "");

    // Configurar el dropdown para TNC_CHANNEL
    for (int i = 0; i <= 3; i++) {
        String placeholder = "%TNC_CHANNEL_SELECTED_" + String(i) + "%";
        String replacement = (config.ext_tnc_channel == i) ? "selected" : "";
        html.replace(placeholder, replacement);
    }

    // Configurar el dropdown para TNC_MODE
    for (int i = 0; i <= 3; i++) {
        String placeholder = "%TNC_MODE_SELECTED_" + String(i) + "%";
        String replacement = (config.ext_tnc_mode == i) ? "selected" : "";
        html.replace(placeholder, replacement);
    }

    // Enviar la respuesta al cliente
    request->send(200, "text/html", html);
}

void handle_modbus_post(AsyncWebServerRequest *request) {
    // Variables para almacenar los valores recibidos con valores por defecto
    bool modbus_enabled = config.modbus_enable;
    bool tnc_enabled = config.ext_tnc_enable;
    int modbus_address = config.modbus_address;
    int modbus_de_gpio = config.modbus_de_gpio;
    int modbus_channel = config.modbus_channel;
    int tnc_channel = config.ext_tnc_channel;
    int tnc_mode = config.ext_tnc_mode;

    // Función lambda para leer parámetros de tipo int
    auto getIntParam = [&](const String &param, int &var) {
        if (request->hasParam(param, true)) {
            var = request->getParam(param, true)->value().toInt();
        }
    };

    // Leer los parámetros
    if (request->hasParam("modbus_enable", true)) {
        modbus_enabled = request->getParam("modbus_enable", true)->value() == "on";
    }
    getIntParam("modbus_address", modbus_address);
    getIntParam("modbus_de_gpio", modbus_de_gpio);
    getIntParam("modbus_channel", modbus_channel);

    if (request->hasParam("tnc_enable", true)) {
        tnc_enabled = request->getParam("tnc_enable", true)->value() == "on";
    }
    getIntParam("tnc_channel", tnc_channel);
    getIntParam("tnc_mode", tnc_mode);

    // Guardar valores en la configuración
    config.modbus_enable = modbus_enabled;
    config.modbus_address = modbus_address;
    config.modbus_de_gpio = modbus_de_gpio;
    config.modbus_channel = modbus_channel;

    config.ext_tnc_enable = tnc_enabled;
    config.ext_tnc_channel = tnc_channel;
    config.ext_tnc_mode = tnc_mode;

    // Confirmar que se actualizaron los valores
    request->send(200, "application/json", "{\"status\":\"success\"}");
}

void handle_counter_config(AsyncWebServerRequest *request) {
    // Cargar el archivo HTML desde SPIFFS o LittleFS
    String html = loadHtmlTemplate("/mod_counter.html");

    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo /mod_counter.html no encontrado");
        return;
    }

    // Reemplazo de valores dinámicos para Counter 0
    html.replace("%COUNTER0_ENABLED%", config.counter0_enable ? "checked" : "");
    html.replace("%COUNTER0_GPIO%", String(config.counter0_gpio));
    html.replace("%COUNTER0_ACTIVE_LOW%", config.counter0_active == 0 ? "checked" : "");
    html.replace("%COUNTER0_ACTIVE_HIGH%", config.counter0_active == 1 ? "checked" : "");

    // Reemplazo de valores dinámicos para Counter 1
    html.replace("%COUNTER1_ENABLED%", config.counter1_enable ? "checked" : "");
    html.replace("%COUNTER1_GPIO%", String(config.counter1_gpio));
    html.replace("%COUNTER1_ACTIVE_LOW%", config.counter1_active == 0 ? "checked" : "");
    html.replace("%COUNTER1_ACTIVE_HIGH%", config.counter1_active == 1 ? "checked" : "");

    // Enviar la respuesta al cliente
    request->send(200, "text/html", html);
}

void handle_counter_post(AsyncWebServerRequest *request) {
    // Variables para almacenar los valores recibidos con valores por defecto
    bool counter0_enabled = config.counter0_enable;
    bool counter1_enabled = config.counter1_enable;
    int counter0_gpio = config.counter0_gpio;
    int counter1_gpio = config.counter1_gpio;
    int counter0_active = config.counter0_active;
    int counter1_active = config.counter1_active;

    // Función lambda para leer parámetros de tipo int
    auto getIntParam = [&](const String &param, int &var) {
        if (request->hasParam(param, true)) {
            var = request->getParam(param, true)->value().toInt();
        }
    };

    // Leer los parámetros
    if (request->hasParam("counter0_enable", true)) {
        counter0_enabled = request->getParam("counter0_enable", true)->value() == "on";
    }
    getIntParam("counter0_gpio", counter0_gpio);
    
    if (request->hasParam("counter0_active", true)) {
        String activeValue = request->getParam("counter0_active", true)->value();
        counter0_active = (activeValue == "HIGH") ? 1 : 0;
    }

    if (request->hasParam("counter1_enable", true)) {
        counter1_enabled = request->getParam("counter1_enable", true)->value() == "on";
    }
    getIntParam("counter1_gpio", counter1_gpio);

    if (request->hasParam("counter1_active", true)) {
        String activeValue = request->getParam("counter1_active", true)->value();
        counter1_active = (activeValue == "HIGH") ? 1 : 0;
    }

    // Guardar valores en la configuración
    config.counter0_enable = counter0_enabled;
    config.counter0_gpio = counter0_gpio;
    config.counter0_active = counter0_active;

    config.counter1_enable = counter1_enabled;
    config.counter1_gpio = counter1_gpio;
    config.counter1_active = counter1_active;

    // Confirmar que se actualizaron los valores
    request->send(200, "application/json", "{\"status\":\"success\"}");
}

void handle_i2c_config(AsyncWebServerRequest *request) {
    // Cargar el archivo HTML para I2C desde SPIFFS o LittleFS
    String html = loadHtmlTemplate("/mod_i2c.html");

    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo /mod_i2c.html no encontrado");
        return;
    }

    // Reemplazo de valores dinámicos para I2C_0
    html.replace("%I2C0_ENABLED%", config.i2c_enable ? "checked" : "");
    html.replace("%I2C0_SDA_GPIO%", String(config.i2c_sda_pin));
    html.replace("%I2C0_SCK_GPIO%", String(config.i2c_sck_pin));
    html.replace("%I2C0_FREQ%", String(config.i2c_freq));

    // Reemplazo de valores dinámicos para I2C_1
    html.replace("%I2C1_ENABLED%", config.i2c1_enable ? "checked" : "");
    html.replace("%I2C1_SDA_GPIO%", String(config.i2c1_sda_pin));
    html.replace("%I2C1_SCK_GPIO%", String(config.i2c1_sck_pin));
    html.replace("%I2C1_FREQ%", String(config.i2c1_freq));

    // Enviar la respuesta al cliente
    request->send(200, "text/html", html);
}

void handle_i2c_post(AsyncWebServerRequest *request) {
    // Variables para almacenar los valores recibidos con valores por defecto
    bool i2c0_enabled = config.i2c_enable;
    bool i2c1_enabled = config.i2c1_enable;
    int i2c0_sda = config.i2c_sda_pin;
    int i2c0_sck = config.i2c_sck_pin;
    int i2c0_freq = config.i2c_freq;
    int i2c1_sda = config.i2c1_sda_pin;
    int i2c1_sck = config.i2c1_sck_pin;
    int i2c1_freq = config.i2c1_freq;

    // Función lambda para leer parámetros de tipo int
    auto getIntParam = [&](const String &param, int &var) {
        if (request->hasParam(param, true)) {
            var = request->getParam(param, true)->value().toInt();
        }
    };

    // Leer los parámetros
    if (request->hasParam("i2c0_enable", true)) {
        i2c0_enabled = request->getParam("i2c0_enable", true)->value() == "on";
    }
    getIntParam("i2c0_sda_gpio", i2c0_sda);
    getIntParam("i2c0_sck_gpio", i2c0_sck);
    getIntParam("i2c0_freq", i2c0_freq);

    if (request->hasParam("i2c1_enable", true)) {
        i2c1_enabled = request->getParam("i2c1_enable", true)->value() == "on";
    }
    getIntParam("i2c1_sda_gpio", i2c1_sda);
    getIntParam("i2c1_sck_gpio", i2c1_sck);
    getIntParam("i2c1_freq", i2c1_freq);

    // Guardar valores en la configuración
    config.i2c_enable = i2c0_enabled;
    config.i2c_sda_pin = i2c0_sda;
    config.i2c_sck_pin = i2c0_sck;
    config.i2c_freq = i2c0_freq;

    config.i2c1_enable = i2c1_enabled;
    config.i2c1_sda_pin = i2c1_sda;
    config.i2c1_sck_pin = i2c1_sck;
    config.i2c1_freq = i2c1_freq;

    // Confirmar que se actualizaron los valores
    request->send(200, "application/json", "{\"status\":\"success\"}");
}

// Manejar solicitud GET para /system
void handle_system_get(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    String html = loadHtmlTemplate("/system.html");

    // Reemplazo de valores dinámicos
    html.replace("%localDateTime%", getLocalDateTime());
    html.replace("%ntpHost%", String(config.ntp_host));
    html.replace("%webUser%", String(config.http_username));
    html.replace("%webPassword%", String(config.http_password));

    for (int i = 1; i <= 4; i++) {
        char placeholder[10];
        snprintf(placeholder, sizeof(placeholder), "%%path%d%%", i);
        html.replace(placeholder, String(config.path[i - 1]));
    }

    html.replace("%oledEnable%", config.oled_enable ? "checked" : "");
    html.replace("%txDisplay%", config.tx_display ? "checked" : "");
    html.replace("%rxDisplay%", config.rx_display ? "checked" : "");
    html.replace("%headUp%", config.h_up ? "checked" : "");
    html.replace("%popupDelay%", String(config.dispDelay));
    html.replace("%rxChannelRF%", config.dispRF ? "checked" : "");
    html.replace("%rxChannelINET%", config.dispINET ? "checked" : "");
    html.replace("%filterDx%", String(config.filterDistant));
    html.replace("%filters%", String(config.dispFilter));

    // Reemplazo dinámico de la zona horaria
    for (int tz = -12; tz <= 12; ++tz) {
        String placeholder = "%timeZoneSelected" + String(tz) + "%";
        html.replace(placeholder, (config.timeZone == tz) ? "selected" : "");
    }

    request->send(200, "text/html", html);
}

// Manejar solicitud POST para /system
void handle_system_post(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    // Lógica existente para configuración de zona horaria y NTP
    if (request->hasArg("SetTimeZone")) {
        String timeZone = request->arg("SetTimeZone");
        config.timeZone = timeZone.toFloat();
        configTime(3600 * config.timeZone, 0, config.ntp_host);
        saveEEPROM();
    } else if (request->hasArg("SetTimeNtp")) {
        String ntpHost = request->arg("SetTimeNtp");
        strcpy(config.ntp_host, ntpHost.c_str());
        configTime(3600 * config.timeZone, 0, config.ntp_host);
        saveEEPROM();
    } else if (request->hasArg("SetTime")) {
        String dateTime = request->arg("SetTime");
        String date = dateTime.substring(0, 10);
        String time = dateTime.substring(11);

        int yyyy = date.substring(0, 4).toInt();
        int mm = date.substring(5, 7).toInt();
        int dd = date.substring(8, 10).toInt();
        int hh = time.substring(0, 2).toInt();
        int ii = time.substring(3, 5).toInt();
        int ss = time.substring(6, 8).toInt();

        tmElements_t timeinfo;
        timeinfo.Year = yyyy - 1970;
        timeinfo.Month = mm;
        timeinfo.Day = dd;
        timeinfo.Hour = hh;
        timeinfo.Minute = ii;
        timeinfo.Second = ss;

        time_t timeStamp = makeTime(timeinfo);
        time_t rtc = timeStamp - (config.timeZone * 3600);
        timeval tv = {rtc, 0};
        timezone tz = {0, 0};
        settimeofday(&tv, &tz);

        saveEEPROM();
    } else if (request->hasArg("REBOOT")) {
        request->send(200, "text/html", "Reiniciando sistema");
        ESP.restart();
        return;
    }

    // Nueva lógica para Web Authentication
    if (request->hasArg("WebUser")) {
        String webUser = request->arg("WebUser");
        strncpy(config.http_username, webUser.c_str(), sizeof(config.http_username));
    }

    if (request->hasArg("WebPassword")) {
        String webPassword = request->arg("WebPassword");
        strncpy(config.http_password, webPassword.c_str(), sizeof(config.http_password));
    }

	// Nueva lógica para PATH USER Define
	for (int i = 1; i <= 4; i++) {
		String argName = "Path_" + String(i);
		if (request->hasArg(argName.c_str())) { // Convertir a const char* con c_str()
			size_t length = sizeof(config.path[i - 1]) - 1; // Reservar espacio para el null-terminator
			strncpy(config.path[i - 1], request->arg(argName.c_str()).c_str(), length);
			config.path[i - 1][length] = '\0'; // Asegurarse de que termine con null
		}
	}

    // Nueva lógica para Display Settings
    if (request->hasArg("OLEDEnable")) {
        config.oled_enable = (request->arg("OLEDEnable") == "OK");
    }

    if (request->hasArg("TXDisplay")) {
        config.tx_display = (request->arg("TXDisplay") == "OK");
    }

    if (request->hasArg("RXDisplay")) {
        config.rx_display = (request->arg("RXDisplay") == "OK");
    }

    if (request->hasArg("HeadUp")) {
        config.h_up = (request->arg("HeadUp") == "OK");
    }

    if (request->hasArg("PopupDelay")) {
        config.dispDelay = request->arg("PopupDelay").toInt();
    }

    if (request->hasArg("RXChannel_RF")) {
        config.dispRF = request->arg("RXChannel").toInt();
    }

	if (request->hasArg("RXChannel_INET")) {
        config.dispINET = request->arg("RXChannel").toInt();
    }

    if (request->hasArg("FilterDX")) {
        config.filterDistant = request->arg("FilterDX").toFloat();
    }

    if (request->hasArg("Filters")) {
        String filters = request->arg("Filters");
        config.dispFilter = filters.toInt();
    }

    // Guardar cambios en EEPROM
    saveEEPROM();

    // Responder con éxito
    request->send(200, "text/html", "Configuración guardada.");
}
// --- Funciones para la configuración GENERAL ---
void handleIgateGeneralGet(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/igate_general.html");
    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }
    // Reemplazar marcadores de posición de la configuración general
    html.replace("%IGATE_ENABLE%", config.igate_en ? "checked" : "");
    html.replace("%MY_CALL%", String(config.aprs_mycall));
    html.replace("%MY_SSID%", String(config.aprs_ssid));
    html.replace("%IGATE_TABLE%", String(config.igate_symbol[0]));
    html.replace("%IGATE_SYMBOL%", String(config.igate_symbol[1]));
    html.replace("%IGATE_COMMENT%", String(config.igate_comment));
    html.replace("%RF2INET%", config.rf2inet ? "checked" : "");
    html.replace("%INET2RF%", config.inet2rf ? "checked" : "");
    request->send(200, "text/html", html);
}

void handleIgateGeneralPost(AsyncWebServerRequest *request) {
    if (request->hasArg("igateEnable")) {
        config.igate_en = (request->arg("igateEnable") == "OK");
    }
    if (request->hasArg("myCall")) {
        String myCall = request->arg("myCall");
        myCall.trim();
        myCall.toUpperCase();
        strncpy(config.aprs_mycall, myCall.c_str(), sizeof(config.aprs_mycall));
    }
    if (request->hasArg("mySSID")) {
        config.aprs_ssid = request->arg("mySSID").toInt();
        if (config.aprs_ssid > 15) config.aprs_ssid = 13;
    }
    if (request->hasArg("igateTable")) {
        config.igate_symbol[0] = request->arg("igateTable")[0];
    }
    if (request->hasArg("igateSymbol")) {
        config.igate_symbol[1] = request->arg("igateSymbol")[0];
    }
    if (request->hasArg("igateComment")) {
        strncpy(config.igate_comment, request->arg("igateComment").c_str(), sizeof(config.igate_comment));
    }
    // Los checkbox se manejan así: si se incluye el parámetro se considera true, de lo contrario false.
    config.rf2inet = request->hasArg("rf2inetEnable");
    config.inet2rf = request->hasArg("inet2rfEnable");

    saveEEPROM(); // Guardar cambios en EEPROM
    request->redirect("/igate/general");
}

// --- Funciones para la configuración POSITION ---
void handleIgatePositionGet(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/igate_position.html");
    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }
    // Reemplazar marcadores para la posición
    html.replace("%IGATE_OBJECT%", (config.igate_object && config.igate_object[0] != '\0') ? String(config.igate_object) : "");
    html.replace("%IGATE_LAT%", String(config.igate_lat, 5));
    html.replace("%IGATE_LON%", String(config.igate_lon, 5));
    html.replace("%IGATE_ALT%", String(config.igate_alt, 2));
    request->send(200, "text/html", html);
}

void handleIgatePositionPost(AsyncWebServerRequest *request) {
    if (request->hasArg("igateObject")) {
        strncpy(config.igate_object, request->arg("igateObject").c_str(), sizeof(config.igate_object));
    }
    if (request->hasArg("igateLat")) {
        config.igate_lat = request->arg("igateLat").toFloat();
    }
    if (request->hasArg("igateLon")) {
        config.igate_lon = request->arg("igateLon").toFloat();
    }
    if (request->hasArg("igateAlt")) {
        config.igate_alt = request->arg("igateAlt").toFloat();
    }
    saveEEPROM();
    request->redirect("/igate/position");
}

// --- Funciones para la configuración PHG ---
void handleIgatePHGGet(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/igate_phg.html");
    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }
    // En este ejemplo se maneja solo la opción PHG (ej. igate_bcn)
    html.replace("%IGATE_BCN%", config.igate_bcn ? "checked" : "");
    request->send(200, "text/html", html);
}

void handleIgatePHGPost(AsyncWebServerRequest *request) {
    // Suponiendo que el checkbox se llama "igateBcnEnable"
    config.igate_bcn = request->hasArg("igateBcnEnable");
    saveEEPROM();
    request->redirect("/igate/phg");
}

// --- Funciones para la configuración FILTER ---
void handleIgateFilterGet(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/igate_filter.html");
    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }
    // Reemplazar marcadores para la configuración del filtro APRS
    html.replace("%APRS_HOST%", String(config.aprs_host));
    html.replace("%APRS_PORT%", String(config.aprs_port));
    html.replace("%APRS_FILTER%", String(config.aprs_filter));
    request->send(200, "text/html", html);
}

void handleIgateFilterPost(AsyncWebServerRequest *request) {
    if (request->hasArg("aprsHost")) {
        strncpy(config.aprs_host, request->arg("aprsHost").c_str(), sizeof(config.aprs_host));
    }
    if (request->hasArg("aprsPort")) {
        config.aprs_port = request->arg("aprsPort").toInt();
    }
    if (request->hasArg("aprsFilter")) {
        strncpy(config.aprs_filter, request->arg("aprsFilter").c_str(), sizeof(config.aprs_filter));
    }
    saveEEPROM();
    request->redirect("/igate/filter");
}

void handleDigiGeneral(AsyncWebServerRequest *request)
{
    if (!request->authenticate(config.http_username, config.http_password))
    {
        return request->requestAuthentication();
    }

    // Cargar la plantilla HTML desde SPIFFS
    String html = loadHtmlTemplate("/digi_general.html");
    if (html.isEmpty())
    {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    // Reemplazar marcadores de posición con los valores de configuración
    html.replace("%DIGI_ENABLE%", config.digi_en ? "checked" : "");
    html.replace("%STATION_CALLSIGN%", String(config.digi_mycall));
    html.replace("%STATION_SSID%", String(config.digi_ssid));
    html.replace("%STATION_SYMBOL_TABLE%", String(config.digi_symbol[0]));
    html.replace("%STATION_SYMBOL%", String(config.digi_symbol[1]));
    html.replace("%DIGI_PATH%", String(config.digi_path));
    html.replace("%TEXT_COMMENT%", String(config.digi_comment));
    html.replace("%REPEAT_DELAY%", String(config.digi_delay));
    html.replace("%TIME_STAMP%", config.digi_timestamp ? "checked" : "");

    request->send(200, "text/html", html);
}

void handleDigiGeneralPost(AsyncWebServerRequest *request)
{
    if (!request->authenticate(config.http_username, config.http_password))
    {
        return request->requestAuthentication();
    }

    if (request->hasArg("enable"))
    {
        config.digi_en = request->arg("enable") == "on";
    }

    if (request->hasArg("stationCallsign"))
    {
        String myCall = request->arg("stationCallsign");
        myCall.trim();
        myCall.toUpperCase();
        strcpy(config.digi_mycall, myCall.c_str());
    }

    if (request->hasArg("stationSsid") && isValidNumber(request->arg("stationSsid")))
    {
        config.digi_ssid = request->arg("stationSsid").toInt();
        if (config.digi_ssid > 15)
            config.digi_ssid = 3;
    }

    if (request->hasArg("stationSymbolTable"))
    {
        config.digi_symbol[0] = request->arg("stationSymbolTable").charAt(0);
    }

    if (request->hasArg("stationSymbol"))
    {
        config.digi_symbol[1] = request->arg("stationSymbol").charAt(0);
    }

	if (request->hasArg("path") && isValidNumber(request->arg("path")))
	{
		config.digi_path = request->arg("path").toInt();
	}
	
    if (request->hasArg("textComment"))
    {
        String comment = request->arg("textComment");
        comment.trim();
        strncpy(config.digi_comment, comment.c_str(), sizeof(config.digi_comment) - 1);
    }

    if (request->hasArg("repeatDelay") && isValidNumber(request->arg("repeatDelay")))
    {
        config.digi_delay = request->arg("repeatDelay").toInt();
    }

    if (request->hasArg("timeStamp"))
    {
        config.digi_timestamp = request->arg("timeStamp") == "on";
    }

    saveEEPROM();
    request->send(200, "text/plain", "OK");
}

void handleDigiPosition(AsyncWebServerRequest *request)
{
    if (!request->authenticate(config.http_username, config.http_password))
    {
        return request->requestAuthentication();
    }

    // Cargar la plantilla HTML desde SPIFFS
    String html = loadHtmlTemplate("/digi_position.html");
    if (html.isEmpty())
    {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    // Reemplazar marcadores de posición con los valores de configuración
    html.replace("%DIGI_BEACON%", config.digi_bcn ? "checked" : "");
    html.replace("%DIGI_INTERVAL%", String(config.digi_interval));
    html.replace("%DIGI_POS_FIX%", config.digi_gps ? "" : "checked");
    html.replace("%DIGI_POS_GPS%", config.digi_gps ? "checked" : "");
    html.replace("%DIGI_LATITUDE%", String(config.digi_lat, 5));
    html.replace("%DIGI_LONGITUDE%", String(config.digi_lon, 5));
    html.replace("%DIGI_ALTITUDE%", String(config.digi_alt, 2));

    request->send(200, "text/html", html);
}

void handleDigiPositionPost(AsyncWebServerRequest *request)
{
    if (!request->authenticate(config.http_username, config.http_password))
    {
        return request->requestAuthentication();
    }

    if (request->hasArg("beacon"))
    {
        config.digi_bcn = request->arg("beacon") == "on";
    }

    if (request->hasArg("interval") && isValidNumber(request->arg("interval")))
    {
        config.digi_interval = request->arg("interval").toInt();
    }

    if (request->hasArg("location"))
    {
        config.digi_gps = (request->arg("location") == "gps");
    }

    if (request->hasArg("latitude") && isValidNumber(request->arg("latitude")))
    {
        config.digi_lat = request->arg("latitude").toFloat();
    }

    if (request->hasArg("longitude") && isValidNumber(request->arg("longitude")))
    {
        config.digi_lon = request->arg("longitude").toFloat();
    }

    if (request->hasArg("altitude") && isValidNumber(request->arg("altitude")))
    {
        config.digi_alt = request->arg("altitude").toFloat();
    }

    saveEEPROM();
    request->send(200, "text/plain", "OK");
}

void handleDigiPHG(AsyncWebServerRequest *request)
{
    if (!request->authenticate(config.http_username, config.http_password))
    {
        return request->requestAuthentication();
    }

    // Cargar la plantilla HTML desde SPIFFS
    String html = loadHtmlTemplate("/digi_phg.html");
    if (html.isEmpty())
    {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    request->send(200, "text/html", html);
}

void handleDigiPHGPost(AsyncWebServerRequest *request)
{
    if (!request->authenticate(config.http_username, config.http_password))
    {
        return request->requestAuthentication();
    }

    saveEEPROM();
    request->send(200, "text/plain", "OK");
}

void handleDigiFilter(AsyncWebServerRequest *request)
{
    if (!request->authenticate(config.http_username, config.http_password))
    {
        return request->requestAuthentication();
    }

    // Cargar la plantilla HTML desde SPIFFS
    String html = loadHtmlTemplate("/digi_filter.html");
    if (html.isEmpty())
    {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    // Reemplazar marcadores de posición con los valores de configuración de filtros
    html.replace("%FILTER_MESSAGE%", (config.digiFilter & FILTER_MESSAGE) ? "checked" : "");
    html.replace("%FILTER_STATUS%", (config.digiFilter & FILTER_STATUS) ? "checked" : "");
    html.replace("%FILTER_TELEMETRY%", (config.digiFilter & FILTER_TELEMETRY) ? "checked" : "");
    html.replace("%FILTER_WEATHER%", (config.digiFilter & FILTER_WX) ? "checked" : "");
    html.replace("%FILTER_OBJECT%", (config.digiFilter & FILTER_OBJECT) ? "checked" : "");
    html.replace("%FILTER_ITEM%", (config.digiFilter & FILTER_ITEM) ? "checked" : "");
    html.replace("%FILTER_QUERY%", (config.digiFilter & FILTER_QUERY) ? "checked" : "");
    html.replace("%FILTER_BUOY%", (config.digiFilter & FILTER_BUOY) ? "checked" : "");
    html.replace("%FILTER_POSITION%", (config.digiFilter & FILTER_POSITION) ? "checked" : "");

    request->send(200, "text/html", html);
}

void handleDigiFilterPost(AsyncWebServerRequest *request)
{
    if (!request->authenticate(config.http_username, config.http_password))
    {
        return request->requestAuthentication();
    }

    config.digiFilter = 0;

    if (request->hasArg("message"))
        config.digiFilter |= FILTER_MESSAGE;
    if (request->hasArg("status"))
        config.digiFilter |= FILTER_STATUS;
    if (request->hasArg("telemetry"))
        config.digiFilter |= FILTER_TELEMETRY;
    if (request->hasArg("weather"))
        config.digiFilter |= FILTER_WX;
    if (request->hasArg("object"))
        config.digiFilter |= FILTER_OBJECT;
    if (request->hasArg("item"))
        config.digiFilter |= FILTER_ITEM;
    if (request->hasArg("query"))
        config.digiFilter |= FILTER_QUERY;
    if (request->hasArg("buoy"))
        config.digiFilter |= FILTER_BUOY;
    if (request->hasArg("position"))
        config.digiFilter |= FILTER_POSITION;

    saveEEPROM();
    request->send(200, "text/plain", "OK");
}

void handleTrackerGeneral(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/tracker_general.html");
    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    // Reemplazar marcadores con los valores actuales de configuración
    html.replace("%TRACKER_ENABLE%", config.trk_en ? "checked" : "");
    html.replace("%SMART_BEACON%", config.trk_smartbeacon ? "checked" : "");
    html.replace("%COMPRESS_ENABLE%", config.trk_compress ? "checked" : "");
    html.replace("%MY_CALL%", String(config.trk_mycall));
    html.replace("%MY_SSID%", String(config.trk_ssid));
    html.replace("%TRACKER_COMMENT%", String(config.trk_comment));

    request->send(200, "text/html", html);
}

void handleTrackerGeneralPost(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    if (request->hasArg("trackerEnable")) config.trk_en = (request->arg("trackerEnable") == "OK");
    if (request->hasArg("smartBcnEnable")) config.trk_smartbeacon = (request->arg("smartBcnEnable") == "OK");
    if (request->hasArg("compressEnable")) config.trk_compress = (request->arg("compressEnable") == "OK");
    if (request->hasArg("myCall")) strcpy(config.trk_mycall, request->arg("myCall").c_str());
    if (request->hasArg("trackerComment")) strcpy(config.trk_comment, request->arg("trackerComment").c_str());

    saveEEPROM();
    request->send(200, "text/plain", "OK");
}

void handleTrackerPosition(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/tracker_position.html");
    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    html.replace("%LATITUDE%", String(config.trk_lat));
    html.replace("%LONGITUDE%", String(config.trk_lon));
    html.replace("%ALTITUDE%", String(config.trk_alt));
    html.replace("%INTERVAL%", String(config.trk_interval));
    html.replace("%POSITION_SOURCE%", config.trk_gps ? "GPS" : "Fix");

    request->send(200, "text/html", html);
}

void handleTrackerPositionPost(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    if (request->hasArg("trackerPosLat") && isValidNumber(request->arg("trackerPosLat"))) {
        config.trk_lat = request->arg("trackerPosLat").toFloat();
    }

    if (request->hasArg("trackerPosLon") && isValidNumber(request->arg("trackerPosLon"))) {
        config.trk_lon = request->arg("trackerPosLon").toFloat();
    }

    if (request->hasArg("trackerPosAlt") && isValidNumber(request->arg("trackerPosAlt"))) {
        config.trk_alt = request->arg("trackerPosAlt").toFloat();
    }

    if (request->hasArg("trackerPosSel")) {
        config.trk_gps = (request->arg("trackerPosSel").toInt() == 1);
    }

    saveEEPROM();
    request->send(200, "text/plain", "OK");
}

void handleTrackerSmartBeacon(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/tracker_smartbeacon.html");
    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    html.replace("%HIGH_SPEED%", String(config.trk_hspeed));
    html.replace("%LOW_SPEED%", String(config.trk_lspeed));
    html.replace("%SLOW_INTERVAL%", String(config.trk_slowinterval));
    html.replace("%MAX_INTERVAL%", String(config.trk_maxinterval));
    html.replace("%MIN_INTERVAL%", String(config.trk_mininterval));
    html.replace("%MIN_ANGLE%", String(config.trk_minangle));

    request->send(200, "text/html", html);
}

void handleTrackerSmartBeaconPost(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    if (request->hasArg("hspeed") && isValidNumber(request->arg("hspeed"))) {
        config.trk_hspeed = request->arg("hspeed").toInt();
    }

    if (request->hasArg("lspeed") && isValidNumber(request->arg("lspeed"))) {
        config.trk_lspeed = request->arg("lspeed").toInt();
    }

    if (request->hasArg("slowInterval") && isValidNumber(request->arg("slowInterval"))) {
        config.trk_slowinterval = request->arg("slowInterval").toInt();
    }

    if (request->hasArg("maxInterval") && isValidNumber(request->arg("maxInterval"))) {
        config.trk_maxinterval = request->arg("maxInterval").toInt();
    }

    if (request->hasArg("minInterval") && isValidNumber(request->arg("minInterval"))) {
        config.trk_mininterval = request->arg("minInterval").toInt();
    }

    if (request->hasArg("minAngle") && isValidNumber(request->arg("minAngle"))) {
        config.trk_minangle = request->arg("minAngle").toInt();
    }

    saveEEPROM();
    request->send(200, "text/plain", "OK");
}

void handle_wx(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}
	bool En = false;
	bool posGPS = false;
	bool pos2RF = false;
	bool pos2INET = false;
	bool timeStamp = false;

	if (request->hasArg("commitWX"))
	{
		for (int i = 0; i < request->args(); i++)
		{
			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}
			if (request->argName(i) == "Object")
			{
				if (request->arg(i) != "")
				{
					String name = request->arg(i);
					name.trim();
					strcpy(config.wx_object, name.c_str());
				}
				else
				{
					config.wx_object[0] = 0;
				}
			}
			if (request->argName(i) == "myCall")
			{
				if (request->arg(i) != "")
				{
					String name = request->arg(i);
					name.trim();
					name.toUpperCase();
					strcpy(config.wx_mycall, name.c_str());
				}
			}
			if (request->argName(i) == "mySSID")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.wx_ssid = request->arg(i).toInt();
					if (config.wx_ssid > 15)
						config.wx_ssid = 3;
				}
			}
			if (request->argName(i) == "PosInv")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.wx_interval = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "PosLat")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.wx_lat = request->arg(i).toFloat();
				}
			}

			if (request->argName(i) == "PosLon")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.wx_lon = request->arg(i).toFloat();
				}
			}
			if (request->argName(i) == "PosAlt")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.wx_alt = request->arg(i).toFloat();
				}
			}
			if (request->argName(i) == "PosSel")
			{
				if (request->arg(i) != "")
				{
					if (request->arg(i).toInt() == 1)
						posGPS = true;
				}
			}

			if (request->argName(i) == "Path")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.wx_path = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "Comment")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.wx_comment, request->arg(i).c_str());
				}
				else
				{
					memset(config.wx_comment, 0, sizeof(config.wx_comment));
				}
			}
			if (request->argName(i) == "Pos2RF")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						pos2RF = true;
				}
			}
			if (request->argName(i) == "Pos2INET")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						pos2INET = true;
				}
			}
			if (request->argName(i) == "wxTimeStamp")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						timeStamp = true;
				}
			}
		}
		config.wx_en = En;
		config.wx_gps = posGPS;
		config.wx_2rf = pos2RF;
		config.wx_2inet = pos2INET;
		config.wx_timestamp = timeStamp;

		saveEEPROM();
		initInterval = true;
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else
	{

		String html = "<script type=\"text/javascript\">\n";
		html += "$('form').submit(function (e) {\n";
		html += "e.preventDefault();\n";
		html += "var data = new FormData(e.currentTarget);\n";
		html += "document.getElementById(\"submitWX\").disabled=true;\n";
		html += "$.ajax({\n";
		html += "url: '/wx',\n";
		html += "type: 'POST',\n";
		html += "data: data,\n";
		html += "contentType: false,\n";
		html += "processData: false,\n";
		html += "success: function (data) {\n";
		html += "alert(\"Submited Successfully\");\n";
		html += "},\n";
		html += "error: function (data) {\n";
		html += "alert(\"An error occurred.\");\n";
		html += "}\n";
		html += "});\n";
		html += "});\n";
		html += "</script>\n";

		/************************ WX Mode **************************/
		html += "<form id='formWX' method=\"POST\" action='#' enctype='multipart/form-data'>\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>[WX] Weather Station</b></span></th>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Enable:</b></td>\n";
		String EnFlag = "";
		if (config.wx_en)
			EnFlag = "checked";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + EnFlag + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Station Callsign:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input maxlength=\"7\" size=\"6\" id=\"myCall\" name=\"myCall\" type=\"text\" value=\"" + String(config.wx_mycall) + "\" /></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Station SSID:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"mySSID\" id=\"mySSID\">\n";
		for (uint8_t ssid = 0; ssid <= 15; ssid++)
		{
			if (config.wx_ssid == ssid)
			{
				html += "<option value=\"" + String(ssid) + "\" selected>" + String(ssid) + "</option>\n";
			}
			else
			{
				html += "<option value=\"" + String(ssid) + "\">" + String(ssid) + "</option>\n";
			}
		}
		html += "</select></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Object Name:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input maxlength=\"9\" size=\"9\" name=\"Object\" type=\"text\" value=\"" + String(config.wx_object) + "\" /><i> *If not used, leave it blank.In use 3-9 charactor</i></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>PATH:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"Path\" id=\"Path\">\n";
		for (uint8_t pthIdx = 0; pthIdx < PATH_LEN; pthIdx++)
		{
			if (config.wx_path == pthIdx)
			{
				html += "<option value=\"" + String(pthIdx) + "\" selected>" + String(PATH_NAME[pthIdx]) + "</option>\n";
			}
			else
			{
				html += "<option value=\"" + String(pthIdx) + "\">" + String(PATH_NAME[pthIdx]) + "</option>\n";
			}
		}
		html += "</select></td>\n";
		// html += "<td style=\"text-align: left;\"><input maxlength=\"72\" size=\"72\" name=\"Path\" type=\"text\" value=\"" + String(config.wx_path) + "\" /></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Text Comment:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input maxlength=\"50\" size=\"50\" name=\"Comment\" type=\"text\" value=\"" + String(config.wx_comment) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>Time Stamp:</b></td>\n";
		String timeStampFlag = "";
		if (config.wx_timestamp)
			timeStampFlag = "checked";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"wxTimeStamp\" value=\"OK\" " + timeStampFlag + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr><td align=\"right\"><b>POSITION:</b></td>\n";
		html += "<td align=\"center\">\n";
		html += "<table>";
		html += "<tr><td style=\"text-align: right;\">Interval:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"3600\" step=\"1\" id=\"PosInv\" name=\"PosInv\" type=\"number\" value=\"" + String(config.wx_interval) + "\" />Sec.</td></tr>";
		String PosFixFlag = "";
		String PosGPSFlag = "";
		String Pos2RFFlag = "";
		String Pos2INETFlag = "";
		if (config.wx_gps)
			PosGPSFlag = "checked=\"checked\"";
		else
			PosFixFlag = "checked=\"checked\"";

		if (config.wx_2rf)
			Pos2RFFlag = "checked";
		if (config.wx_2inet)
			Pos2INETFlag = "checked";
		html += "<tr><td style=\"text-align: right;\">Location:</td><td style=\"text-align: left;\"><input type=\"radio\" name=\"PosSel\" value=\"0\" " + PosFixFlag + "/>Fix <input type=\"radio\" name=\"PosSel\" value=\"1\" " + PosGPSFlag + "/>GPS </td></tr>\n";
		html += "<tr><td style=\"text-align: right;\">TX Channel:</td><td style=\"text-align: left;\"><input type=\"checkbox\" name=\"Pos2RF\" value=\"OK\" " + Pos2RFFlag + "/>RF <input type=\"checkbox\" name=\"Pos2INET\" value=\"OK\" " + Pos2INETFlag + "/>Internet </td></tr>\n";
		html += "<tr><td style=\"text-align: right;\">Latitude:</td><td style=\"text-align: left;\"><input min=\"-90\" max=\"90\" step=\"0.00001\" name=\"PosLat\" type=\"number\" value=\"" + String(config.wx_lat, 5) + "\" />degrees (positive for North, negative for South)</td></tr>\n";
		html += "<tr><td style=\"text-align: right;\">Longitude:</td><td style=\"text-align: left;\"><input min=\"-180\" max=\"180\" step=\"0.00001\" name=\"PosLon\" type=\"number\" value=\"" + String(config.wx_lon, 5) + "\" />degrees (positive for East, negative for West)</td></tr>\n";
		html += "<tr><td style=\"text-align: right;\">Altitude:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"10000\" step=\"0.1\" name=\"PosAlt\" type=\"number\" value=\"" + String(config.wx_alt, 2) + "\" /> meter. *Value 0 is not send height</td></tr>\n";
		html += "</table></td>";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>PORT:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"channel\" id=\"channel\">\n";
		for (int i = 0; i < 4; i++)
		{
			if (config.wx_channel == i)
				html += "<option value=\"" + String(i) + "\" selected>" + String(WX_PORT[i]) + " </option>\n";
			else
				html += "<option value=\"" + String(i) + "\" >" + String(WX_PORT[i]) + " </option>\n";
		}
		html += "</select>\n";
		html += "</td>\n";
		html += "</tr>\n";

		html += "</table><br />\n";
		html += "<div><button type='submit' id='submitWX'  name=\"commitWX\"> Apply Change </button></div>\n";
		html += "<input type=\"hidden\" name=\"commitWX\"/>\n";
		html += "</form><br />";
		request->send(200, "text/html", html); // send to someones browser when asked
	}
}

// void handle_tlm(AsyncWebServerRequest *request)
// {
// 	if (!request->authenticate(config.http_username, config.http_password))
// 	{
// 		return request->requestAuthentication();
// 	}
// 	bool En = false;
// 	bool pos2RF = false;
// 	bool pos2INET = false;
// 	String arg = "";

// 	if (request->hasArg("commitTLM"))
// 	{
// 		for (int i = 0; i < request->args(); i++)
// 		{
// 			if (request->argName(i) == "Enable")
// 			{
// 				if (request->arg(i) != "")
// 				{
// 					if (String(request->arg(i)) == "OK")
// 						En = true;
// 				}
// 			}
// 			if (request->argName(i) == "myCall")
// 			{
// 				if (request->arg(i) != "")
// 				{
// 					String name = request->arg(i);
// 					name.trim();
// 					name.toUpperCase();
// 					strcpy(config.tlm0_mycall, name.c_str());
// 				}
// 			}
// 			if (request->argName(i) == "mySSID")
// 			{
// 				if (request->arg(i) != "")
// 				{
// 					if (isValidNumber(request->arg(i)))
// 						config.tlm0_ssid = request->arg(i).toInt();
// 					if (config.tlm0_ssid > 15)
// 						config.tlm0_ssid = 3;
// 				}
// 			}
// 			if (request->argName(i) == "infoInv")
// 			{
// 				if (request->arg(i) != "")
// 				{
// 					if (isValidNumber(request->arg(i)))
// 						config.tlm0_info_interval = request->arg(i).toInt();
// 				}
// 			}
// 			if (request->argName(i) == "dataInv")
// 			{
// 				if (request->arg(i) != "")
// 				{
// 					if (isValidNumber(request->arg(i)))
// 						config.tlm0_data_interval = request->arg(i).toInt();
// 				}
// 			}

// 			if (request->argName(i) == "Path")
// 			{
// 				if (request->arg(i) != "")
// 				{
// 					if (isValidNumber(request->arg(i)))
// 						config.tlm0_path = request->arg(i).toInt();
// 				}
// 			}
// 			if (request->argName(i) == "Comment")
// 			{
// 				if (request->arg(i) != "")
// 				{
// 					strcpy(config.tlm0_comment, request->arg(i).c_str());
// 				}
// 				else
// 				{
// 					memset(config.tlm0_comment, 0, sizeof(config.tlm0_comment));
// 				}
// 			}
// 			if (request->argName(i) == "Pos2RF")
// 			{
// 				if (request->arg(i) != "")
// 				{
// 					if (String(request->arg(i)) == "OK")
// 						pos2RF = true;
// 				}
// 			}
// 			if (request->argName(i) == "Pos2INET")
// 			{
// 				if (request->arg(i) != "")
// 				{
// 					if (String(request->arg(i)) == "OK")
// 						pos2INET = true;
// 				}
// 			}
// 			for (int x = 0; x < 13; x++)
// 			{
// 				arg = "sensorCH" + String(x);
// 				if (request->argName(i) == arg)
// 				{
// 					if (isValidNumber(request->arg(i)))
// 						config.tml0_data_channel[x] = request->arg(i).toInt();
// 				}
// 				arg = "param" + String(x);
// 				if (request->argName(i) == arg)
// 				{
// 					if (request->arg(i) != "")
// 					{
// 						strcpy(config.tlm0_PARM[x], request->arg(i).c_str());
// 					}
// 				}
// 				arg = "unit" + String(x);
// 				if (request->argName(i) == arg)
// 				{
// 					if (request->arg(i) != "")
// 					{
// 						strcpy(config.tlm0_UNIT[x], request->arg(i).c_str());
// 					}
// 				}
// 				if (x < 5)
// 				{
// 					for (int y = 0; y < 3; y++)
// 					{
// 						arg = "eqns" + String(x) + String((char)(y + 'a'));
// 						if (request->argName(i) == arg)
// 						{
// 							if (isValidNumber(request->arg(i)))
// 								config.tlm0_EQNS[x][y] = request->arg(i).toFloat();
// 						}
// 					}
// 				}
// 			}
// 			uint8_t b = 1;
// 			for (int x = 0; x < 8; x++)
// 			{
// 				arg = "bitact" + String(x);
// 				if (request->argName(i) == arg)
// 				{
// 					if (isValidNumber(request->arg(i)))
// 					{
// 						if (request->arg(i).toInt() == 1)
// 						{
// 							config.tlm0_BITS_Active |= b;
// 						}
// 						else
// 						{
// 							config.tlm0_BITS_Active &= ~b;
// 						}
// 					}
// 				}
// 				b <<= 1;
// 			}
// 		}
// 		config.tlm0_en = En;
// 		config.tlm0_2rf = pos2RF;
// 		config.tlm0_2inet = pos2INET;

// 		saveEEPROM();
// 		initInterval = true;
// 		String html = "OK";
// 		request->send(200, "text/html", html);
// 	}
// 	else
// 	{

// 		String html = "<script type=\"text/javascript\">\n";
// 		html += "$('form').submit(function (e) {\n";
// 		html += "e.preventDefault();\n";
// 		html += "var data = new FormData(e.currentTarget);\n";
// 		html += "document.getElementById(\"submitTLM\").disabled=true;\n";
// 		html += "$.ajax({\n";
// 		html += "url: '/tlm',\n";
// 		html += "type: 'POST',\n";
// 		html += "data: data,\n";
// 		html += "contentType: false,\n";
// 		html += "processData: false,\n";
// 		html += "success: function (data) {\n";
// 		html += "alert(\"Submited Successfully\");\n";
// 		html += "},\n";
// 		html += "error: function (data) {\n";
// 		html += "alert(\"An error occurred.\");\n";
// 		html += "}\n";
// 		html += "});\n";
// 		html += "});\n";
// 		html += "</script>\n";

// 		/************************ TLM Mode **************************/
// 		html += "<form id='formTLM' method=\"POST\" action='#' enctype='multipart/form-data'>\n";
// 		html += "<table>\n";
// 		html += "<th colspan=\"2\"><span><b>System Telemetry</b></span></th>\n";
// 		html += "<tr>\n";
// 		html += "<td align=\"right\"><b>Enable:</b></td>\n";
// 		String EnFlag = "";
// 		if (config.tlm0_en)
// 			EnFlag = "checked";
// 		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + EnFlag + "><span class=\"slider round\"></span></label></td>\n";
// 		html += "</tr>\n";
// 		html += "<tr>\n";
// 		html += "<td align=\"right\"><b>Station Callsign:</b></td>\n";
// 		html += "<td style=\"text-align: left;\"><input maxlength=\"7\" size=\"6\" id=\"myCall\" name=\"myCall\" type=\"text\" value=\"" + String(config.tlm0_mycall) + "\" /></td>\n";
// 		html += "</tr>\n";
// 		html += "<tr>\n";
// 		html += "<td align=\"right\"><b>Station SSID:</b></td>\n";
// 		html += "<td style=\"text-align: left;\">\n";
// 		html += "<select name=\"mySSID\" id=\"mySSID\">\n";
// 		for (uint8_t ssid = 0; ssid <= 15; ssid++)
// 		{
// 			if (config.tlm0_ssid == ssid)
// 			{
// 				html += "<option value=\"" + String(ssid) + "\" selected>" + String(ssid) + "</option>\n";
// 			}
// 			else
// 			{
// 				html += "<option value=\"" + String(ssid) + "\">" + String(ssid) + "</option>\n";
// 			}
// 		}
// 		html += "</select></td>\n";
// 		html += "</tr>\n";

// 		html += "<tr>\n";
// 		html += "<td align=\"right\"><b>PATH:</b></td>\n";
// 		html += "<td style=\"text-align: left;\">\n";
// 		html += "<select name=\"Path\" id=\"Path\">\n";
// 		for (uint8_t pthIdx = 0; pthIdx < PATH_LEN; pthIdx++)
// 		{
// 			if (config.tlm0_path == pthIdx)
// 			{
// 				html += "<option value=\"" + String(pthIdx) + "\" selected>" + String(PATH_NAME[pthIdx]) + "</option>\n";
// 			}
// 			else
// 			{
// 				html += "<option value=\"" + String(pthIdx) + "\">" + String(PATH_NAME[pthIdx]) + "</option>\n";
// 			}
// 		}
// 		html += "</select></td>\n";
// 		// html += "<td style=\"text-align: left;\"><input maxlength=\"72\" size=\"72\" name=\"Path\" type=\"text\" value=\"" + String(config.tlm0_path) + "\" /></td>\n";
// 		html += "</tr>\n";
// 		html += "<tr>\n";
// 		html += "<td align=\"right\"><b>Text Comment:</b></td>\n";
// 		html += "<td style=\"text-align: left;\"><input maxlength=\"50\" size=\"50\" name=\"Comment\" type=\"text\" value=\"" + String(config.tlm0_comment) + "\" /></td>\n";
// 		html += "</tr>\n";

// 		html += "<tr><td style=\"text-align: right;\">Info Interval:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"3600\" step=\"1\" name=\"infoInv\" type=\"number\" value=\"" + String(config.tlm0_info_interval) + "\" />Sec.</td></tr>";
// 		html += "<tr><td style=\"text-align: right;\">Data Interval:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"3600\" step=\"1\" name=\"dataInv\" type=\"number\" value=\"" + String(config.tlm0_data_interval) + "\" />Sec.</td></tr>";

// 		String Pos2RFFlag = "";
// 		String Pos2INETFlag = "";
// 		if (config.tlm0_2rf)
// 			Pos2RFFlag = "checked";
// 		if (config.tlm0_2inet)
// 			Pos2INETFlag = "checked";
// 		html += "<tr><td style=\"text-align: right;\">TX Channel:</td><td style=\"text-align: left;\"><input type=\"checkbox\" name=\"Pos2RF\" value=\"OK\" " + Pos2RFFlag + "/>RF <input type=\"checkbox\" name=\"Pos2INET\" value=\"OK\" " + Pos2INETFlag + "/>Internet </td></tr>\n";

// 		// html += "<tr>\n";
// 		// html += "<td align=\"right\"><b>Time Stamp:</b></td>\n";
// 		// String timeStampFlag = "";
// 		// if (config.wx_timestamp)
// 		// 	timeStampFlag = "checked";
// 		// html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"wxTimeStamp\" value=\"OK\" " + timeStampFlag + "><span class=\"slider round\"></span></label></td>\n";
// 		// html += "</tr>\n";
// 		for (int ax = 0; ax < 5; ax++)
// 		{
// 			html += "<tr><td align=\"right\"><b>Channel A" + String(ax + 1) + ":</b></td>\n";
// 			html += "<td align=\"center\">\n";
// 			html += "<table>";

// 			// html += "<tr><td style=\"text-align: right;\">Name:</td><td style=\"text-align: center;\"><i>Sensor Type</i></td><td style=\"text-align: center;\"><i>Parameter</i></td><td style=\"text-align: center;\"><i>Unit</i></td></tr>\n";

// 			html += "<tr><td style=\"text-align: right;\">Type/Name:</td>\n";
// 			html += "<td style=\"text-align: left;\">Sensor Type: ";
// 			html += "<select name=\"sensorCH" + String(ax) + "\" id=\"sensorCH" + String(ax) + "\">\n";
// 			for (uint8_t idx = 0; idx < SYSTEM_LEN; idx++)
// 			{
// 				if (config.tml0_data_channel[ax] == idx)
// 				{
// 					html += "<option value=\"" + String(idx) + "\" selected>" + String(SYSTEM_NAME[idx]) + "</option>\n";
// 				}
// 				else
// 				{
// 					html += "<option value=\"" + String(idx) + "\">" + String(SYSTEM_NAME[idx]) + "</option>\n";
// 				}
// 			}
// 			html += "</select></td>\n";

// 			html += "<td style=\"text-align: left;\">Parameter: <input maxlength=\"10\" size=\"8\" name=\"param" + String(ax) + "\" type=\"text\" value=\"" + String(config.tlm0_PARM[ax]) + "\" /></td>\n";
// 			html += "<td style=\"text-align: left;\">Unit: <input maxlength=\"8\" size=\"5\" name=\"unit" + String(ax) + "\" type=\"text\" value=\"" + String(config.tlm0_UNIT[ax]) + "\" /></td></tr>\n";
// 			html += "<tr><td style=\"text-align: right;\">EQNS:</td><td colspan=\"3\" style=\"text-align: left;\">a:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax) + "a\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][0], 3) + "\" />  b:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax) + "b\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][1], 3) + "\" /> c:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax) + "c\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][2], 3) + "\" /> (av^2+bv+c)</td></tr>\n";
// 			html += "</table></td>";
// 			html += "</tr>\n";
// 		}

// 		uint8_t b = 1;
// 		for (int ax = 0; ax < 8; ax++)
// 		{
// 			html += "<tr><td align=\"right\"><b>Channel B" + String(ax + 1) + ":</b></td>\n";
// 			html += "<td align=\"center\">\n";
// 			html += "<table>";

// 			// html += "<tr><td style=\"text-align: right;\">Type/Name:</td>\n";
// 			html += "<td style=\"text-align: left;\">Type: ";
// 			html += "<select name=\"sensorCH" + String(ax + 5) + "\" id=\"sensorCH" + String(ax) + "\">\n";
// 			for (uint8_t idx = 0; idx < SYSTEM_BIT_LEN; idx++)
// 			{
// 				if (config.tml0_data_channel[ax + 5] == idx)
// 				{
// 					html += "<option value=\"" + String(idx) + "\" selected>" + String(SYSTEM_BITS_NAME[idx]) + "</option>\n";
// 				}
// 				else
// 				{
// 					html += "<option value=\"" + String(idx) + "\">" + String(SYSTEM_BITS_NAME[idx]) + "</option>\n";
// 				}
// 			}
// 			html += "</select></td>\n";

// 			html += "<td style=\"text-align: left;\">Parameter: <input maxlength=\"10\" size=\"8\" name=\"param" + String(ax + 5) + "\" type=\"text\" value=\"" + String(config.tlm0_PARM[ax + 5]) + "\" /></td>\n";
// 			html += "<td style=\"text-align: left;\">Unit: <input maxlength=\"8\" size=\"5\" name=\"unit" + String(ax + 5) + "\" type=\"text\" value=\"" + String(config.tlm0_UNIT[ax + 5]) + "\" /></td>\n";
// 			String LowFlag = "", HighFlag = "";
// 			if (config.tlm0_BITS_Active & b)
// 				HighFlag = "checked=\"checked\"";
// 			else
// 				LowFlag = "checked=\"checked\"";
// 			html += "<td style=\"text-align: left;\"> Active:<input type=\"radio\" name=\"bitact" + String(ax) + "\" value=\"0\" " + LowFlag + "/>LOW <input type=\"radio\" name=\"bitact" + String(ax) + "\" value=\"1\" " + HighFlag + "/>HIGH </td>\n";
// 			html += "</tr>\n";
// 			// html += "<tr><td style=\"text-align: right;\">EQNS:</td><td colspan=\"3\" style=\"text-align: left;\">a:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax + 1) + "a\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][0], 3) + "\" />  b:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax + 1) + "b\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][1], 3) + "\" /> c:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax + 1) + "c\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][2], 3) + "\" /> (av^2+bv+c)</td></tr>\n";
// 			html += "</table></td>";
// 			html += "</tr>\n";
// 			b <<= 1;
// 		}

// 		// html += "<tr><td align=\"right\"><b>Parameter Name:</b></td>\n";
// 		// html += "<td align=\"center\">\n";
// 		// html += "<table>";

// 		// // html += "<tr><td style=\"text-align: right;\">Latitude:</td><td style=\"text-align: left;\"><input min=\"-90\" max=\"90\" step=\"0.00001\" name=\"PosLat\" type=\"number\" value=\"" + String(config.wx_lat, 5) + "\" />degrees (positive for North, negative for South)</td></tr>\n";
// 		// // html += "<tr><td style=\"text-align: right;\">Longitude:</td><td style=\"text-align: left;\"><input min=\"-180\" max=\"180\" step=\"0.00001\" name=\"PosLon\" type=\"number\" value=\"" + String(config.wx_lon, 5) + "\" />degrees (positive for East, negative for West)</td></tr>\n";
// 		// // html += "<tr><td style=\"text-align: right;\">Altitude:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"10000\" step=\"0.1\" name=\"PosAlt\" type=\"number\" value=\"" + String(config.wx_alt, 2) + "\" /> meter. *Value 0 is not send height</td></tr>\n";
// 		// html += "</table></td>";
// 		// html += "</tr>\n";

// 		html += "</table><br />\n";
// 		html += "<div><button type='submit' id='submitTLM'  name=\"commitTLM\"> Apply Change </button></div>\n";
// 		html += "<input type=\"hidden\" name=\"commitTLM\"/>\n";
// 		html += "</form><br />";
// 		request->send(200, "text/html", html); // send to someones browser when asked
// 	}
// }

void handleTlmGeneral(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/tlm_general.html");
    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    html.replace("%TLM_ENABLE%", config.tlm0_en ? "checked" : "");
    html.replace("%MY_CALL%", String(config.tlm0_mycall));
    html.replace("%MY_SSID%", String(config.tlm0_ssid));
    html.replace("%INFO_INTERVAL%", String(config.tlm0_info_interval));
    html.replace("%DATA_INTERVAL%", String(config.tlm0_data_interval));
    html.replace("%TLM_PATH%", String(config.tlm0_path));
    html.replace("%TLM_COMMENT%", String(config.tlm0_comment));
    html.replace("%TX_RF%", config.tlm0_2rf ? "checked" : "");
    html.replace("%TX_INET%", config.tlm0_2inet ? "checked" : "");

    request->send(200, "text/html", html);
}

void handleTlmGeneralPost(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    if (request->hasArg("Enable")) config.tlm0_en = (request->arg("Enable") == "OK");
    if (request->hasArg("myCall")) strcpy(config.tlm0_mycall, request->arg("myCall").c_str());
    if (request->hasArg("mySSID") && isValidNumber(request->arg("mySSID"))) {
        config.tlm0_ssid = request->arg("mySSID").toInt();
        if (config.tlm0_ssid > 15) config.tlm0_ssid = 3;
    }
    if (request->hasArg("infoInv") && isValidNumber(request->arg("infoInv"))) {
        config.tlm0_info_interval = request->arg("infoInv").toInt();
    }
    if (request->hasArg("dataInv") && isValidNumber(request->arg("dataInv"))) {
        config.tlm0_data_interval = request->arg("dataInv").toInt();
    }
    if (request->hasArg("Path") && isValidNumber(request->arg("Path"))) {
        config.tlm0_path = request->arg("Path").toInt();
    }
    if (request->hasArg("Comment")) strcpy(config.tlm0_comment, request->arg("Comment").c_str());
    if (request->hasArg("Pos2RF")) config.tlm0_2rf = (request->arg("Pos2RF") == "OK");
    if (request->hasArg("Pos2INET")) config.tlm0_2inet = (request->arg("Pos2INET") == "OK");

    saveEEPROM();
    request->send(200, "text/plain", "OK");
}

void handleTlmChannelA(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/tlm_channela.html");
    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    for (int i = 0; i < 5; i++) {
        html.replace("%SENSOR_TYPE_" + String(i) + "%", String(config.tml0_data_channel[i]));
        html.replace("%PARAM_" + String(i) + "%", String(config.tlm0_PARM[i]));
        html.replace("%UNIT_" + String(i) + "%", String(config.tlm0_UNIT[i]));
        html.replace("%EQNS_A_" + String(i) + "%", String(config.tlm0_EQNS[i][0], 3));
        html.replace("%EQNS_B_" + String(i) + "%", String(config.tlm0_EQNS[i][1], 3));
        html.replace("%EQNS_C_" + String(i) + "%", String(config.tlm0_EQNS[i][2], 3));
    }

    request->send(200, "text/html", html);
}

void handleTlmChannelAPost(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    for (int i = 0; i < 5; i++) {
        String sensorArg = "sensorCH" + String(i);
        String paramArg = "param" + String(i);
        String unitArg = "unit" + String(i);

        if (request->hasArg(sensorArg.c_str()) && isValidNumber(request->arg(sensorArg.c_str()))) {
            config.tml0_data_channel[i] = request->arg(sensorArg.c_str()).toInt();
        }
        if (request->hasArg(paramArg.c_str())) {
            strcpy(config.tlm0_PARM[i], request->arg(paramArg.c_str()).c_str());
        }
        if (request->hasArg(unitArg.c_str())) {
            strcpy(config.tlm0_UNIT[i], request->arg(unitArg.c_str()).c_str());
        }

        for (int j = 0; j < 3; j++) {
            String eqnsArg = "eqns" + String(i) + String((char)('a' + j));
            if (request->hasArg(eqnsArg.c_str()) && isValidNumber(request->arg(eqnsArg.c_str()))) {
                config.tlm0_EQNS[i][j] = request->arg(eqnsArg.c_str()).toFloat();
            }
        }
    }

    saveEEPROM();
    request->send(200, "text/plain", "OK");
}

void handleTlmChannelB(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/tlm_channelb.html");
    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    for (int i = 0; i < 8; i++) {
        html.replace("%BIT_SENSOR_" + String(i) + "%", String(config.tml0_data_channel[i + 5]));
        html.replace("%BIT_PARAM_" + String(i) + "%", String(config.tlm0_PARM[i + 5]));
        html.replace("%BIT_UNIT_" + String(i) + "%", String(config.tlm0_UNIT[i + 5]));
        html.replace("%BIT_ACTIVE_" + String(i) + "%", (config.tlm0_BITS_Active & (1 << i)) ? "checked" : "");
    }

    request->send(200, "text/html", html);
}

void handleTlmChannelBPost(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    for (int i = 0; i < 8; i++) {
        String sensorArg = "sensorCH" + String(i + 5);
        String paramArg = "param" + String(i + 5);
        String unitArg = "unit" + String(i + 5);
        String bitActArg = "bitact" + String(i);

        if (request->hasArg(sensorArg.c_str()) && isValidNumber(request->arg(sensorArg.c_str()))) {
            config.tml0_data_channel[i + 5] = request->arg(sensorArg.c_str()).toInt();
        }
        if (request->hasArg(paramArg.c_str())) {
            strcpy(config.tlm0_PARM[i + 5], request->arg(paramArg.c_str()).c_str());
        }
        if (request->hasArg(unitArg.c_str())) {
            strcpy(config.tlm0_UNIT[i + 5], request->arg(unitArg.c_str()).c_str());
        }

        if (request->hasArg(bitActArg.c_str()) && isValidNumber(request->arg(bitActArg.c_str()))) {
            if (request->arg(bitActArg.c_str()).toInt() == 1) {
                config.tlm0_BITS_Active |= (1 << i);
            } else {
                config.tlm0_BITS_Active &= ~(1 << i);
            }
        }
    }

    saveEEPROM();
    request->send(200, "text/plain", "OK");
}

void handle_tracker(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}
	bool trakerEn = false;
	bool smartEn = false;
	bool compEn = false;

	bool posGPS = false;
	bool pos2RF = false;
	bool pos2INET = false;
	bool optCST = false;
	bool optAlt = false;
	bool optBat = false;
	bool optSat = false;
	bool timeStamp = false;

	if (request->hasArg("commitTRACKER"))
	{
		for (uint8_t i = 0; i < request->args(); i++)
		{
			if (request->argName(i) == "trackerEnable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						trakerEn = true;
				}
			}
			if (request->argName(i) == "smartBcnEnable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						smartEn = true;
				}
			}
			if (request->argName(i) == "compressEnable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						compEn = true;
				}
			}
			if (request->argName(i) == "trackerOptCST")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						optCST = true;
				}
			}
			if (request->argName(i) == "trackerOptAlt")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						optAlt = true;
				}
			}
			if (request->argName(i) == "trackerOptBat")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						optBat = true;
				}
			}
			if (request->argName(i) == "trackerOptSat")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						optSat = true;
				}
			}
			if (request->argName(i) == "myCall")
			{
				if (request->arg(i) != "")
				{
					String name = request->arg(i);
					name.trim();
					name.toUpperCase();
					strcpy(config.trk_mycall, name.c_str());
				}
			}
			if (request->argName(i) == "trackerObject")
			{
				if (request->arg(i) != "")
				{
					String name = request->arg(i);
					name.trim();
					strcpy(config.trk_item, name.c_str());
				}
				else
				{
					memset(config.trk_item, 0, sizeof(config.trk_item));
				}
			}
			if (request->argName(i) == "mySSID")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_ssid = request->arg(i).toInt();
					if (config.trk_ssid > 15)
						config.trk_ssid = 13;
				}
			}
			if (request->argName(i) == "trackerPosInv")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_interval = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "trackerPosLat")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_lat = request->arg(i).toFloat();
				}
			}

			if (request->argName(i) == "trackerPosLon")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_lon = request->arg(i).toFloat();
				}
			}
			if (request->argName(i) == "trackerPosAlt")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_alt = request->arg(i).toFloat();
				}
			}
			if (request->argName(i) == "trackerPosSel")
			{
				if (request->arg(i) != "")
				{
					if (request->arg(i).toInt() == 1)
						posGPS = true;
				}
			}
			if (request->argName(i) == "hspeed")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_hspeed = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "lspeed")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_lspeed = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "slowInterval")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_slowinterval = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "maxInterval")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_maxinterval = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "minInterval")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_mininterval = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "minAngle")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_minangle = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "trackerTable")
			{
				if (request->arg(i) != "")
				{
					config.trk_symbol[0] = request->arg(i).charAt(0);
				}
			}
			if (request->argName(i) == "trackerSymbol")
			{
				if (request->arg(i) != "")
				{
					config.trk_symbol[1] = request->arg(i).charAt(0);
				}
			}
			if (request->argName(i) == "moveTable")
			{
				if (request->arg(i) != "")
				{
					config.trk_symmove[0] = request->arg(i).charAt(0);
				}
			}
			if (request->argName(i) == "moveSymbol")
			{
				if (request->arg(i) != "")
				{
					config.trk_symmove[1] = request->arg(i).charAt(0);
				}
			}
			if (request->argName(i) == "stopTable")
			{
				if (request->arg(i) != "")
				{
					config.trk_symstop[0] = request->arg(i).charAt(0);
				}
			}
			if (request->argName(i) == "stopSymbol")
			{
				if (request->arg(i) != "")
				{
					config.trk_symstop[1] = request->arg(i).charAt(0);
				}
			}

			if (request->argName(i) == "trackerPath")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.trk_path = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "trackerComment")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.trk_comment, request->arg(i).c_str());
				}
				else
				{
					memset(config.trk_comment, 0, sizeof(config.trk_comment));
				}
			}

			if (request->argName(i) == "trackerPos2RF")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						pos2RF = true;
				}
			}
			if (request->argName(i) == "trackerPos2INET")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						pos2INET = true;
				}
			}
			if (request->argName(i) == "trackerTimeStamp")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						timeStamp = true;
				}
			}
		}
		config.trk_en = trakerEn;
		config.trk_smartbeacon = smartEn;
		config.trk_compress = compEn;

		config.trk_gps = posGPS;
		config.trk_loc2rf = pos2RF;
		config.trk_loc2inet = pos2INET;

		config.trk_cst = optCST;
		config.trk_altitude = optAlt;
		config.trk_bat = optBat;
		config.trk_sat = optSat;
		config.trk_timestamp = timeStamp;

		saveEEPROM();
		initInterval = true;
		String html = "OK";
		request->send(200, "text/html", html);
	}

	String html = "<script type=\"text/javascript\">\n";
	html += "$('form').submit(function (e) {\n";
	html += "e.preventDefault();\n";
	html += "var data = new FormData(e.currentTarget);\n";
	html += "document.getElementById(\"submitTRACKER\").disabled=true;\n";
	html += "$.ajax({\n";
	html += "url: '/tracker',\n";
	html += "type: 'POST',\n";
	html += "data: data,\n";
	html += "contentType: false,\n";
	html += "processData: false,\n";
	html += "success: function (data) {\n";
	html += "alert(\"Submited Successfully\");\n";
	html += "},\n";
	html += "error: function (data) {\n";
	html += "alert(\"An error occurred.\");\n";
	html += "}\n";
	html += "});\n";
	html += "});\n";
	html += "</script>\n<script type=\"text/javascript\">\n";
	html += "function openWindowSymbol(sel) {\n";
	html += "var i, l, options = [{\n";
	html += "value: 'first',\n";
	html += "text: 'First'\n";
	html += "}, {\n";
	html += "value: 'second',\n";
	html += "text: 'Second'\n";
	html += "}],\n";
	html += "newWindow = window.open(\"/symbol?sel=\"+sel.toString(), null, \"height=400,width=400,status=no,toolbar=no,menubar=no,location=no\");\n";
	html += "}\n";

	html += "function setValue(sel,symbol,table) {\n";
	html += "var txtsymbol=document.getElementById('trackerSymbol');\n";
	html += "var txttable=document.getElementById('trackerTable');\n";
	html += "var imgicon=document.getElementById('trackerImgSymbol');\n";
	html += "if(sel==1){\n";
	html += "txtsymbol=document.getElementById('moveSymbol');\n";
	html += "txttable=document.getElementById('moveTable');\n";
	html += "imgicon= document.getElementById('moveImgSymbol');\n";
	html += "}else if(sel==2){\n";
	html += "txtsymbol=document.getElementById('stopSymbol');\n";
	html += "txttable=document.getElementById('stopTable');\n";
	html += "imgicon= document.getElementById('stopImgSymbol');\n";
	html += "}\n";
	html += "txtsymbol.value = String.fromCharCode(symbol);\n";
	html += "if(table==1){\n txttable.value='/';\n";
	html += "}else if(table==2){\n txttable.value='\\\\';\n}\n";
	html += "imgicon.src = \"http://aprs.dprns.com/symbols/icons/\"+symbol.toString()+'-'+table.toString()+'.png';\n";
	html += "\n}\n";
	html += "function onSmartCheck() {\n";
	html += "if (document.querySelector('#smartBcnEnable').checked) {\n";
	// Checkbox has been checked
	html += "document.getElementById(\"smartbcnGrp\").disabled=false;\n";
	html += "} else {\n";
	// Checkbox has been unchecked
	html += "document.getElementById(\"smartbcnGrp\").disabled=true;\n";
	html += "}\n}\n";

	html += "</script>\n";

	delay(1);
	/************************ tracker Mode **************************/
	html += "<form id='formtracker' method=\"POST\" action='#' enctype='multipart/form-data'>\n";
	// html += "<h2>[TRACKER] Tracker Position Mode</h2>\n";
	html += "<table>\n";
	// html += "<tr>\n";
	// html += "<th width=\"200\"><span><b>Setting</b></span></th>\n";
	// html += "<th><span><b>Value</b></span></th>\n";
	// html += "</tr>\n";
	html += "<th colspan=\"2\"><span><b>[TRACKER] Tracker Position Mode</b></span></th>\n";
	html += "<tr>\n";
	html += "<td align=\"right\"><b>Enable:</b></td>\n";
	String trackerEnFlag = "";
	if (config.trk_en)
		trackerEnFlag = "checked";
	html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"trackerEnable\" value=\"OK\" " + trackerEnFlag + "><span class=\"slider round\"></span></label></td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td align=\"right\"><b>Station Callsign:</b></td>\n";
	html += "<td style=\"text-align: left;\"><input maxlength=\"7\" size=\"6\" id=\"myCall\" name=\"myCall\" type=\"text\" value=\"" + String(config.trk_mycall) + "\" /></td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td align=\"right\"><b>Station SSID:</b></td>\n";
	html += "<td style=\"text-align: left;\">\n";
	html += "<select name=\"mySSID\" id=\"mySSID\">\n";
	for (uint8_t ssid = 0; ssid <= 15; ssid++)
	{
		if (config.trk_ssid == ssid)
		{
			html += "<option value=\"" + String(ssid) + "\" selected>" + String(ssid) + "</option>\n";
		}
		else
		{
			html += "<option value=\"" + String(ssid) + "\">" + String(ssid) + "</option>\n";
		}
	}
	html += "</select></td>\n";
	html += "</tr>\n";

	html += "<tr>\n";
	html += "<td align=\"right\"><b>Item/Obj Name:</b></td>\n";
	html += "<td style=\"text-align: left;\"><input maxlength=\"9\" size=\"9\" id=\"trackerObject\" name=\"trackerObject\" type=\"text\" value=\"" + String(config.trk_item) + "\" /><i> *If not used, leave it blank.In use 3-9 charactor</i></td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td align=\"right\"><b>PATH:</b></td>\n";
	html += "<td style=\"text-align: left;\">\n";
	html += "<select name=\"trackerPath\" id=\"trackerPath\">\n";
	for (uint8_t pthIdx = 0; pthIdx < PATH_LEN; pthIdx++)
	{
		if (config.trk_path == pthIdx)
		{
			html += "<option value=\"" + String(pthIdx) + "\" selected>" + String(PATH_NAME[pthIdx]) + "</option>\n";
		}
		else
		{
			html += "<option value=\"" + String(pthIdx) + "\">" + String(PATH_NAME[pthIdx]) + "</option>\n";
		}
	}
	html += "</select></td>\n";
	// html += "<td style=\"text-align: left;\"><input maxlength=\"72\" size=\"72\" id=\"trackerPath\" name=\"trackerPath\" type=\"text\" value=\"" + String(config.trk_path) + "\" /></td>\n";
	html += "</tr>\n";

	html += "<tr>\n";
	html += "<td align=\"right\"><b>Text Comment:</b></td>\n";
	html += "<td style=\"text-align: left;\"><input maxlength=\"50\" size=\"50\" id=\"trackerComment\" name=\"trackerComment\" type=\"text\" value=\"" + String(config.trk_comment) + "\" /></td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td align=\"right\"><b>Smart Beacon:</b></td>\n";
	String smartBcnEnFlag = "";
	if (config.trk_smartbeacon)
		smartBcnEnFlag = "checked";
	html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" id=\"smartBcnEnable\" name=\"smartBcnEnable\" onclick=\"onSmartCheck()\" value=\"OK\" " + smartBcnEnFlag + "><span class=\"slider round\"></span></label><label style=\"vertical-align: bottom;font-size: 8pt;\"><i> *Switch use to smart beacon mode</i></label></td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td align=\"right\"><b>Compress:</b></td>\n";
	String compressEnFlag = "";
	if (config.trk_compress)
		compressEnFlag = "checked";
	html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"compressEnable\" value=\"OK\" " + compressEnFlag + "><span class=\"slider round\"></span></label><label style=\"vertical-align: bottom;font-size: 8pt;\"><i> *Switch compress packet</i></label></td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td align=\"right\"><b>Time Stamp:</b></td>\n";
	String timeStampFlag = "";
	if (config.trk_timestamp)
		timeStampFlag = "checked";
	html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"trackerTimeStamp\" value=\"OK\" " + timeStampFlag + "><span class=\"slider round\"></span></label></td>\n";
	html += "</tr>\n";
	String trackerPos2RFFlag = "";
	String trackerPos2INETFlag = "";
	if (config.trk_loc2rf)
		trackerPos2RFFlag = "checked";
	if (config.trk_loc2inet)
		trackerPos2INETFlag = "checked";
	html += "<tr><td style=\"text-align: right;\"><b>TX Channel:</b></td><td style=\"text-align: left;\"><input type=\"checkbox\" name=\"trackerPos2RF\" value=\"OK\" " + trackerPos2RFFlag + "/>RF <input type=\"checkbox\" name=\"trackerPos2INET\" value=\"OK\" " + trackerPos2INETFlag + "/>Internet </td></tr>\n";
	String trackerOptBatFlag = "";
	String trackerOptSatFlag = "";
	String trackerOptAltFlag = "";
	String trackerOptCSTFlag = "";
	if (config.trk_bat)
		trackerOptBatFlag = "checked";
	if (config.trk_sat)
		trackerOptSatFlag = "checked";
	if (config.trk_altitude)
		trackerOptAltFlag = "checked";
	if (config.trk_cst)
		trackerOptCSTFlag = "checked";
	html += "<tr><td style=\"text-align: right;\"><b>Option:</b></td><td style=\"text-align: left;\">";
	html += "<input type=\"checkbox\" name=\"trackerOptCST\" value=\"OK\" " + trackerOptCSTFlag + "/>Course/Speed ";
	html += "<input type=\"checkbox\" name=\"trackerOptAlt\" value=\"OK\" " + trackerOptAltFlag + "/>Altitude ";
	html += "<input type=\"checkbox\" name=\"trackerOptBat\" value=\"OK\" " + trackerOptBatFlag + "/>Battery ";
	html += "<input type=\"checkbox\" name=\"trackerOptSat\" value=\"OK\" " + trackerOptSatFlag + "/>Satellite";
	html += "</td></tr>\n";

	html += "<tr>";
	html += "<td align=\"right\"><b>POSITION:</b></td>\n";
	html += "<td align=\"center\">\n";
	html += "<table>";
	html += "<tr><td style=\"text-align: right;\">Interval:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"3600\" step=\"1\" id=\"trackerPosInv\" name=\"trackerPosInv\" type=\"number\" value=\"" + String(config.trk_interval) + "\" />Sec.</label></td></tr>";
	String trackerPosFixFlag = "";
	String trackerPosGPSFlag = "";

	if (config.trk_gps)
		trackerPosGPSFlag = "checked=\"checked\"";
	else
		trackerPosFixFlag = "checked=\"checked\"";

	html += "<tr><td style=\"text-align: right;\">Location:</td><td style=\"text-align: left;\"><input type=\"radio\" name=\"trackerPosSel\" value=\"0\" " + trackerPosFixFlag + "/>Fix <input type=\"radio\" name=\"trackerPosSel\" value=\"1\" " + trackerPosGPSFlag + "/>GPS </td></tr>\n";
	html += "<tr>\n";
	html += "<td align=\"right\">Symbol Icon:</td>\n";
	String table = "1";
	if (config.trk_symbol[0] == 47)
		table = "1";
	if (config.trk_symbol[0] == 92)
		table = "2";
	html += "<td style=\"text-align: left;\">Table:<input maxlength=\"1\" size=\"1\" id=\"trackerTable\" name=\"trackerTable\" type=\"text\" value=\"" + String(config.trk_symbol[0]) + "\" style=\"background-color: rgb(97, 239, 170);\" /> Symbol:<input maxlength=\"1\" size=\"1\" id=\"trackerSymbol\" name=\"trackerSymbol\" type=\"text\" value=\"" + String(config.trk_symbol[1]) + "\" style=\"background-color: rgb(97, 239, 170);\" /> <img border=\"1\" style=\"vertical-align: middle;\" id=\"trackerImgSymbol\" onclick=\"openWindowSymbol(0);\" src=\"http://aprs.dprns.com/symbols/icons/" + String((int)config.trk_symbol[1]) + "-" + table + ".png\"> <i>*Click icon for select symbol</i></td>\n";
	html += "</tr>\n";
	html += "<tr><td style=\"text-align: right;\">Latitude:</td><td style=\"text-align: left;\"><input min=\"-90\" max=\"90\" step=\"0.00001\" id=\"trackerPosLat\" name=\"trackerPosLat\" type=\"number\" value=\"" + String(config.trk_lat, 5) + "\" />degrees (positive for North, negative for South)</td></tr>\n";
	html += "<tr><td style=\"text-align: right;\">Longitude:</td><td style=\"text-align: left;\"><input min=\"-180\" max=\"180\" step=\"0.00001\" id=\"trackerPosLon\" name=\"trackerPosLon\" type=\"number\" value=\"" + String(config.trk_lon, 5) + "\" />degrees (positive for East, negative for West)</td></tr>\n";
	html += "<tr><td style=\"text-align: right;\">Altitude:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"10000\" step=\"0.1\" id=\"trackerPosAlt\" name=\"trackerPosAlt\" type=\"number\" value=\"" + String(config.trk_alt, 2) + "\" /> meter. *Value 0 is not send height</td></tr>\n";
	html += "</table></td>";
	html += "</tr>\n";

	html += "<tr>\n";
	html += "<td align=\"right\"><b>Smart Beacon:</b></td>\n";
	html += "<td align=\"center\">\n";
	if (config.trk_smartbeacon)
		html += "<fieldset id=\"smartbcnGrp\">\n";
	else
		html += "<fieldset id=\"smartbcnGrp\" disabled>\n";
	html += "<legend>Smart beacon configuration</legend>\n<table>";
	html += "<tr>\n";
	html += "<td align=\"right\">Move Symbol:</td>\n";
	table = "1";
	if (config.trk_symmove[0] == 47)
		table = "1";
	if (config.trk_symmove[0] == 92)
		table = "2";
	html += "<td style=\"text-align: left;\">Table:<input maxlength=\"1\" size=\"1\" id=\"moveTable\" name=\"moveTable\" type=\"text\" value=\"" + String(config.trk_symmove[0]) + "\" style=\"background-color: rgb(97, 239, 170);\" /> Symbol:<input maxlength=\"1\" size=\"1\" id=\"moveSymbol\" name=\"moveSymbol\" type=\"text\" value=\"" + String(config.trk_symmove[1]) + "\" style=\"background-color: rgb(97, 239, 170);\" /> <img border=\"1\" style=\"vertical-align: middle;\" id=\"moveImgSymbol\" onclick=\"openWindowSymbol(1);\" src=\"http://aprs.dprns.com/symbols/icons/" + String((int)config.trk_symmove[1]) + "-" + table + ".png\"> <i>*Click icon for select MOVE symbol</i></td>\n";
	html += "</tr>\n";
	html += "<tr>\n";
	html += "<td align=\"right\">Stop Symbol:</td>\n";
	table = "1";
	if (config.trk_symstop[0] == 47)
		table = "1";
	if (config.trk_symstop[0] == 92)
		table = "2";
	html += "<td style=\"text-align: left;\">Table:<input maxlength=\"1\" size=\"1\" id=\"stopTable\" name=\"stopTable\" type=\"text\" value=\"" + String(config.trk_symstop[0]) + "\" style=\"background-color: rgb(97, 239, 170);\" /> Symbol:<input maxlength=\"1\" size=\"1\" id=\"stopSymbol\" name=\"stopSymbol\" type=\"text\" value=\"" + String(config.trk_symstop[1]) + "\" style=\"background-color: rgb(97, 239, 170);\" /> <img border=\"1\" style=\"vertical-align: middle;\" id=\"stopImgSymbol\" onclick=\"openWindowSymbol(2);\" src=\"http://aprs.dprns.com/symbols/icons/" + String((int)config.trk_symstop[1]) + "-" + table + ".png\"> <i>*Click icon for select STOP symbol</i></td>\n";
	html += "</tr>\n";
	html += "<tr><td style=\"text-align: right;\">High Speed:</td><td style=\"text-align: left;\"><input size=\"3\" min=\"10\" max=\"1000\" step=\"1\" id=\"hspeed\" name=\"hspeed\" type=\"number\" value=\"" + String(config.trk_hspeed) + "\" /> km/h</td></tr>\n";
	html += "<tr><td style=\"text-align: right;\">Low Speed:</td><td style=\"text-align: left;\"><input size=\"3\" min=\"1\" max=\"250\" step=\"1\" id=\"lspeed\" name=\"lspeed\" type=\"number\" value=\"" + String(config.trk_lspeed) + "\" /> km/h</td></tr>\n";
	html += "<tr><td style=\"text-align: right;\">Slow Interval:</td><td style=\"text-align: left;\"><input size=\"3\" min=\"60\" max=\"3600\" step=\"1\" id=\"slowInterval\" name=\"slowInterval\" type=\"number\" value=\"" + String(config.trk_slowinterval) + "\" /> Sec.</td></tr>\n";
	html += "<tr><td style=\"text-align: right;\">Max Interval:</td><td style=\"text-align: left;\"><input size=\"3\" min=\"10\" max=\"255\" step=\"1\" id=\"maxInterval\" name=\"maxInterval\" type=\"number\" value=\"" + String(config.trk_maxinterval) + "\" /> Sec.</td></tr>\n";
	html += "<tr><td style=\"text-align: right;\">Min Interval:</td><td style=\"text-align: left;\"><input size=\"3\" min=\"1\" max=\"100\" step=\"1\" id=\"minInterval\" name=\"minInterval\" type=\"number\" value=\"" + String(config.trk_mininterval) + "\" /> Sec.</td></tr>\n";
	html += "<tr><td style=\"text-align: right;\">Min Angle:</td><td style=\"text-align: left;\"><input size=\"3\" min=\"1\" max=\"359\" step=\"1\" id=\"minAngle\" name=\"minAngle\" type=\"number\" value=\"" + String(config.trk_minangle) + "\" /> Degree.</td></tr>\n";

	html += "</table></fieldset></tr></table><br />\n";
	html += "<div><button type='submit' id='submitTRACKER'  name=\"commitTRACKER\"> Apply Change </button></div>\n";
	html += "<input type=\"hidden\" name=\"commitTRACKER\"/>\n";
	html += "</form><br />";
	request->send(200, "text/html", html); // send to someones browser when asked
}

void handleWirelessGet(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    String html = "";
    File file = SPIFFS.open("/wireless.html", "r");
    if (!file) {
        request->send(500, "text/plain", "Error loading wireless.html");
        return;
    }
    while (file.available()) {
        html += file.readString();
    }
    file.close();

	// Reemplazar el marcador para WiFi STA Enable principal
	html.replace("%STA_ENABLE%", (config.wifi_mode & WIFI_STA_FIX) ? "checked" : "");

    html.replace("%AP_ENABLE%", (config.wifi_mode & WIFI_AP_FIX) ? "checked" : "");
    html.replace("%AP_SSID%", String(config.wifi_ap_ssid));
    html.replace("%AP_PASSWORD%", String(config.wifi_ap_pass));

	for (int i = 0; i < 5; i++) {
		html.replace("%STA" + String(i + 1) + "_ENABLE%", config.wifi_sta[i].enable ? "checked" : "");
		html.replace("%STA" + String(i + 1) + "_SSID%", String(config.wifi_sta[i].wifi_ssid));
		html.replace("%STA" + String(i + 1) + "_PASSWORD%", String(config.wifi_sta[i].wifi_pass));
	}

    request->send(200, "text/html", html);
}

void handleWirelessPost(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    if (request->hasArg("commitWiFiAP")) {
        config.wifi_mode &= ~WIFI_AP_FIX;
        if (request->hasArg("wifiAP") && request->arg("wifiAP") == "OK") {
            config.wifi_mode |= WIFI_AP_FIX;
        }
        if (request->hasArg("wifi_ssidAP")) {
            strncpy(config.wifi_ap_ssid, request->arg("wifi_ssidAP").c_str(), sizeof(config.wifi_ap_ssid));
        }
        if (request->hasArg("wifi_passAP")) {
            strncpy(config.wifi_ap_pass, request->arg("wifi_passAP").c_str(), sizeof(config.wifi_ap_pass));
        }
    }

    if (request->hasArg("commitWiFiClient")) {
        config.wifi_mode &= ~WIFI_STA_FIX;
        if (request->hasArg("wificlient") && request->arg("wificlient") == "on") { // Corregido para esperar "on"
            config.wifi_mode |= WIFI_STA_FIX;
        }

		for (int i = 0; i < 5; i++) {
			String enableKey = "wifiStation" + String(i);
			config.wifi_sta[i].enable = request->hasArg(enableKey.c_str()) && request->arg(enableKey.c_str()) == "on";

			String ssidKey = "wifi_ssid" + String(i);
			if (request->hasArg(ssidKey.c_str())) {
				strncpy(config.wifi_sta[i].wifi_ssid, request->arg(ssidKey.c_str()).c_str(), sizeof(config.wifi_sta[i].wifi_ssid));
			}

			String passKey = "wifi_pass" + String(i);
			if (request->hasArg(passKey.c_str())) {
				strncpy(config.wifi_sta[i].wifi_pass, request->arg(passKey.c_str()).c_str(), sizeof(config.wifi_sta[i].wifi_pass));
			}
		}

    }

    saveEEPROM();
    request->redirect("/wireless?saved=true");
}

extern bool afskSync;
extern String lastPkgRaw;
extern float dBV;
extern int mVrms;
void handle_realtime(AsyncWebServerRequest *request)
{
	// char jsonMsg[1000];
	char *jsonMsg;
	time_t timeStamp;
	time(&timeStamp);

	if (afskSync && (lastPkgRaw.length() > 5))
	{
		int input_length = lastPkgRaw.length();
		jsonMsg = (char *)malloc((input_length * 2) + 200);
		char *input_buffer = (char *)malloc(input_length + 2);
		char *output_buffer = (char *)malloc(input_length * 2);
		if (output_buffer)
		{
			// lastPkgRaw.toCharArray(input_buffer, lastPkgRaw.length(), 0);
			memcpy(input_buffer, lastPkgRaw.c_str(), lastPkgRaw.length());
			lastPkgRaw.clear();
			encode_base64((unsigned char *)input_buffer, input_length, (unsigned char *)output_buffer);
			// Serial.println(output_buffer);
			sprintf(jsonMsg, "{\"Active\":\"1\",\"mVrms\":\"%d\",\"RAW\":\"%s\",\"timeStamp\":\"%li\"}", mVrms, output_buffer, timeStamp);
			// Serial.println(jsonMsg);
			free(input_buffer);
			free(output_buffer);
		}
	}
	else
	{
		jsonMsg = (char *)malloc(100);
		if (afskSync)
			sprintf(jsonMsg, "{\"Active\":\"1\",\"mVrms\":\"%d\",\"RAW\":\"REVDT0RFIEZBSUwh\",\"timeStamp\":\"%li\"}", mVrms, timeStamp);
		else
			sprintf(jsonMsg, "{\"Active\":\"0\",\"mVrms\":\"0\",\"RAW\":\"\",\"timeStamp\":\"%li\"}", timeStamp);
	}
	afskSync = false;
	request->send(200, "text/html", String(jsonMsg));

	delay(100);
	free(jsonMsg);
}

void handle_ws()
{
	// char jsonMsg[1000];
	char *jsonMsg;
	time_t timeStamp;
	time(&timeStamp);

	if(ws.count()<1) return;

	if (afskSync && (lastPkgRaw.length() > 5))
	{
		int input_length = lastPkgRaw.length();
		jsonMsg = (char *)calloc((input_length * 2) + 200, sizeof(char));
		if (jsonMsg)
		{
			char *input_buffer = (char *)calloc(input_length + 2, sizeof(char));
			char *output_buffer = (char *)calloc(input_length * 2, sizeof(char));
			if (output_buffer)
			{
				memset(input_buffer, 0, (input_length + 2));
				memset(output_buffer, 0, (input_length * 2));
				// lastPkgRaw.toCharArray(input_buffer, input_length, 0);
				memcpy(input_buffer, lastPkgRaw.c_str(), lastPkgRaw.length());
				lastPkgRaw.clear();
				encode_base64((unsigned char *)input_buffer, input_length, (unsigned char *)output_buffer);
				// Serial.println(output_buffer);
				sprintf(jsonMsg, "{\"Active\":\"1\",\"mVrms\":\"%d\",\"RAW\":\"%s\",\"timeStamp\":\"%li\"}", mVrms, output_buffer, timeStamp);
				// Serial.println(jsonMsg);
				free(input_buffer);
				free(output_buffer);
			}
			ws.textAll(jsonMsg);
			free(jsonMsg);
		}
	}
	else
	{
		jsonMsg = (char *)calloc(300, sizeof(char));
		if (jsonMsg )
		{
			if (afskSync)
				sprintf(jsonMsg, "{\"Active\":\"1\",\"mVrms\":\"%d\",\"RAW\":\"REVDT0RFIEZBSUwh\",\"timeStamp\":\"%li\"}", mVrms, timeStamp);
			else
				sprintf(jsonMsg, "{\"Active\":\"0\",\"mVrms\":\"0\",\"RAW\":\"\",\"timeStamp\":\"%li\"}", timeStamp);
			ws.textAll(jsonMsg);
			free(jsonMsg);
		}
	}
	afskSync = false;
}

void handle_ws_gnss(char *nmea, size_t size)
{
	time_t timeStamp;
	time(&timeStamp);

	unsigned int output_length = encode_base64_length(size);
	unsigned char nmea_enc[output_length];
	char jsonMsg[output_length + 200];
	encode_base64((unsigned char *)nmea, size, (unsigned char *)nmea_enc);
	// Serial.println(output_buffer);
	sprintf(jsonMsg, "{\"en\":\"%d\",\"lat\":\"%.5f\",\"lng\":\"%.5f\",\"alt\":\"%.2f\",\"spd\":\"%.2f\",\"csd\":\"%.1f\",\"hdop\":\"%.2f\",\"sat\":\"%d\",\"timeStamp\":\"%li\",\"RAW\":\"%s\"}", (int)config.gnss_enable, gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.speed.kmph(), gps.course.deg(), gps.hdop.hdop(), gps.satellites.value(), timeStamp, nmea_enc);
	ws_gnss.textAll(jsonMsg, strlen(jsonMsg));
}

void handle_tnc2(AsyncWebServerRequest *request) {
    // Autenticación si es necesaria
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    // Servir el archivo estático desde SPIFFS
    request->send(SPIFFS, "/tnc2.html", "text/html");
}

void handle_about_get(AsyncWebServerRequest *request) {
    if (SPIFFS.exists("/about.html")) {
        File file = SPIFFS.open("/about.html", "r");
        String html = file.readString();
        file.close();

        // Determinar modo WiFi
        String wifiMode;
        if (config.wifi_mode == WIFI_AP_FIX) {
            wifiMode = "AP";
        } else if (config.wifi_mode == WIFI_STA_FIX) {
            wifiMode = "STA";
        } else if (config.wifi_mode == WIFI_AP_STA_FIX) {
            wifiMode = "AP+STA";
        } else {
            wifiMode = "OFF";
        }
        html.replace("%WIFI_MODE%", wifiMode);

        // Reemplazo de marcadores dinámicos
        html.replace("%HARDWARE_VERSION%", "ESP32DR Simple, ESP32DR_SA868, DIY");
        html.replace("%FIRMWARE_VERSION%", "V" + String(VERSION) + String(VERSION_BUILD));
        html.replace("%RF_ANALOG_MODULE%", "MODEL: " + String(RF_TYPE[config.rf_type]) + " (" + RF_VERSION + ")");
        html.replace("%ESP32_MODEL%", String(ESP.getChipModel()));
        uint64_t chipid = ESP.getEfuseMac();
        char strCID[50];
        sprintf(strCID, "%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
        html.replace("%CHIP_ID%", String(strCID));
        html.replace("%REVISION%", String(ESP.getChipRevision()));
        html.replace("%FLASH%", String(ESP.getFlashChipSize() / 1024) + " KByte");
        html.replace("%PSRAM%", String(ESP.getPsramSize() / 1024) + " KByte");

        html.replace("%WIFI_MODE%", String(WiFi.getMode() == WIFI_MODE_APSTA ? "AP+STA" : 
                          WiFi.getMode() == WIFI_MODE_AP ? "AP" : 
                          WiFi.getMode() == WIFI_MODE_STA ? "STA" : "Unknown"));
        html.replace("%MAC%", WiFi.macAddress());
        html.replace("%CHANNEL%", String(WiFi.channel()));

        // Determinar potencia WiFi
        wifi_power_t wpr = WiFi.getTxPower();
        String wifipower = "20 dBm"; // Default max power
        if (wpr < 8) { wifipower = "-1 dBm"; }
        else if (wpr < 21) { wifipower = "2 dBm"; }
        else if (wpr < 29) { wifipower = "5 dBm"; }
        else if (wpr < 35) { wifipower = "8.5 dBm"; }
        else if (wpr < 45) { wifipower = "11 dBm"; }
        else if (wpr < 53) { wifipower = "13 dBm"; }
        else if (wpr < 61) { wifipower = "15 dBm"; }
        else if (wpr < 69) { wifipower = "17 dBm"; }
        else if (wpr < 75) { wifipower = "18.5 dBm"; }
        else if (wpr < 77) { wifipower = "19 dBm"; }
        else if (wpr < 80) { wifipower = "19.5 dBm"; }
        html.replace("%TX_POWER%", wifipower);
        html.replace("%SSID%", WiFi.SSID());
        html.replace("%LOCAL_IP%", WiFi.localIP().toString());
        html.replace("%GATEWAY_IP%", WiFi.gatewayIP().toString());
        html.replace("%DNS_IP%", WiFi.dnsIP().toString());

        request->send(200, "text/html", html);
    } else {
        request->send(404, "text/plain", "404: Not Found");
    }
}

void handle_about_post(AsyncWebServerRequest *request) {
    // Procesa solicitudes POST para "/about"
    request->send(200, "text/plain", "POST request to /about received");
}

void handle_gnss(AsyncWebServerRequest *request) {
    if (!SPIFFS.begin(true)) {
        Serial.println("Error montando SPIFFS");
        request->send(500, "text/plain", "Error al montar SPIFFS");
        return;
    }

    File file = SPIFFS.open("/gnss.html", "r");
    if (!file) {
        request->send(500, "text/plain", "Error abriendo gnss.html");
        return;
    }

    // Leer el contenido del archivo
    String html = file.readString();
    file.close();

    // Reemplazar marcadores con valores dinámicos
    html.replace("%Latitude%", String(gps.location.lat(), 5));
    html.replace("%Longitude%", String(gps.location.lng(), 5));
    html.replace("%Altitude%", String(gps.altitude.meters(), 2) + " m");
    html.replace("%Speed%", String(gps.speed.kmph(), 2) + " km/h");
    html.replace("%Course%", String(gps.course.deg(), 1));
    html.replace("%HDOP%", String(gps.hdop.hdop(), 2));
    html.replace("%Satellites%", String(gps.satellites.value()));

    // Enviar el HTML modificado al cliente
    request->send(200, "text/html", html);
}

void handleDebugPage(AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/debug.html", "text/html");
}

String escapeJson(String input) {
    input.replace("\\", "\\\\");  // Escapar barra invertida
    input.replace("\"", "\\\"");  // Escapar comillas dobles
    return input;
}

void handleDebugData(AsyncWebServerRequest *request) {
    String json = "{";

    // Sección Radio
    json += "\"radio\":{";
    json += "\"rfEnable\":\"" + String(config.rf_en ? "On" : "Off") + "\",";
    json += "\"rfModuleType\":\"" + String(config.rf_type == RF_SA868_VHF ? "SA868_VHF" : "Other") + "\",";
    json += "\"txFrequency\":\"" + String(config.freq_tx, 4) + " MHz\",";
    json += "\"rxFrequency\":\"" + String(config.freq_rx, 4) + " MHz\",";
    json += "\"txPower\":\"" + String(config.rf_power == HIGH ? "High" : "Low") + "\",";
    json += "\"volume\":\"" + String(config.volume) + "\",";
    json += "\"sqlLevel\":\"" + String(config.sql_level) + "\",";
    json += "\"modemType\":\"" + String(config.modem_type == 1 ? "AFSK1200" : "Other") + "\",";
    json += "\"audioHpf\":\"" + String(config.audio_hpf ? "On" : "Off") + "\",";
    json += "\"audioBpf\":\"" + String(config.audio_bpf ? "On" : "Off") + "\",";
    json += "\"txTimeSlot\":\"" + String(config.tx_timeslot) + " ms\",";
    json += "\"preamble\":\"" + String(config.preamble) + "\"";
    json += "},";

    // Sección IGate
	json += "\"igate\":{";
	json += "\"enable\":\"" + String(config.igate_en ? "On" : "Off") + "\",";
	json += "\"rf2inet\":\"" + String(config.rf2inet ? "On" : "Off") + "\",";
	json += "\"inet2rf\":\"" + String(config.inet2rf ? "On" : "Off") + "\",";
	json += "\"locationToInet\":\"" + String(config.igate_loc2inet ? "On" : "Off") + "\",";
	json += "\"locationToRf\":\"" + String(config.igate_loc2rf ? "On" : "Off") + "\",";
	json += "\"rf2inetFilter\":\"" + String(config.rf2inetFilter, HEX) + "\",";
	json += "\"inet2rfFilter\":\"" + String(config.inet2rfFilter, HEX) + "\",";
	json += "\"stationCallsign\":\"" + String(config.aprs_mycall) + "\",";
	json += "\"stationSsid\":\"" + String(config.aprs_ssid) + "\",";
	json += "\"path\":\"" + String(config.igate_path) + "\",";
	json += "\"host\":\"" + String(config.aprs_host) + "\",";
	json += "\"port\":\"" + String(config.aprs_port) + "\",";
	json += "\"passcode\":\"" + String(config.aprs_passcode) + "\",";
	json += "\"symbol\":\"" + String(config.igate_symbol) + "\",";
	json += "\"latitude\":\"" + String(config.igate_lat, 4) + "\",";
	json += "\"longitude\":\"" + String(config.igate_lon, 4) + "\",";
	json += "\"altitude\":\"" + String(config.igate_alt) + "\",";
	json += "\"interval\":\"" + String(config.igate_interval) + " s\",";
	json += "\"comment\":\"" + String(config.igate_comment) + "\"";
	json += "},";

    // Sección Digi
	json += "\"digi\":{";
	json += "\"enable\":\"" + String(config.digi_en ? "On" : "Off") + "\",";
	json += "\"locationToInet\":\"" + String(config.digi_loc2inet ? "On" : "Off") + "\",";
	json += "\"locationToRf\":\"" + String(config.digi_loc2rf ? "On" : "Off") + "\",";
	json += "\"filter\":\"" + String(config.digiFilter, HEX) + "\",";
	json += "\"stationCallsign\":\"" + String(config.digi_mycall) + "\",";
	json += "\"stationSsid\":\"" + String(config.digi_ssid) + "\",";
	json += "\"path\":\"" + String(config.digi_path) + "\",";
	json += "\"latitude\":\"" + String(config.digi_lat, 4) + "\",";
	json += "\"longitude\":\"" + String(config.digi_lon, 4) + "\",";
	json += "\"altitude\":\"" + String(config.digi_alt) + "\",";
	json += "\"interval\":\"" + String(config.digi_interval) + " s\",";
	json += "\"timestamp\":\"" + String(config.digi_timestamp ? "On" : "Off") + "\",";
	json += "\"delay\":\"" + String(config.digi_delay) + " ms\",";
	json += "\"symbol\":\"" + String(config.digi_symbol) + "\",";
	json += "\"comment\":\"" + String(config.digi_comment) + "\"";
	json += "},";

    // Sección Tracker
	json += "\"tracker\":{";
	json += "\"enable\":\"" + String(config.trk_en ? "On" : "Off") + "\",";
	json += "\"locationToInet\":\"" + String(config.trk_loc2inet ? "On" : "Off") + "\",";
	json += "\"locationToRf\":\"" + String(config.trk_loc2rf ? "On" : "Off") + "\",";
	json += "\"smartBeacon\":\"" + String(config.trk_smartbeacon ? "On" : "Off") + "\",";
	json += "\"compressed\":\"" + String(config.trk_compress ? "On" : "Off") + "\",";
	json += "\"altitudeIncluded\":\"" + String(config.trk_altitude ? "On" : "Off") + "\",";
	json += "\"timestamp\":\"" + String(config.trk_timestamp ? "On" : "Off") + "\",";
	json += "\"highSpeed\":\"" + String(config.trk_hspeed) + " km/h\",";
	json += "\"lowSpeed\":\"" + String(config.trk_lspeed) + " km/h\",";
	json += "\"minInterval\":\"" + String(config.trk_mininterval) + " s\",";
	json += "\"maxInterval\":\"" + String(config.trk_maxinterval) + " s\",";
	json += "\"minAngle\":\"" + String(config.trk_minangle) + "°\",";
	json += "\"slowInterval\":\"" + String(config.trk_slowinterval) + " s\",";
	json += "\"stationCallsign\":\"" + String(config.trk_mycall) + "\",";
	json += "\"stationSsid\":\"" + String(config.trk_ssid) + "\",";
	json += "\"path\":\"" + String(config.trk_path) + "\",";
	json += "\"latitude\":\"" + String(config.trk_lat, 4) + "\",";
	json += "\"longitude\":\"" + String(config.trk_lon, 4) + "\",";
	json += "\"altitude\":\"" + String(config.trk_alt) + "\",";
	json += "\"interval\":\"" + String(config.trk_interval) + " s\",";
	json += "\"symbol\":\"" + String(config.trk_symbol) + "\",";
	json += "\"movingSymbol\":\"" + String(config.trk_symmove) + "\",";
	json += "\"stoppedSymbol\":\"" + escapeJson(String(config.trk_symstop)) + "\",";
	json += "\"comment\":\"" + String(config.trk_comment) + "\"";
	json += "},";

	// Sección WX
	json += "\"wx\":{";
	json += "\"enable\":\"" + String(config.wx_en ? "On" : "Off") + "\",";
	json += "\"locationToInet\":\"" + String(config.wx_2inet ? "On" : "Off") + "\",";
	json += "\"locationToRf\":\"" + String(config.wx_2rf ? "On" : "Off") + "\",";
	json += "\"stationCallsign\":\"" + String(config.wx_mycall) + "\",";
	json += "\"stationSsid\":\"" + String(config.wx_ssid) + "\",";
	json += "\"path\":\"" + String(config.wx_path) + "\",";
	json += "\"latitude\":\"" + String(config.wx_lat, 4) + "\",";
	json += "\"longitude\":\"" + String(config.wx_lon, 4) + "\",";
	json += "\"altitude\":\"" + String(config.wx_alt) + "\",";
	json += "\"interval\":\"" + String(config.wx_interval) + " s\",";
	json += "\"comment\":\"" + String(config.wx_comment) + "\"";
	json += "},";

	// Sección TLM
	json += "\"tlm\":{";
	json += "\"enable\":\"" + String(config.tlm0_en ? "On" : "Off") + "\",";
	json += "\"locationToInet\":\"" + String(config.tlm0_2inet ? "On" : "Off") + "\",";
	json += "\"locationToRf\":\"" + String(config.tlm0_2rf ? "On" : "Off") + "\",";
	json += "\"stationCallsign\":\"" + String(config.tlm0_mycall) + "\",";
	json += "\"stationSsid\":\"" + String(config.tlm0_ssid) + "\",";
	json += "\"path\":\"" + String(config.tlm0_path) + "\",";
	json += "\"dataInterval\":\"" + String(config.tlm0_data_interval) + " s\",";
	json += "\"infoInterval\":\"" + String(config.tlm0_info_interval) + " s\",";
	json += "\"activeBits\":\"" + String(config.tlm0_BITS_Active, HEX) + "\",";
	json += "\"comment\":\"" + String(config.tlm0_comment) + "\"";
	json += "},";

	// Sección MOD
	json += "\"mod\":{";
	json += "\"gnssEnable\":\"" + String(config.gnss_enable ? "On" : "Off") + "\",";
	json += "\"gnssPort\":\"" + String(config.gnss_channel) + "\",";
	json += "\"gnssAtCommand\":\"" + String(config.gnss_at_command) + "\",";
	json += "\"gnssTcpHost\":\"" + String(config.gnss_tcp_host) + "\",";
	json += "\"gnssTcpPort\":\"" + String(config.gnss_tcp_port) + "\",";
	json += "\"modbusEnable\":\"" + String(config.modbus_enable ? "On" : "Off") + "\",";
	json += "\"modbusChannel\":\"" + String(config.modbus_channel) + "\",";
	json += "\"modbusAddress\":\"" + String(config.modbus_address) + "\",";
	json += "\"modbusDeGpio\":\"" + String(config.modbus_de_gpio) + "\",";
	json += "\"uart0Enable\":\"" + String(config.uart0_enable ? "On" : "Off") + "\",";
	json += "\"uart0Baudrate\":\"" + String(config.uart0_baudrate) + "\",";
	json += "\"uart0RxGpio\":\"" + String(config.uart0_rx_gpio) + "\",";
	json += "\"uart0TxGpio\":\"" + String(config.uart0_tx_gpio) + "\",";
	json += "\"uart0RtsGpio\":\"" + String(config.uart0_rts_gpio) + "\",";
	json += "\"uart1Enable\":\"" + String(config.uart1_enable ? "On" : "Off") + "\",";
	json += "\"uart1Baudrate\":\"" + String(config.uart1_baudrate) + "\",";
	json += "\"uart1RxGpio\":\"" + String(config.uart1_rx_gpio) + "\",";
	json += "\"uart1TxGpio\":\"" + String(config.uart1_tx_gpio) + "\",";
	json += "\"uart1RtsGpio\":\"" + String(config.uart1_rts_gpio) + "\",";
	json += "\"uart2Enable\":\"" + String(config.uart2_enable ? "On" : "Off") + "\",";
	json += "\"uart2Baudrate\":\"" + String(config.uart2_baudrate) + "\",";
	json += "\"uart2RxGpio\":\"" + String(config.uart2_rx_gpio) + "\",";
	json += "\"uart2TxGpio\":\"" + String(config.uart2_tx_gpio) + "\",";
	json += "\"uart2RtsGpio\":\"" + String(config.uart2_rts_gpio) + "\",";
	json += "\"rfRxGpio\":\"" + String(config.rf_rx_gpio) + "\",";
	json += "\"rfTxGpio\":\"" + String(config.rf_tx_gpio) + "\",";
	json += "\"rfPdGpio\":\"" + String(config.rf_pd_gpio) + "\",";
	json += "\"rfPttGpio\":\"" + String(config.rf_ptt_gpio) + "\",";
	json += "\"rfBaudrate\":\"" + String(config.rf_baudrate) + "\",";
	json += "\"adcAtten\":\"" + String(config.adc_atten) + "\",";
	json += "\"adcDcOffset\":\"" + String(config.adc_dc_offset) + "\"";
	json += "},";

    // Sección System
	json += "\"system\":{";
	json += "\"localDateTime\":\"" + String(now()) + "\",";
	json += "\"ntpHost\":\"" + String(config.ntp_host) + "\",";
	json += "\"timeZone\":\"" + String(config.timeZone) + "\",";
	json += "\"webUser\":\"" + String(config.http_username) + "\",";
	json += "\"webPassword\":\"****\",";
	json += "\"paths\":{";
	json += "\"path1\":\"" + String(config.path[0]) + "\",";
	json += "\"path2\":\"" + String(config.path[1]) + "\",";
	json += "\"path3\":\"" + String(config.path[2]) + "\",";
	json += "\"path4\":\"" + String(config.path[3]) + "\"";
	json += "}";
	json += "},";

	json += "\"display\":{";
	json += "\"oledEnable\":\"" + String(config.oled_enable ? "On" : "Off") + "\",";
	json += "\"txDisplay\":\"" + String(config.tx_display ? "On" : "Off") + "\",";
	json += "\"rxDisplay\":\"" + String(config.rx_display ? "On" : "Off") + "\",";
	json += "\"headUp\":\"" + String(config.h_up ? "On" : "Off") + "\",";
	json += "\"popupDelay\":\"" + String(config.dispDelay) + " ms\",";
	json += "\"rxChannelRF\":\"" + String(config.dispRF) + "\",";
	json += "\"rxChannelINET\":\"" + String(config.dispINET) + "\",";
	json += "\"filterDx\":\"" + String(config.filterDistant) + "\",";
	json += "\"filters\":\"" + String(config.dispFilter) + "\"";
    json += "}"; // Sin coma final

    json += "}"; // Cerramos el JSON principal

    // Enviar respuesta al cliente
    request->send(200, "application/json", json);
}

void handle_default()
{
	defaultSetting = true;
	defaultConfig();
	defaultSetting = false;
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{

	if (type == WS_EVT_CONNECT)
	{

		log_d("Websocket client connection received");
	}
	else if (type == WS_EVT_DISCONNECT)
	{

		log_d("Client disconnected");
	}
}

bool webServiceBegin = true;
void webService()
{
	if (webServiceBegin)
	{
		webServiceBegin = false;
	}
	else
	{
		return;
	}
	ws.onEvent(onWsEvent);

	// web client handlers
	async_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
					{ setMainPage(request); });										// INDEX	
	async_server.on("/list-files", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handle_list_files(request);});								// for debug
	async_server.on("/symbol", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_symbol(request); });									// for APRS Symbols
	async_server.on("/radio", HTTP_GET, [](AsyncWebServerRequest *request) 	
					{ handle_radio_get(request);});									// RADIO GET	
	async_server.on("/radio", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_radio_post(request);});								// RADIO POST
	async_server.on("/mod/uart", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handle_uart_config(request);});		 						// MOD UART GET
	async_server.on("/mod/uart", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_uart_post(request);});		 						// MOD UART POST									
	async_server.on("/mod/rf", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handle_rf_config(request);});									// MOD RF GET
	async_server.on("/mod/rf", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handle_rf_post(request);});									// MOD RF POST				
	async_server.on("/mod/gnss", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handle_gnss_config(request);});								// MOD GNSS GET
	async_server.on("/mod/gnss", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_gnss_post(request);});									// MOD GNSS POST
	async_server.on("/mod/modbus", HTTP_GET, [](AsyncWebServerRequest *request) 	
					{ handle_modbus_config(request);});								// MOD MODBUS GET
	async_server.on("/mod/modbus", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_modbus_post(request); });								// MOD MODBUS POST
	async_server.on("/mod/counter", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handle_counter_config(request);});							// MOD COUNTER GET
	async_server.on("/mod/counter", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_counter_post(request); });								// MOD COUNTER POST
	async_server.on("/mod/i2c", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_i2c_config(request);});								// MOD I2C GET
	async_server.on("/mod/i2c", HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_i2c_post(request);});									// MOD I2C POST
	async_server.on("/default", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_default(); });											// DEFAULT	
	// igate					
	async_server.on("/igate/general", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleIgateGeneralGet(request);});
	async_server.on("/igate/general", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleIgateGeneralPost(request);});
	async_server.on("/igate/position", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleIgatePositionGet(request);});
	async_server.on("/igate/position", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleIgatePositionPost(request);});
	async_server.on("/igate/phg", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleIgatePHGGet(request);});
	async_server.on("/igate/phg", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleIgatePHGPost(request);});
	async_server.on("/igate/filter", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleIgateFilterGet(request);});
	async_server.on("/igate/filter", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleIgateFilterPost(request);});
	// digipeater				
	async_server.on("/digi/general", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleDigiGeneral(request);});
	async_server.on("/digi/general", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleDigiGeneralPost(request);});
	async_server.on("/digi/position", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleDigiPosition(request);});
	async_server.on("/digi/position", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleDigiPositionPost(request);});
	async_server.on("/digi/phg", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleDigiPHG(request);});
	async_server.on("/digi/phg", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleDigiPHGPost(request);});
	async_server.on("/digi/filter", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleDigiFilter(request);});
	async_server.on("/digi/filter", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleDigiFilterPost(request);});
	// Tracker General
    async_server.on("/tracker/general", HTTP_GET, [](AsyncWebServerRequest *request) 
                    { handleTrackerGeneral(request); });
    async_server.on("/tracker/general", HTTP_POST, [](AsyncWebServerRequest *request) 
                    { handleTrackerGeneralPost(request); });

    // Tracker Position
    async_server.on("/tracker/position", HTTP_GET, [](AsyncWebServerRequest *request) 
                    { handleTrackerPosition(request); });
    async_server.on("/tracker/position", HTTP_POST, [](AsyncWebServerRequest *request) 
                    { handleTrackerPositionPost(request); });

    // Tracker SmartBeacon
    async_server.on("/tracker/smartbeacon", HTTP_GET, [](AsyncWebServerRequest *request) 
                    { handleTrackerSmartBeacon(request); });
    async_server.on("/tracker/smartbeacon", HTTP_POST, [](AsyncWebServerRequest *request) 
                    { handleTrackerSmartBeaconPost(request); });
							
	async_server.on("/wx", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_wx(request); });	
														
	// async_server.on("/tlm", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
	// 				{ handle_tlm(request); });										// TLM

	async_server.on("/tlm/general", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleTlmGeneral(request); });
	async_server.on("/tlm/general", HTTP_POST, [](AsyncWebServerRequest *request) 	
					{ handleTlmGeneralPost(request); });

	async_server.on("/tlm/channela", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleTlmChannelA(request); });
	async_server.on("/tlm/channela", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleTlmChannelAPost(request); });

	async_server.on("/tlm/channelb", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleTlmChannelB(request); });
	async_server.on("/tlm/channelb", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleTlmChannelBPost(request); });

	async_server.on("/system", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handle_system_get(request);});								// SYSTEM GET	
	async_server.on("/system", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_system_post(request);});								// SYSTEM POST	
	async_server.on("/wireless", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleWirelessGet(request);});								// WIRELESS GET	
	async_server.on("/wireless", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleWirelessPost(request);});								// WIRELESS POST	
	async_server.on("/tnc2", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_tnc2(request); });										// TNC2
	async_server.on("/gnss", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_gnss(request); });										// GNSS
	async_server.on("/realtime", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_realtime(request); });									// REALTIME
    async_server.on("/about", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_about_get(request); });								// ABOUT GET
    async_server.on("/about", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_about_post(request); });								// ABOUT POST
	async_server.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_dashboard(request); });								// DASHBOARD
	async_server.on("/sidebarInfo", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_sidebar(request); });									// SIDEBAR
	async_server.on("/sysinfo", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_sysinfo(request); });									// SYSINFO
	async_server.on("/lastHeard", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_lastHeard(request); });								// LASTHEARD
	async_server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_css(request); });										// CSS
	async_server.on("/jquery-3.7.1.js", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_jquery(request); });									// JQUERY
	async_server.on("/modes-enabled", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleModesEnabled(request); });								// MODES ENABLED	
	async_server.on("/network-status", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleNetworkStatus(request); });								// NETWORK STATUS
	async_server.on("/statistics", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleStatistics(request); });								// STATISTICS
	async_server.on("/radio-info", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleRadioInfo(request); });									// RADIO INFO
	async_server.on("/aprs-server", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleAPRSServer(request); });								// APRS SERVER
	async_server.on("/wifi-info", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleWiFiInfo(request); });									// WIFI INFO
	async_server.on("/debug-info", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleDebugData(request); });									// DEBUG INFO						
	async_server.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleDebugPage(request); });									// DEBUG PAGE
	async_server.on(
		"/update", HTTP_POST, [](AsyncWebServerRequest *request)
		{
  		bool espShouldReboot = !Update.hasError();
  		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", espShouldReboot ? "<h1><strong>Update DONE</strong></h1><br><a href='/'>Return Home</a>" : "<h1><strong>Update FAILED</strong></h1><br><a href='/updt'>Retry?</a>");
  		response->addHeader("Connection", "close");
  		request->send(response); },
		[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
		{
			if (!index)
			{
				Serial.printf("Update Start: %s\n", filename.c_str());
				if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000))
				{
					Update.printError(Serial);
				}
				else
				{
					disableLoopWDT();
					disableCore0WDT();
					disableCore1WDT();
					vTaskSuspend(taskAPRSPollHandle);
					vTaskSuspend(taskAPRSHandle);
				}
			}
			if (!Update.hasError())
			{
				if (Update.write(data, len) != len)
				{
					Update.printError(Serial);
				}
			}
			if (final)
			{
				if (Update.end(true))
				{
					Serial.printf("Update Success: %uByte\n", index + len);
					delay(1000);
					esp_restart();
				}
				else
				{
					Update.printError(Serial);
				}
			}
		});

	lastheard_events.onConnect([](AsyncEventSourceClient *client)
							   {
    if(client->lastId()){
      log_d("Web Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 1000); });
	async_server.addHandler(&lastheard_events);
	async_server.onNotFound(notFound);
	async_server.begin();
	async_websocket.addHandler(&ws);
	async_websocket.addHandler(&ws_gnss);
	async_websocket.begin();
}