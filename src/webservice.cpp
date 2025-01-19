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
#include "wireguard_vpn.h"
#include <LibAPRSesp.h>
#include <parse_aprs.h>
#include "jquery_min_js.h"
#include "SPIFFS.h"

AsyncWebServer async_server(80);
AsyncWebServer async_websocket(81);
AsyncWebSocket ws("/ws");
AsyncWebSocket ws_gnss("/ws_gnss");

// Create an Event Source on /events
AsyncEventSource lastheard_events("/eventHeard");

String loadHtmlTemplate(const char* path) {
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        return "";
    }

    File file = SPIFFS.open(path, "r");
    if (!file || file.isDirectory()) {
        Serial.println("Failed to open file");
        return "";
    }

    String html;
    while (file.available()) {
        html += file.readString();
    }
    file.close();
    return html;
}

String webString;

bool defaultSetting = false;

void serviceHandle()
{
	// server.handleClient();
}

void notFound(AsyncWebServerRequest *request)
{
	request->send(404, "text/plain", "Not found");
}

void handle_logout(AsyncWebServerRequest *request)
{
	webString = "Log out";
	request->send(200, "text/html", webString);
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
	if (wireguard_active() == true)
		html += "<th style=\"background:#0b0; color:#030; width:50%;border-radius: 10px;border: 2px solid white;\">VPN</th>\n";
	else
		html += "<th style=\"background:#606060; color:#b0b0b0;border-radius: 10px;border: 2px solid white;\" aria-disabled=\"true\">VPN</th>\n";
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
    json += "\"vpn\":" + String(wireguard_active() ? "true" : "false") + ",";
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


void handle_lastHeard(AsyncWebServerRequest *request)
{
	struct pbuf_t aprs;
	ParseAPRS aprsParse;
	struct tm tmstruct;
	String html = "";
	sort(pkgList, PKGLISTSIZE);

	html = "<table>\n";
	html += "<th colspan=\"7\" style=\"background-color: #070ac2;\">LAST HEARD <a href=\"/tnc2\" target=\"_tnc2\" style=\"color: yellow;font-size:8pt\">[RAW]</a></th>\n";
	html += "<tr>\n";
	html += "<th style=\"min-width:10ch\"><span><b>Time (";
	if (config.timeZone >= 0)
		html += "+";
	// else
	//	html += "-";

	if (config.timeZone == (int)config.timeZone)
		html += String((int)config.timeZone) + ")</b></span></th>\n";
	else
		html += String(config.timeZone, 1) + ")</b></span></th>\n";
	html += "<th style=\"min-width:16px\">ICON</th>\n";
	html += "<th style=\"min-width:10ch\">Callsign</th>\n";
	html += "<th>VIA LAST PATH</th>\n";
	html += "<th style=\"min-width:5ch\">DX</th>\n";
	html += "<th style=\"min-width:5ch\">PACKET</th>\n";
	html += "<th style=\"min-width:5ch\">AUDIO</th>\n";
	html += "</tr>\n";

	for (int i = 0; i < PKGLISTSIZE; i++)
	{
		if (i >= PKGLISTSIZE)
			break;
		pkgListType pkg = getPkgList(i);
		if (pkg.time > 0)
		{
			String line = String(pkg.raw);
			int packet = pkg.pkg;
			int start_val = line.indexOf(">", 0); // หาตำแหน่งแรกของ >
			if (start_val > 3)
			{
				String src_call = line.substring(0, start_val);
				memset(&aprs, 0, sizeof(pbuf_t));
				aprs.buf_len = 300;
				aprs.packet_len = line.length();
				line.toCharArray(&aprs.data[0], aprs.packet_len);
				int start_info = line.indexOf(":", 0);
				int end_ssid = line.indexOf(",", 0);
				int start_dst = line.indexOf(">", 2);
				int start_dstssid = line.indexOf("-", start_dst);
				String path = "";

				if ((end_ssid > start_dst) && (end_ssid < start_info))
				{
					path = line.substring(end_ssid + 1, start_info);
				}
				if (end_ssid < 5)
					end_ssid = start_info;
				if ((start_dstssid > start_dst) && (start_dstssid < start_dst + 10))
				{
					aprs.dstcall_end_or_ssid = &aprs.data[start_dstssid];
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
				if (aprsParse.parse_aprs(&aprs))
				{
					pkg.calsign[10] = 0;
					// time_t tm = pkg.time;
					localtime_r(&pkg.time, &tmstruct);
					char strTime[10];
					sprintf(strTime, "%02d:%02d:%02d", tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);
					// String str = String(tmstruct.tm_hour, DEC) + ":" + String(tmstruct.tm_min, DEC) + ":" + String(tmstruct.tm_sec, DEC);

					html += "<tr><td>" + String(strTime) + "</td>";
					String fileImg = "";
					uint8_t sym = (uint8_t)aprs.symbol[1];
					if (sym > 31 && sym < 127)
					{
						if (aprs.symbol[0] > 64 && aprs.symbol[0] < 91) // table A-Z
						{
							html += "<td><b>" + String(aprs.symbol[0]) + "</b></td>";
						}
						else
						{
							fileImg = String(sym, DEC);
							if (aprs.symbol[0] == 92)
							{
								fileImg += "-2.png";
							}
							else if (aprs.symbol[0] == 47)
							{
								fileImg += "-1.png";
							}
							else
							{
								fileImg = "dot.png";
							}
							html += "<td><img src=\"http://aprs.dprns.com/symbols/icons/" + fileImg + "\"></td>";
						}
					}
					else
					{
						html += "<td><img src=\"http://aprs.dprns.com/symbols/icons/dot.png\"></td>";
					}
					html += "<td>" + src_call;
					if (aprs.srcname_len > 0 && aprs.srcname_len < 10) // Get Item/Object
					{
						char itemname[10];
						memset(&itemname, 0, sizeof(itemname));
						memcpy(&itemname, aprs.srcname, aprs.srcname_len);
						html += "(" + String(itemname) + ")";
					}
					html += +"</td>";
					if (path == "")
					{
						html += "<td style=\"text-align: left;\">RF: DIRECT</td>";
					}
					else
					{
						String LPath = path.substring(path.lastIndexOf(',') + 1);
						// if(path.indexOf("qAR")>=0 || path.indexOf("qAS")>=0 || path.indexOf("qAC")>=0){ //Via from Internet Server
						if (path.indexOf("qA") >= 0 || path.indexOf("TCPIP") >= 0)
						{
							html += "<td style=\"text-align: left;\">INET: " + LPath + "</td>";
						}
						else
						{
							if (path.indexOf("*") > 0)
							{
								html += "<td style=\"text-align: left;\">DIGI: " + path + "</td>";
							}
							else
							{
								html += "<td style=\"text-align: left;\">RF: " + path + "</td>";
							}
						}
					}
					// html += "<td>" + path + "</td>";
					if (aprs.flags & F_HASPOS)
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
						double dtmp = aprsParse.direction(lon, lat, aprs.lng, aprs.lat);
						double dist = aprsParse.distance(lon, lat, aprs.lng, aprs.lat);
						html += "<td>" + String(dist, 1) + "km/" + String(dtmp, 0) + "°</td>";
					}
					else
					{
						html += "<td>-</td>\n";
					}
					html += "<td>" + String(packet) + "</td>\n";
					if (pkg.audio_level == 0)
					{
						html += "<td>-</td></tr>\n";
					}
					else
					{
						double Vrms = (double)pkg.audio_level / 1000;
						double audBV = 20.0F * log10(Vrms);
						if (audBV < -20.0F)
						{
							html += "<td style=\"color: #0000f0;\">";
						}
						else if (audBV > -5.0F)
						{
							html += "<td style=\"color: #f00000;\">";
						}
						else
						{
							html += "<td style=\"color: #008000;\">";
						}
						html += String(audBV, 1) + "dBV</td></tr>\n";
					}
				}
			}
		}
	}
	html += "</table>\n";
	request->send(200, "text/html", html); // send to someones browser when asked
	delay(100);
	html.clear();
}

void event_lastHeard()
{
	// log_d("Event count: %d",lastheard_events.count());
	if (lastheard_events.count() == 0)
		return;

	struct pbuf_t aprs;
	ParseAPRS aprsParse;
	struct tm tmstruct;

	String html = "";
	String line = "";
	sort(pkgList, PKGLISTSIZE);

	//log_d("Create html last heard");

	html = "<table>\n";
	html += "<th colspan=\"7\" style=\"background-color: #070ac2;\">LAST HEARD <a href=\"/tnc2\" target=\"_tnc2\" style=\"color: yellow;font-size:8pt\">[RAW]</a></th>\n";
	html += "<tr>\n";
	html += "<th style=\"min-width:10ch\"><span><b>Time (";
	if (config.timeZone >= 0)
		html += "+";
	// else
	//	html += "-";

	if (config.timeZone == (int)config.timeZone)
		html += String((int)config.timeZone) + ")</b></span></th>\n";
	else
		html += String(config.timeZone, 1) + ")</b></span></th>\n";
	html += "<th style=\"min-width:16px\">ICON</th>\n";
	html += "<th style=\"min-width:10ch\">Callsign</th>\n";
	html += "<th>VIA LAST PATH</th>\n";
	html += "<th style=\"min-width:5ch\">DX</th>\n";
	html += "<th style=\"min-width:5ch\">PACKET</th>\n";
	html += "<th style=\"min-width:5ch\">AUDIO</th>\n";
	html += "</tr>\n";

	for (int i = 0; i < PKGLISTSIZE; i++)
	{
		if (i >= PKGLISTSIZE)
			break;
		pkgListType pkg = getPkgList(i);
		if (pkg.time > 0)
		{
			line = String(pkg.raw);
			//log_d("IDX=%d RAW:%s",i,line.c_str());
			int packet = pkg.pkg;
			int start_val = line.indexOf(">", 0); // หาตำแหน่งแรกของ >
			if (start_val > 3)
			{
				String src_call = line.substring(0, start_val);
				memset(&aprs, 0, sizeof(pbuf_t));
				aprs.buf_len = 300;
				aprs.packet_len = line.length();
				line.toCharArray(&aprs.data[0], aprs.packet_len);
				int start_info = line.indexOf(":", 0);
				if(start_info<10) continue;
				int start_dst = line.lastIndexOf(">", start_info);
				if(start_dst<5) continue;
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
				if (aprsParse.parse_aprs(&aprs))
				{
					pkg.calsign[10] = 0;
					// time_t tm = pkg.time;
					localtime_r(&pkg.time, &tmstruct);
					char strTime[10];
					sprintf(strTime, "%02d:%02d:%02d", tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);
					// String str = String(tmstruct.tm_hour, DEC) + ":" + String(tmstruct.tm_min, DEC) + ":" + String(tmstruct.tm_sec, DEC);

					html += "<tr><td>" + String(strTime) + "</td>";
					String fileImg = "";
					uint8_t sym = (uint8_t)aprs.symbol[1];
					if (sym > 31 && sym < 127)
					{
						if (aprs.symbol[0] > 64 && aprs.symbol[0] < 91) // table A-Z
						{
							html += "<td><b>" + String(aprs.symbol[0]) + "</b></td>";
						}
						else
						{
							fileImg = String(sym, DEC);
							if (aprs.symbol[0] == 92)
							{
								fileImg += "-2.png";
							}
							else if (aprs.symbol[0] == 47)
							{
								fileImg += "-1.png";
							}
							else
							{
								fileImg = "dot.png";
							}
							html += "<td><img src=\"http://aprs.dprns.com/symbols/icons/" + fileImg + "\"></td>";
						}
						fileImg.clear();
					}
					else
					{
						html += "<td><img src=\"http://aprs.dprns.com/symbols/icons/dot.png\"></td>";
					}
					html += "<td>" + src_call;
					if (aprs.srcname_len > 0 && aprs.srcname_len < 10) // Get Item/Object
					{
						char itemname[10];
						memset(&itemname, 0, 10);
						memcpy(&itemname, aprs.srcname, aprs.srcname_len);
						html += "(" + String(itemname) + ")";
					}
					html += +"</td>";
					if (path == "")
					{
						html += "<td style=\"text-align: left;\">RF: DIRECT</td>";
					}
					else
					{
						String LPath = path.substring(path.lastIndexOf(',') + 1);
						// if(path.indexOf("qAR")>=0 || path.indexOf("qAS")>=0 || path.indexOf("qAC")>=0){ //Via from Internet Server
						if (path.indexOf("qA") >= 0 || path.indexOf("TCPIP") >= 0)
						{
							html += "<td style=\"text-align: left;\">INET: " + LPath + "</td>";
						}
						else
						{
							if (path.indexOf("*") > 0)
								html += "<td style=\"text-align: left;\">DIGI: " + path + "</td>";
							else
								html += "<td style=\"text-align: left;\">RF: " + path + "</td>";
						}
						LPath.clear();
					}
					// html += "<td>" + path + "</td>";
					if (aprs.flags & F_HASPOS)
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
						double dtmp = aprsParse.direction(lon, lat, aprs.lng, aprs.lat);
						double dist = aprsParse.distance(lon, lat, aprs.lng, aprs.lat);
						html += "<td>" + String(dist, 1) + "km/" + String(dtmp, 0) + "°</td>";
					}
					else
					{
						html += "<td>-</td>\n";
					}
					html += "<td>" + String(packet) + "</td>\n";
					if (pkg.audio_level == 0)
					{
						html += "<td>-</td></tr>\n";
					}
					else
					{
						double Vrms = (double)pkg.audio_level / 1000;
						double audBV = 20.0F * log10(Vrms);
						if (audBV < -20.0F)
						{
							html += "<td style=\"color: #0000f0;\">";
						}
						else if (audBV > -5.0F)
						{
							html += "<td style=\"color: #f00000;\">";
						}
						else
						{
							html += "<td style=\"color: #008000;\">";
						}
						html += String(audBV, 1) + "dBV</td></tr>\n";
					}
				}
				path.clear();
				src_call.clear();
			}
			line.clear();
		}
	}
	html += "</table>\n";
	char *info = (char *)calloc(html.length(), sizeof(char));
	if (info)
	{
		//log_d("Send Event lastHeard");
		html.toCharArray(info, html.length(), 0);
		html.clear();
		lastheard_events.send(info, "lastHeard", millis(), 5000);
		free(info);
	}
	else
	{
		log_d("Memory is low!!");
	}
}

void handle_radio_get(AsyncWebServerRequest *request) {
    String html = loadHtmlTemplate("/radio.html");

    // Reemplazar valores dinámicos
    html.replace("%VOLUME%", String(config.volume));
    html.replace("%SQL_LEVEL%", String(config.sql_level));
    html.replace("%TX_FREQ%", String(config.freq_tx, 4));
    html.replace("%RX_FREQ%", String(config.freq_rx, 4));
    html.replace("%TX_CTCSS%", String(config.tone_tx));
    html.replace("%RX_CTCSS%", String(config.tone_rx));
    html.replace("%RF_TYPE%", String(config.rf_type));
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

void handle_vpn(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    if (request->method() == HTTP_POST) {
        if (request->hasArg("vpnEnable")) {
            config.vpn = (request->arg("vpnEnable") == "OK");
        }
        if (request->hasArg("wg_peer_address")) {
            strcpy(config.wg_peer_address, request->arg("wg_peer_address").c_str());
        }
        if (request->hasArg("wg_port")) {
            config.wg_port = request->arg("wg_port").toInt();
        }
        if (request->hasArg("wg_local_address")) {
            strcpy(config.wg_local_address, request->arg("wg_local_address").c_str());
        }
        if (request->hasArg("wg_netmask_address")) {
            strcpy(config.wg_netmask_address, request->arg("wg_netmask_address").c_str());
        }
        if (request->hasArg("wg_gw_address")) {
            strcpy(config.wg_gw_address, request->arg("wg_gw_address").c_str());
        }
        if (request->hasArg("wg_public_key")) {
            strcpy(config.wg_public_key, request->arg("wg_public_key").c_str());
        }
        if (request->hasArg("wg_private_key")) {
            strcpy(config.wg_private_key, request->arg("wg_private_key").c_str());
        }

        saveEEPROM(); // Guardar los cambios
        request->send(200, "text/html", "Configuración VPN guardada exitosamente.");
        return;
    }

    // Cargar el HTML desde SPIFFS
    String html = loadHtmlTemplate("/vpn.html");
    if (html.isEmpty()) {
        request->send(500, "text/plain", "Error al cargar el archivo HTML.");
        return;
    }

    // Reemplazar los marcadores con los valores actuales
    html.replace("%VPN_ENABLE%", config.vpn ? "checked" : "");
    html.replace("%WG_PEER_ADDRESS%", String(config.wg_peer_address));
    html.replace("%WG_PORT%", String(config.wg_port));
    html.replace("%WG_LOCAL_ADDRESS%", String(config.wg_local_address));
    html.replace("%WG_NETMASK%", String(config.wg_netmask_address));
    html.replace("%WG_GW%", String(config.wg_gw_address));
    html.replace("%WG_PUBLIC_KEY%", String(config.wg_public_key));
    html.replace("%WG_PRIVATE_KEY%", String(config.wg_private_key));

    // Enviar el HTML modificado al cliente
    request->send(200, "text/html", html);
}

void handle_mod(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}
	if (request->hasArg("commitGNSS"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					// if (isValidNumber(request->arg(i)))
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "atc")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.gnss_at_command, request->arg(i).c_str());
				}
				else
				{
					memset(config.gnss_at_command, 0, sizeof(config.gnss_at_command));
				}
			}

			if (request->argName(i) == "Host")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.gnss_tcp_host, request->arg(i).c_str());
				}
			}

			if (request->argName(i) == "Port")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.gnss_tcp_port = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "channel")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.gnss_channel = request->arg(i).toInt();
				}
			}
		}

		config.gnss_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitUART0"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "baudrate")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart0_baudrate = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "rx")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart0_rx_gpio = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "tx")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart0_tx_gpio = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "rts")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart0_rts_gpio = request->arg(i).toInt();
				}
			}
		}

		config.uart0_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitUART1"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "baudrate")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart1_baudrate = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "rx")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart1_rx_gpio = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "tx")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart1_tx_gpio = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "rts")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart1_rts_gpio = request->arg(i).toInt();
				}
			}
		}

		config.uart1_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitUART2"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "baudrate")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart2_baudrate = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "rx")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart2_rx_gpio = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "tx")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart2_tx_gpio = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "rts")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.uart2_rts_gpio = request->arg(i).toInt();
				}
			}
		}

		config.uart2_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitMODBUS"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					// if (isValidNumber(request->arg(i)))
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "channel")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.modbus_channel = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "address")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.modbus_address = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "de")
			{
				if (request->arg(i) != "")
				{
					config.modbus_de_gpio = request->arg(i).toInt();
				}
			}
		}

		config.modbus_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitTNC"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					// if (isValidNumber(request->arg(i)))
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "channel")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.ext_tnc_channel = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "mode")
			{
				if (isValidNumber(request->arg(i)))
				{
					config.ext_tnc_mode = request->arg(i).toInt();
				}
			}
		}

		config.ext_tnc_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitONEWIRE"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					// if (isValidNumber(request->arg(i)))
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "data")
			{
				if (request->arg(i) != "")
				{
					config.onewire_gpio = request->arg(i).toInt();
				}
			}
		}

		config.onewire_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitRF"))
	{
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "sql_active")
			{
				if (request->arg(i) != "")
				{
					config.rf_sql_active = (bool)request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "pd_active")
			{
				if (request->arg(i) != "")
				{
					config.rf_pd_active = (bool)request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "pwr_active")
			{
				if (request->arg(i) != "")
				{
					config.rf_pwr_active = (bool)request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "ptt_active")
			{
				if (request->arg(i) != "")
				{
					config.rf_ptt_active = (bool)request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "baudrate")
			{
				if (request->arg(i) != "")
				{
					config.rf_baudrate = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "rx")
			{
				if (request->arg(i) != "")
				{
					config.rf_rx_gpio = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "tx")
			{
				if (request->arg(i) != "")
				{
					config.rf_tx_gpio = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "pd")
			{
				if (request->arg(i) != "")
				{
					config.rf_pd_gpio = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "pwr")
			{
				if (request->arg(i) != "")
				{
					config.rf_pwr_gpio = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "ptt")
			{
				if (request->arg(i) != "")
				{
					config.rf_ptt_gpio = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "sql")
			{
				if (request->arg(i) != "")
				{
					config.rf_sql_gpio = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "atten")
			{
				if (request->arg(i) != "")
				{
					config.adc_atten = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "offset")
			{
				if (request->arg(i) != "")
				{
					config.adc_dc_offset = request->arg(i).toInt();
				}
			}
		}
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitI2C0"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "sda")
			{
				if (request->arg(i) != "")
				{
					config.i2c_sda_pin = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "sck")
			{
				if (request->arg(i) != "")
				{
					config.i2c_sck_pin = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "freq")
			{
				if (request->arg(i) != "")
				{
					config.i2c_freq = request->arg(i).toInt();
				}
			}
		}

		config.i2c_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitI2C1"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "sda")
			{
				if (request->arg(i) != "")
				{
					config.i2c1_sda_pin = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "sck")
			{
				if (request->arg(i) != "")
				{
					config.i2c1_sck_pin = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "freq")
			{
				if (request->arg(i) != "")
				{
					config.i2c1_freq = request->arg(i).toInt();
				}
			}
		}

		config.i2c1_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitCOUNTER0"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "gpio")
			{
				if (request->arg(i) != "")
				{
					config.counter0_gpio = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "active")
			{
				if (request->arg(i) != "")
				{
					config.counter0_active = (bool)request->arg(i).toInt();
				}
			}
		}

		config.counter0_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else if (request->hasArg("commitCOUNTER1"))
	{
		bool En = false;
		for (uint8_t i = 0; i < request->args(); i++)
		{
			// Serial.print("SERVER ARGS ");
			// Serial.print(request->argName(i));
			// Serial.print("=");
			// Serial.println(request->arg(i));

			if (request->argName(i) == "Enable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						En = true;
				}
			}

			if (request->argName(i) == "gpio")
			{
				if (request->arg(i) != "")
				{
					config.counter1_gpio = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "active")
			{
				if (request->arg(i) != "")
				{
					config.counter1_active = (bool)request->arg(i).toInt();
				}
			}
		}

		config.counter0_enable = En;
		saveEEPROM();
		String html = "OK";
		request->send(200, "text/html", html);
	}
	else
	{

		String html = "<script type=\"text/javascript\">\n";
		html += "$('form').submit(function (e) {\n";
		html += "e.preventDefault();\n";
		html += "var data = new FormData(e.currentTarget);\n";
		html += "if(e.currentTarget.id===\"formUART0\") document.getElementById(\"submitURAT0\").disabled=true;\n";
		html += "if(e.currentTarget.id===\"formUART1\") document.getElementById(\"submitURAT1\").disabled=true;\n";
		html += "if(e.currentTarget.id===\"formUART1\") document.getElementById(\"submitURAT1\").disabled=true;\n";
		html += "if(e.currentTarget.id===\"formGNSS\") document.getElementById(\"submitGNSS\").disabled=true;\n";
		html += "if(e.currentTarget.id===\"formMODBUS\") document.getElementById(\"submitMODBUS\").disabled=true;\n";
		html += "if(e.currentTarget.id===\"formTNC\") document.getElementById(\"submitTNC\").disabled=true;\n";
		// html += "if(e.currentTarget.id===\"formONEWIRE\") document.getElementById(\"submitONEWIRE\").disabled=true;\n";
		html += "if(e.currentTarget.id===\"formRF\") document.getElementById(\"submitRF\").disabled=true;\n";
		html += "if(e.currentTarget.id===\"formI2C0\") document.getElementById(\"submitI2C0\").disabled=true;\n";
		html += "if(e.currentTarget.id===\"formI2C1\") document.getElementById(\"submitI2C1\").disabled=true;\n";
		html += "if(e.currentTarget.id===\"formCOUNT0\") document.getElementById(\"submitCOUNT0\").disabled=true;\n";
		html += "if(e.currentTarget.id===\"formCOUNT1\") document.getElementById(\"submitCOUNT1\").disabled=true;\n";
		html += "$.ajax({\n";
		html += "url: '/mod',\n";
		html += "type: 'POST',\n";
		html += "data: data,\n";
		html += "contentType: false,\n";
		html += "processData: false,\n";
		html += "success: function (data) {\n";
		html += "alert(\"Submited Successfully\\nRequire hardware RESET!\");\n";
		html += "},\n";
		html += "error: function (data) {\n";
		html += "alert(\"An error occurred.\");\n";
		html += "}\n";
		html += "});\n";
		html += "});\n";
		html += "</script>\n";

		html += "<table style=\"text-align:unset;border-width:0px;background:unset\"><tr style=\"background:unset;vertical-align:top\"><td width=\"32%\" style=\"border:unset;\">";
		// html += "<h2>System Setting</h2>\n";
		/**************UART0(USB) Modify******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromUART0\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>UART0(USB) Modify</b></span></th>\n";
		html += "<tr>";

		String enFlage = "";
		if (config.uart0_enable)
			enFlage = "checked";
		html += "<td align=\"right\"><b>Enable</b></td>\n";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + enFlage + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>RX GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"rx\" type=\"number\" value=\"" + String(config.uart0_rx_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>TX GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"tx\" type=\"number\" value=\"" + String(config.uart0_tx_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>RTS/DE GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\"  name=\"rts\" type=\"number\" value=\"" + String(config.uart0_rts_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>Baudrate:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"baudrate\" id=\"baudrate\">\n";
		for (int i = 0; i < 13; i++)
		{
			if (config.uart0_baudrate == baudrate[i])
				html += "<option value=\"" + String(baudrate[i]) + "\" selected>" + String(baudrate[i]) + " </option>\n";
			else
				html += "<option value=\"" + String(baudrate[i]) + "\" >" + String(baudrate[i]) + " </option>\n";
		}
		html += "</select> bps\n";
		html += "</td>\n";
		html += "</tr>\n";
		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitUART0\" name=\"commitUART0\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitUART0\"/>\n";
		html += "</td></tr></table>\n";

		html += "</form><br />\n";
		html += "</td><td width=\"32%\" style=\"border:unset;\">";

		/**************UART1 Modify******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromUART1\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>UART1 Modify</b></span></th>\n";
		html += "<tr>";

		enFlage = "";
		if (config.uart1_enable)
			enFlage = "checked";
		html += "<td align=\"right\"><b>Enable</b></td>\n";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + enFlage + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>RX GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"rx\" type=\"number\" value=\"" + String(config.uart1_rx_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>TX GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"tx\" type=\"number\" value=\"" + String(config.uart1_tx_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>RTS/DE GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\"  name=\"rts\" type=\"number\" value=\"" + String(config.uart1_rts_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>Baudrate:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"baudrate\" id=\"baudrate\">\n";
		for (int i = 0; i < 13; i++)
		{
			if (config.uart1_baudrate == baudrate[i])
				html += "<option value=\"" + String(baudrate[i]) + "\" selected>" + String(baudrate[i]) + " </option>\n";
			else
				html += "<option value=\"" + String(baudrate[i]) + "\" >" + String(baudrate[i]) + " </option>\n";
		}
		html += "</select> bps\n";
		html += "</td>\n";
		html += "</tr>\n";
		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitUART1\" name=\"commitUART1\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitUART1\"/>\n";
		html += "</td></tr></table>\n";

		html += "</form><br />\n";
		html += "</td><td width=\"32%\" style=\"border:unset;\">";

		/**************UART2 Modify******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromUART2\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>UART2 Modify</b></span></th>\n";
		html += "<tr>";

		enFlage = "";
		if (config.uart2_enable)
			enFlage = "checked";
		html += "<td align=\"right\"><b>Enable</b></td>\n";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + enFlage + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>RX GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"rx\" type=\"number\" value=\"" + String(config.uart2_rx_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>TX GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"tx\" type=\"number\" value=\"" + String(config.uart2_tx_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>RTS/DE GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\"  name=\"rts\" type=\"number\" value=\"" + String(config.uart2_rts_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>Baudrate:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"baudrate\" id=\"baudrate\">\n";
		for (int i = 0; i < 13; i++)
		{
			if (config.uart2_baudrate == baudrate[i])
				html += "<option value=\"" + String(baudrate[i]) + "\" selected>" + String(baudrate[i]) + " </option>\n";
			else
				html += "<option value=\"" + String(baudrate[i]) + "\" >" + String(baudrate[i]) + " </option>\n";
		}
		html += "</select> bps\n";
		html += "</td>\n";
		html += "</tr>\n";
		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitUART2\" name=\"commitUART2\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitUART2\"/>\n";
		html += "</td></tr></table>\n";

		html += "</form><br />\n";
		html += "</td></tr></table>\n";

		// html += "</td><td width=\"32%\" style=\"border:unset;\">";

		/**************1-Wire Modify******************/
		// html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromONEWIRE\" method=\"post\">\n";
		// html += "<table>\n";
		// html += "<th colspan=\"2\"><span><b>1-Wire Bus Modify</b></span></th>\n";
		// html += "<tr>";

		// syncFlage = "";
		// if (config.onewire_enable)
		// 	syncFlage = "checked";
		// html += "<td align=\"right\"><b>Enable</b></td>\n";
		// html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + syncFlage + "><span class=\"slider round\"></span></label></td>\n";
		// html += "</tr>\n";

		// html += "<tr>\n";
		// html += "<td align=\"right\"><b>GPIO:</b></td>\n";
		// html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"data\" type=\"number\" value=\"" + String(config.onewire_gpio) + "\" /></td>\n";
		// html += "</tr>\n";

		// html += "<tr><td colspan=\"2\" align=\"right\">\n";
		// html += "<input class=\"btn btn-primary\" id=\"submitONEWIRE\" name=\"commitONEWIRE\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		// html += "<input type=\"hidden\" name=\"commitONEWIRE\"/>\n";
		// html += "</td></tr></table>\n";
		// html += "</form><br />\n";

		// html += "</td></tr></table>\n";

		html += "<table style=\"text-align:unset;border-width:0px;background:unset\"><tr style=\"background:unset;vertical-align:top\"><td width=\"50%\" style=\"border:unset;vertical-align:top\">";
		/**************RF GPIO******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromRF\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>RF GPIO Modify</b></span></th>\n";
		html += "<tr>";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>Baudrate:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"baudrate\" id=\"baudrate\">\n";
		for (int i = 0; i < 13; i++)
		{
			if (config.rf_baudrate == baudrate[i])
				html += "<option value=\"" + String(baudrate[i]) + "\" selected>" + String(baudrate[i]) + " </option>\n";
			else
				html += "<option value=\"" + String(baudrate[i]) + "\" >" + String(baudrate[i]) + " </option>\n";
		}
		html += "</select> bps\n";
		html += "</td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>ADC Attenuation:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"atten\" id=\"atten\">\n";
		for (int i = 0; i < 4; i++)
		{
			if (config.adc_atten == i)
				html += "<option value=\"" + String(i) + "\" selected>" + String(ADC_ATTEN[i]) + " </option>\n";
			else
				html += "<option value=\"" + String(i) + "\" >" + String(ADC_ATTEN[i]) + " </option>\n";
		}
		html += "</select>\n";
		html += "</td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>ADC DC OFFSET:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"100\" max=\"2500\" name=\"offset\" type=\"number\" value=\"" + String(config.adc_dc_offset) + "\" /> mV     (Current: " + String(offset) + " mV)</td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>RX GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"rx\" type=\"number\" value=\"" + String(config.rf_rx_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>TX GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"tx\" type=\"number\" value=\"" + String(config.rf_tx_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		String LowFlag = "", HighFlag = "";
		LowFlag = "";
		HighFlag = "";
		if (config.rf_pd_active)
			HighFlag = "checked=\"checked\"";
		else
			LowFlag = "checked=\"checked\"";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>PD GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\"  name=\"pd\" type=\"number\" value=\"" + String(config.rf_pd_gpio) + "\" /> Active:<input type=\"radio\" name=\"pd_active\" value=\"0\" " + LowFlag + "/>LOW <input type=\"radio\" name=\"pd_active\" value=\"1\" " + HighFlag + "/>HIGH </td>\n";
		html += "</tr>\n";

		LowFlag = "";
		HighFlag = "";
		if (config.rf_pwr_active)
			HighFlag = "checked=\"checked\"";
		else
			LowFlag = "checked=\"checked\"";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>H/L GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\"  name=\"pwr\" type=\"number\" value=\"" + String(config.rf_pwr_gpio) + "\" /> Active:<input type=\"radio\" name=\"pwr_active\" value=\"0\" " + LowFlag + "/>LOW <input type=\"radio\" name=\"pwr_active\" value=\"1\" " + HighFlag + "/>HIGH </td>\n";
		html += "</tr>\n";

		LowFlag = "";
		HighFlag = "";
		if (config.rf_sql_active)
			HighFlag = "checked=\"checked\"";
		else
			LowFlag = "checked=\"checked\"";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>SQL GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\"  name=\"sql\" type=\"number\" value=\"" + String(config.rf_sql_gpio) + "\" /> Active:<input type=\"radio\" name=\"sql_active\" value=\"0\" " + LowFlag + "/>LOW <input type=\"radio\" name=\"sql_active\" value=\"1\" " + HighFlag + "/>HIGH </td>\n";
		html += "</tr>\n";

		LowFlag = "";
		HighFlag = "";
		if (config.rf_ptt_active)
			HighFlag = "checked=\"checked\"";
		else
			LowFlag = "checked=\"checked\"";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>PTT GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\"  name=\"ptt\" type=\"number\" value=\"" + String(config.rf_ptt_gpio) + "\" /> Active:<input type=\"radio\" name=\"ptt_active\" value=\"0\" " + LowFlag + "/>LOW <input type=\"radio\" name=\"ptt_active\" value=\"1\" " + HighFlag + "/>HIGH </td>\n";
		html += "</tr>\n";

		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitRF\" name=\"commitRF\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitRF\"/>\n";
		html += "</td></tr></table>\n";
		html += "</form>\n";

		html += "</td><td width=\"23%\" style=\"border:unset;\">";

		/**************I2C_0 Modify******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromI2C0\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>I2C_0(OLED) Modify</b></span></th>\n";
		html += "<tr>";

		String syncFlage = "";
		if (config.i2c_enable)
			syncFlage = "checked";
		html += "<td align=\"right\"><b>Enable</b></td>\n";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + syncFlage + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>SDA GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"sda\" type=\"number\" value=\"" + String(config.i2c_sda_pin) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>SCK GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"sck\" type=\"number\" value=\"" + String(config.i2c_sck_pin) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>Frequency:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"1000\" max=\"800000\" name=\"freq\" type=\"number\" value=\"" + String(config.i2c_freq) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitI2C0\" name=\"commitI2C0\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitI2C0\"/>\n";
		html += "</td></tr></table>\n";
		html += "</form>\n";

		/**************Counter_0 Modify******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromCOUNTER0\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>Counter_0 Modify</b></span></th>\n";
		html += "<tr>";

		syncFlage = "";
		if (config.counter0_enable)
			syncFlage = "checked";
		html += "<td align=\"right\"><b>Enable</b></td>\n";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + syncFlage + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>INPUT GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"gpio\" type=\"number\" value=\"" + String(config.counter0_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		LowFlag = "";
		HighFlag = "";
		if (config.counter0_active)
			HighFlag = "checked=\"checked\"";
		else
			LowFlag = "checked=\"checked\"";
		html += "<tr>\n";
		html += "<td align=\"right\">Active</td>\n";
		html += "<td style=\"text-align: left;\"><input type=\"radio\" name=\"active\" value=\"0\" " + LowFlag + "/>LOW <input type=\"radio\" name=\"active\" value=\"1\" " + HighFlag + "/>HIGH </td>\n";
		html += "</tr>\n";

		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitCOUNTER0\" name=\"commitCOUNTER0\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitCOUNTER0\"/>\n";
		html += "</td></tr></table>\n";
		html += "</form>\n";

		html += "</td><td width=\"23%\" style=\"border:unset;\">";
		/**************I2C_1 Modify******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromI2C1\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>I2C_1 Modify</b></span></th>\n";
		html += "<tr>";

		syncFlage = "";
		if (config.i2c1_enable)
			syncFlage = "checked";
		html += "<td align=\"right\"><b>Enable</b></td>\n";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + syncFlage + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>SDA GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"sda\" type=\"number\" value=\"" + String(config.i2c1_sda_pin) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>SCK GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"sck\" type=\"number\" value=\"" + String(config.i2c1_sck_pin) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>Frequency:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"1000\" max=\"800000\" name=\"freq\" type=\"number\" value=\"" + String(config.i2c1_freq) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitI2C1\" name=\"commitI2C1\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitI2C1\"/>\n";
		html += "</td></tr></table>\n";
		html += "</form>\n";

		/**************Counter_1 Modify******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromCOUNTER1\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>Counter_1 Modify</b></span></th>\n";
		html += "<tr>";

		syncFlage = "";
		if (config.counter1_enable)
			syncFlage = "checked";
		html += "<td align=\"right\"><b>Enable</b></td>\n";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + syncFlage + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>INPUT GPIO:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"gpio\" type=\"number\" value=\"" + String(config.counter1_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		LowFlag = "";
		HighFlag = "";
		if (config.counter1_active)
			HighFlag = "checked=\"checked\"";
		else
			LowFlag = "checked=\"checked\"";
		html += "<tr>\n";
		html += "<td align=\"right\">Active</td>\n";
		html += "<td style=\"text-align: left;\"><input type=\"radio\" name=\"active\" value=\"0\" " + LowFlag + "/>LOW <input type=\"radio\" name=\"active\" value=\"1\" " + HighFlag + "/>HIGH </td>\n";
		html += "</tr>\n";

		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitCOUNTER1\" name=\"commitCOUNTER1\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitCOUNTER1\"/>\n";
		html += "</td></tr></table>\n";
		html += "</form>\n";

		html += "</td></tr></table>\n";

		//******************
		html += "<table style=\"text-align:unset;border-width:0px;background:unset\"><tr style=\"background:unset;vertical-align:top\"><td width=\"50%\" style=\"border:unset;vertical-align:top\">";
		/**************GNSS Modify******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromGNSS\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>GNSS Modify</b></span></th>\n";
		html += "<tr>";

		enFlage = "";
		if (config.gnss_enable)
			enFlage = "checked";
		html += "<td align=\"right\"><b>Enable</b></td>\n";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + enFlage + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>PORT:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"channel\" id=\"channel\">\n";
		for (int i = 0; i < 5; i++)
		{
			if (config.gnss_channel == i)
				html += "<option value=\"" + String(i) + "\" selected>" + String(GNSS_PORT[i]) + " </option>\n";
			else
				html += "<option value=\"" + String(i) + "\" >" + String(GNSS_PORT[i]) + " </option>\n";
		}
		html += "</select>\n";
		html += "</td>\n";
		html += "</tr>\n";

		html += "<td align=\"right\"><b>AT Command:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input maxlength=\"30\" size=\"20\" id=\"atc\" name=\"atc\" type=\"text\" value=\"" + String(config.gnss_at_command) + "\" /></td>\n";
		html += "</tr>\n";
		html += "<td align=\"right\"><b>TCP Host:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input maxlength=\"20\" size=\"15\" id=\"Host\" name=\"Host\" type=\"text\" value=\"" + String(config.gnss_tcp_host) + "\" /></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>TCP Port:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"1024\" max=\"65535\"  id=\"Port\" name=\"Port\" type=\"number\" value=\"" + String(config.gnss_tcp_port) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitGNSS\" name=\"commitGNSS\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitGNSS\"/>\n";
		html += "</td></tr></table>\n";

		html += "</form><br />\n";

		html += "</td><td width=\"23%\" style=\"border:unset;\">";

		/**************MODBUS Modify******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromMODBUS\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>MODBUS Modify</b></span></th>\n";
		html += "<tr>";

		enFlage = "";
		if (config.modbus_enable)
			enFlage = "checked";
		html += "<td align=\"right\"><b>Enable</b></td>\n";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + enFlage + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>PORT:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"channel\" id=\"channel\">\n";
		for (int i = 0; i < 5; i++)
		{
			if (config.modbus_channel == i)
				html += "<option value=\"" + String(i) + "\" selected>" + String(GNSS_PORT[i]) + " </option>\n";
			else
				html += "<option value=\"" + String(i) + "\" >" + String(GNSS_PORT[i]) + " </option>\n";
		}
		html += "</select>\n";
		html += "</td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>Address:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"address\" type=\"number\" value=\"" + String(config.modbus_address) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>DE:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input min=\"-1\" max=\"39\" name=\"de\" type=\"number\" value=\"" + String(config.modbus_de_gpio) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitMODBUS\" name=\"commitMODBUS\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitMODBUS\"/>\n";
		html += "</td></tr></table>\n";
		html += "</form>\n";

		html += "</td><td width=\"23%\" style=\"border:unset;\">";

		/**************External TNC Modify******************/
		html += "<form accept-charset=\"UTF-8\" action=\"#\" class=\"form-horizontal\" id=\"fromTNC\" method=\"post\">\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>External TNC Modify</b></span></th>\n";
		html += "<tr>";

		enFlage = "";
		if (config.ext_tnc_enable)
			enFlage = "checked";
		html += "<td align=\"right\"><b>Enable</b></td>\n";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + enFlage + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>PORT:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"channel\" id=\"channel\">\n";
		for (int i = 0; i < 4; i++)
		{
			if (config.ext_tnc_channel == i)
				html += "<option value=\"" + String(i) + "\" selected>" + String(TNC_PORT[i]) + " </option>\n";
			else
				html += "<option value=\"" + String(i) + "\" >" + String(TNC_PORT[i]) + " </option>\n";
		}
		html += "</select>\n";
		html += "</td>\n";
		html += "</tr>\n";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>MODE:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"mode\" id=\"mode\">\n";
		for (int i = 0; i < 4; i++)
		{
			if (config.ext_tnc_mode == i)
				html += "<option value=\"" + String(i) + "\" selected>" + String(TNC_MODE[i]) + " </option>\n";
			else
				html += "<option value=\"" + String(i) + "\" >" + String(TNC_MODE[i]) + " </option>\n";
		}
		html += "</select>\n";
		html += "</td>\n";
		html += "</tr>\n";

		html += "<tr><td colspan=\"2\" align=\"right\">\n";
		html += "<input class=\"btn btn-primary\" id=\"submitTNC\" name=\"commitTNC\" type=\"submit\" value=\"Apply\" maxlength=\"80\"/>\n";
		html += "<input type=\"hidden\" name=\"commitTNC\"/>\n";
		html += "</td></tr></table>\n";
		html += "</form>\n";

		html += "</td></tr></table>\n";

		request->send(200, "text/html", html); // send to someones browser when asked
	}
}

// Manejar solicitud GET para /system
void handle_system_get(AsyncWebServerRequest *request) {
    if (!request->authenticate(config.http_username, config.http_password)) {
        return request->requestAuthentication();
    }

    String html = loadHtmlTemplate("/system.html");

    // Reemplazo de valores dinámicos
    html.replace("%WEB_USER%", String(config.http_username));
    html.replace("%WEB_PASSWORD%", String(config.http_password));

	for (int i = 1; i <= 4; i++) {
		char argName[10];
		snprintf(argName, sizeof(argName), "Path_%d", i);
		if (request->hasArg(argName)) {
			strncpy(config.path[i - 1], request->arg(argName).c_str(), sizeof(config.path[i - 1]));
		}
	}

    html.replace("%OLED_ENABLE%", config.oled_enable ? "checked" : "");
    html.replace("%TX_DISPLAY%", config.tx_display ? "checked" : "");
    html.replace("%RX_DISPLAY%", config.rx_display ? "checked" : "");
    html.replace("%HEAD_UP%", config.h_up ? "checked" : "");
    html.replace("%POPUP_DELAY%", String(config.dispDelay));
    html.replace("%RX_CHANNEL_INET%", String(config.dispINET));
	html.replace("%RX_CHANNEL_RF%", String(config.dispRF));
    html.replace("%FILTER_DX%", String(config.filterDistant));
    html.replace("%FILTERS%", String(config.dispFilter));

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

void handle_igate_get(AsyncWebServerRequest *request) {
// if (!request->authenticate(config.http_username, config.http_password)) {
//     return request->requestAuthentication();
// }
    // Cargar y enviar el HTML
    String html = loadHtmlTemplate("/igate.html");

    if (html.isEmpty()) {
        request->send(404, "text/plain", "Archivo HTML vacío o no encontrado");
        return;
    }

    // Reemplazar valores dinámicos
    html.replace("%IGATE_ENABLE%", config.igate_en ? "checked" : "");
    html.replace("%MY_CALL%", String(config.aprs_mycall));
    html.replace("%MY_SSID%", String(config.aprs_ssid));
    html.replace("%IGATE_TABLE%", String(config.igate_symbol[0]));
    html.replace("%IGATE_SYMBOL%", String(config.igate_symbol[1]));
	html.replace("%IGATE_OBJECT%", (config.igate_object && config.igate_object[0] != '\0') ? String(config.igate_object) : "");
    html.replace("%APRS_HOST%", String(config.aprs_host));
    html.replace("%APRS_PORT%", String(config.aprs_port));
    html.replace("%APRS_FILTER%", String(config.aprs_filter));
    html.replace("%IGATE_COMMENT%", String(config.igate_comment));
    html.replace("%RF2INET%", config.rf2inet ? "checked" : "");
    html.replace("%INET2RF%", config.inet2rf ? "checked" : "");
    html.replace("%IGATE_BCN%", config.igate_bcn ? "checked" : "");
    html.replace("%IGATE_LAT%", String(config.igate_lat, 5));
    html.replace("%IGATE_LON%", String(config.igate_lon, 5));
    html.replace("%IGATE_ALT%", String(config.igate_alt, 2));

    Serial.println("HTML cargado y procesado correctamente.");
    request->send(200, "text/html", html);
}

void handle_igate_post(AsyncWebServerRequest *request) {
// if (!request->authenticate(config.http_username, config.http_password)) {
//     return request->requestAuthentication();
// }

    if (request->hasArg("igateEnable")) {
        config.igate_en = request->arg("igateEnable") == "OK";
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

    if (request->hasArg("aprsHost")) {
        strncpy(config.aprs_host, request->arg("aprsHost").c_str(), sizeof(config.aprs_host));
    }

    if (request->hasArg("aprsPort")) {
        config.aprs_port = request->arg("aprsPort").toInt();
    }

    if (request->hasArg("aprsFilter")) {
        strncpy(config.aprs_filter, request->arg("aprsFilter").c_str(), sizeof(config.aprs_filter));
    }

    if (request->hasArg("igateComment")) {
        strncpy(config.igate_comment, request->arg("igateComment").c_str(), sizeof(config.igate_comment));
    }

    if (request->hasArg("rf2inetEnable")) {
        config.rf2inet = true;
    } else {
        config.rf2inet = false;
    }

    if (request->hasArg("inet2rfEnable")) {
        config.inet2rf = true;
    } else {
        config.inet2rf = false;
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

    saveEEPROM(); // Guardar los cambios
    request->redirect("/igate");
}

void handle_digi(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}
	bool digiEn = false;
	bool posGPS = false;
	bool bcnEN = false;
	bool pos2RF = false;
	bool pos2INET = false;
	bool timeStamp = false;

	if (request->hasArg("commitDIGI"))
	{
		config.digiFilter = 0;
		for (int i = 0; i < request->args(); i++)
		{
			if (request->argName(i) == "digiEnable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						digiEn = true;
				}
			}
			if (request->argName(i) == "myCall")
			{
				if (request->arg(i) != "")
				{
					String name = request->arg(i);
					name.trim();
					name.toUpperCase();
					strcpy(config.digi_mycall, name.c_str());
				}
			}
			if (request->argName(i) == "mySSID")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.digi_ssid = request->arg(i).toInt();
					if (config.digi_ssid > 15)
						config.digi_ssid = 3;
				}
			}
			if (request->argName(i) == "digiDelay")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.digi_delay = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "digiPosInv")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.digi_interval = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "digiPosLat")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.digi_lat = request->arg(i).toFloat();
				}
			}

			if (request->argName(i) == "digiPosLon")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.digi_lon = request->arg(i).toFloat();
				}
			}
			if (request->argName(i) == "digiPosAlt")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.digi_alt = request->arg(i).toFloat();
				}
			}
			if (request->argName(i) == "digiPosSel")
			{
				if (request->arg(i) != "")
				{
					if (request->arg(i).toInt() == 1)
						posGPS = true;
				}
			}

			if (request->argName(i) == "digiTable")
			{
				if (request->arg(i) != "")
				{
					config.digi_symbol[0] = request->arg(i).charAt(0);
				}
			}
			if (request->argName(i) == "digiSymbol")
			{
				if (request->arg(i) != "")
				{
					config.digi_symbol[1] = request->arg(i).charAt(0);
				}
			}
			if (request->argName(i) == "digiPath")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.digi_path = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "digiComment")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.digi_comment, request->arg(i).c_str());
				}
				else
				{
					memset(config.digi_comment, 0, sizeof(config.digi_comment));
				}
			}
			if (request->argName(i) == "texttouse")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.digi_phg, request->arg(i).c_str());
				}
			}
			if (request->argName(i) == "digiComment")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.digi_comment, request->arg(i).c_str());
				}
			}
			if (request->argName(i) == "digiPos2RF")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						pos2RF = true;
				}
			}
			if (request->argName(i) == "digiPos2INET")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						pos2INET = true;
				}
			}
			if (request->argName(i) == "digiBcnEnable")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						bcnEN = true;
				}
			}
			// Filter
			if (request->argName(i) == "FilterMessage")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						config.digiFilter |= FILTER_MESSAGE;
				}
			}

			if (request->argName(i) == "FilterTelemetry")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						config.digiFilter |= FILTER_TELEMETRY;
				}
			}

			if (request->argName(i) == "FilterStatus")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						config.digiFilter |= FILTER_STATUS;
				}
			}

			if (request->argName(i) == "FilterWeather")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						config.digiFilter |= FILTER_WX;
				}
			}

			if (request->argName(i) == "FilterObject")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						config.digiFilter |= FILTER_OBJECT;
				}
			}

			if (request->argName(i) == "FilterItem")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						config.digiFilter |= FILTER_ITEM;
				}
			}

			if (request->argName(i) == "FilterQuery")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						config.digiFilter |= FILTER_QUERY;
				}
			}
			if (request->argName(i) == "FilterBuoy")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						config.digiFilter |= FILTER_BUOY;
				}
			}
			if (request->argName(i) == "FilterPosition")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						config.digiFilter |= FILTER_POSITION;
				}
			}
			if (request->argName(i) == "digiTimeStamp")
			{
				if (request->arg(i) != "")
				{
					if (String(request->arg(i)) == "OK")
						timeStamp = true;
				}
			}
		}
		config.digi_en = digiEn;
		config.digi_gps = posGPS;
		config.digi_bcn = bcnEN;
		config.digi_loc2rf = pos2RF;
		config.digi_loc2inet = pos2INET;
		config.digi_timestamp = timeStamp;

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
		html += "document.getElementById(\"submitDIGI\").disabled=true;\n";
		html += "$.ajax({\n";
		html += "url: '/digi',\n";
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
		html += "function openWindowSymbol() {\n";
		html += "var i, l, options = [{\n";
		html += "value: 'first',\n";
		html += "text: 'First'\n";
		html += "}, {\n";
		html += "value: 'second',\n";
		html += "text: 'Second'\n";
		html += "}],\n";
		html += "newWindow = window.open(\"/symbol\", null, \"height=400,width=400,status=no,toolbar=no,menubar=no,titlebar=no,location=no\");\n";
		html += "}\n";

		html += "function setValue(symbol,table) {\n";
		html += "document.getElementById('digiSymbol').value = String.fromCharCode(symbol);\n";
		html += "if(table==1){\n document.getElementById('digiTable').value='/';\n";
		html += "}else if(table==2){\n document.getElementById('digiTable').value='\\\\';\n}\n";
		html += "document.getElementById('digiImgSymbol').src = \"http://aprs.dprns.com/symbols/icons/\"+symbol.toString()+'-'+table.toString()+'.png';\n";
		html += "\n}\n";
		html += "function calculatePHGR(){document.forms.formDIGI.texttouse.value=\"PHG\"+calcPower(document.forms.formDIGI.power.value)+calcHeight(document.forms.formDIGI.haat.value)+calcGain(document.forms.formDIGI.gain.value)+calcDirection(document.forms.formDIGI.direction.selectedIndex)}function Log2(e){return Math.log(e)/Math.log(2)}function calcPerHour(e){return e<10?e:String.fromCharCode(65+(e-10))}function calcHeight(e){return String.fromCharCode(48+Math.round(Log2(e/10),0))}function calcPower(e){if(e<1)return 0;if(e>=1&&e<4)return 1;if(e>=4&&e<9)return 2;if(e>=9&&e<16)return 3;if(e>=16&&e<25)return 4;if(e>=25&&e<36)return 5;if(e>=36&&e<49)return 6;if(e>=49&&e<64)return 7;if(e>=64&&e<81)return 8;if(e>=81)return 9}function calcDirection(e){if(e==\"0\")return\"0\";if(e==\"1\")return\"1\";if(e==\"2\")return\"2\";if(e==\"3\")return\"3\";if(e==\"4\")return\"4\";if(e==\"5\")return\"5\";if(e==\"6\")return\"6\";if(e==\"7\")return\"7\";if(e==\"8\")return\"8\"}function calcGain(e){return e>9?\"9\":e<0?\"0\":Math.round(e,0)}\n";
		html += "</script>\n";

		/************************ DIGI Mode **************************/
		html += "<form id='formDIGI' method=\"POST\" action='#' enctype='multipart/form-data'>\n";
		// html += "<h2>[DIGI] Digital Repeater Mode</h2>\n";
		html += "<table>\n";
		// html += "<tr>\n";
		// html += "<th width=\"200\"><span><b>Setting</b></span></th>\n";
		// html += "<th><span><b>Value</b></span></th>\n";
		// html += "</tr>\n";
		html += "<th colspan=\"2\"><span><b>[DIGI] Dital Repeater Mode</b></span></th>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Enable:</b></td>\n";
		String digiEnFlag = "";
		if (config.digi_en)
			digiEnFlag = "checked";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"digiEnable\" value=\"OK\" " + digiEnFlag + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Station Callsign:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input maxlength=\"7\" size=\"6\" id=\"myCall\" name=\"myCall\" type=\"text\" value=\"" + String(config.digi_mycall) + "\" /></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Station SSID:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"mySSID\" id=\"mySSID\">\n";
		for (uint8_t ssid = 0; ssid <= 15; ssid++)
		{
			if (config.digi_ssid == ssid)
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
		html += "<td align=\"right\"><b>Station Symbol:</b></td>\n";
		String table = "1";
		if (config.digi_symbol[0] == 47)
			table = "1";
		if (config.digi_symbol[0] == 92)
			table = "2";
		html += "<td style=\"text-align: left;\">Table:<input maxlength=\"1\" size=\"1\" id=\"digiTable\" name=\"digiTable\" type=\"text\" value=\"" + String(config.digi_symbol[0]) + "\" style=\"background-color: rgb(97, 239, 170);\" /> Symbol:<input maxlength=\"1\" size=\"1\" id=\"digiSymbol\" name=\"digiSymbol\" type=\"text\" value=\"" + String(config.digi_symbol[1]) + "\" style=\"background-color: rgb(97, 239, 170);\" /> <img border=\"1\" style=\"vertical-align: middle;\" id=\"digiImgSymbol\" onclick=\"openWindowSymbol();\" src=\"http://aprs.dprns.com/symbols/icons/" + String((int)config.digi_symbol[1]) + "-" + table + ".png\"> <i>*Click icon for select symbol</i></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>PATH:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"digiPath\" id=\"digiPath\">\n";
		for (uint8_t pthIdx = 0; pthIdx < PATH_LEN; pthIdx++)
		{
			if (config.digi_path == pthIdx)
			{
				html += "<option value=\"" + String(pthIdx) + "\" selected>" + String(PATH_NAME[pthIdx]) + "</option>\n";
			}
			else
			{
				html += "<option value=\"" + String(pthIdx) + "\">" + String(PATH_NAME[pthIdx]) + "</option>\n";
			}
		}
		html += "</select></td>\n";
		// html += "<td style=\"text-align: left;\"><input maxlength=\"72\" size=\"72\" id=\"digiPath\" name=\"digiPath\" type=\"text\" value=\"" + String(config.digi_path) + "\" /></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Text Comment:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input maxlength=\"50\" size=\"50\" id=\"digiComment\" name=\"digiComment\" type=\"text\" value=\"" + String(config.digi_comment) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr><td style=\"text-align: right;\"><b>Repeat Delay:</b></td><td style=\"text-align: left;\"><input min=\"0\" max=\"10000\" step=\"100\" id=\"digiDelay\" name=\"digiDelay\" type=\"number\" value=\"" + String(config.digi_delay) + "\" /> mSec. <i>*0 is auto,Other random of delay time</i></td></tr>";

		html += "<tr>\n";
		html += "<td align=\"right\"><b>Time Stamp:</b></td>\n";
		String timeStampFlag = "";
		if (config.digi_timestamp)
			timeStampFlag = "checked";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"digiTimeStamp\" value=\"OK\" " + timeStampFlag + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";

		html += "<tr><td align=\"right\"><b>POSITION:</b></td>\n";
		html += "<td align=\"center\">\n";
		html += "<table>";
		String digiBcnEnFlag = "";
		if (config.digi_bcn)
			digiBcnEnFlag = "checked";

		html += "<tr><td style=\"text-align: right;\">Beacon:</td><td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"digiBcnEnable\" value=\"OK\" " + digiBcnEnFlag + "><span class=\"slider round\"></span></label><label style=\"vertical-align: bottom;font-size: 8pt;\">  Interval:<input min=\"0\" max=\"3600\" step=\"1\" id=\"digiPosInv\" name=\"digiPosInv\" type=\"number\" value=\"" + String(config.digi_interval) + "\" />Sec.</label></td></tr>";
		String digiPosFixFlag = "";
		String digiPosGPSFlag = "";
		String digiPos2RFFlag = "";
		String digiPos2INETFlag = "";
		if (config.digi_gps)
			digiPosGPSFlag = "checked=\"checked\"";
		else
			digiPosFixFlag = "checked=\"checked\"";

		if (config.digi_loc2rf)
			digiPos2RFFlag = "checked";
		if (config.digi_loc2inet)
			digiPos2INETFlag = "checked";
		html += "<tr><td style=\"text-align: right;\">Location:</td><td style=\"text-align: left;\"><input type=\"radio\" name=\"digiPosSel\" value=\"0\" " + digiPosFixFlag + "/>Fix <input type=\"radio\" name=\"digiPosSel\" value=\"1\" " + digiPosGPSFlag + "/>GPS </td></tr>\n";
		html += "<tr><td style=\"text-align: right;\">TX Channel:</td><td style=\"text-align: left;\"><input type=\"checkbox\" name=\"digiPos2RF\" value=\"OK\" " + digiPos2RFFlag + "/>RF <input type=\"checkbox\" name=\"digiPos2INET\" value=\"OK\" " + digiPos2INETFlag + "/>Internet </td></tr>\n";
		html += "<tr><td style=\"text-align: right;\">Latitude:</td><td style=\"text-align: left;\"><input min=\"-90\" max=\"90\" step=\"0.00001\" id=\"digiPosLat\" name=\"digiPosLat\" type=\"number\" value=\"" + String(config.digi_lat, 5) + "\" />degrees (positive for North, negative for South)</td></tr>\n";
		html += "<tr><td style=\"text-align: right;\">Longitude:</td><td style=\"text-align: left;\"><input min=\"-180\" max=\"180\" step=\"0.00001\" id=\"digiPosLon\" name=\"digiPosLon\" type=\"number\" value=\"" + String(config.digi_lon, 5) + "\" />degrees (positive for East, negative for West)</td></tr>\n";
		html += "<tr><td style=\"text-align: right;\">Altitude:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"10000\" step=\"0.1\" id=\"digiPosAlt\" name=\"digiPosAlt\" type=\"number\" value=\"" + String(config.digi_alt, 2) + "\" /> meter. *Value 0 is not send height</td></tr>\n";
		html += "</table></td>";
		html += "</tr>\n";
		delay(1);
		html += "<tr>\n";
		html += "<td align=\"right\"><b>PHG:</b></td>\n";
		html += "<td align=\"center\">\n";
		html += "<table>";
		html += "<tr>\n";
		html += "<td align=\"right\">Radio TX Power</td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"power\" id=\"power\">\n";
		html += "<option value=\"1\" selected>1</option>\n";
		html += "<option value=\"5\">5</option>\n";
		html += "<option value=\"10\">10</option>\n";
		html += "<option value=\"15\">15</option>\n";
		html += "<option value=\"25\">25</option>\n";
		html += "<option value=\"35\">35</option>\n";
		html += "<option value=\"50\">50</option>\n";
		html += "<option value=\"65\">65</option>\n";
		html += "<option value=\"80\">80</option>\n";
		html += "</select> Watts</td>\n";
		html += "</tr>\n";
		html += "<tr><td style=\"text-align: right;\">Antenna Gain</td><td style=\"text-align: left;\"><input size=\"3\" min=\"0\" max=\"100\" step=\"0.1\" id=\"gain\" name=\"gain\" type=\"number\" value=\"6\" /> dBi</td></tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\">Height</td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"haat\" id=\"haat\">\n";
		int k = 10;
		for (uint8_t w = 0; w < 10; w++)
		{
			if (w == 0)
			{
				html += "<option value=\"" + String(k) + "\" selected>" + String(k) + "</option>\n";
			}
			else
			{
				html += "<option value=\"" + String(k) + "\">" + String(k) + "</option>\n";
			}
			k += k;
		}
		html += "</select> Feet</td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\">Antenna/Direction</td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"direction\" id=\"direction\">\n";
		html += "<option>Omni</option><option>NE</option><option>E</option><option>SE</option><option>S</option><option>SW</option><option>W</option><option>NW</option><option>N</option>\n";
		html += "</select></td>\n";
		html += "</tr>\n";
		html += "<tr><td align=\"right\"><b>PHG Text</b></td><td align=\"left\"><input name=\"texttouse\" type=\"text\" size=\"6\" style=\"background-color: rgb(97, 239, 170);\" value=\"" + String(config.digi_phg) + "\"/> <input type=\"button\" value=\"Calculate PHG\" onclick=\"javascript:calculatePHGR()\" /></td></tr>\n";

		html += "</table></tr>";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Filter:</b></td>\n";

		html += "<td align=\"center\">\n";
		html += "<fieldset id=\"FilterGrp\">\n";
		html += "<legend>Filter repeater</legend>\n<table style=\"text-align:unset;border-width:0px;background:unset\">";
		html += "<tr style=\"background:unset;\">";

		String filterFlageEn = "";
		if (config.digiFilter & FILTER_MESSAGE)
			filterFlageEn = "checked";
		html += "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"FilterMessage\" type=\"checkbox\" value=\"OK\" " + filterFlageEn + "/>Message</td>\n";

		filterFlageEn = "";
		if (config.digiFilter & FILTER_STATUS)
			filterFlageEn = "checked";
		html += "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"FilterStatus\" type=\"checkbox\" value=\"OK\" " + filterFlageEn + "/>Status</td>\n";

		filterFlageEn = "";
		if (config.digiFilter & FILTER_TELEMETRY)
			filterFlageEn = "checked";
		html += "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"FilterTelemetry\" type=\"checkbox\" value=\"OK\" " + filterFlageEn + "/>Telemetry</td>\n";

		filterFlageEn = "";
		if (config.digiFilter & FILTER_WX)
			filterFlageEn = "checked";
		html += "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"FilterWeather\" type=\"checkbox\" value=\"OK\" " + filterFlageEn + "/>Weather</td>\n";

		filterFlageEn = "";
		if (config.digiFilter & FILTER_OBJECT)
			filterFlageEn = "checked";
		html += "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"FilterObject\" type=\"checkbox\" value=\"OK\" " + filterFlageEn + "/>Object</td>\n";

		filterFlageEn = "";
		if (config.digiFilter & FILTER_ITEM)
			filterFlageEn = "checked";
		html += "</tr><tr style=\"background:unset;\"><td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"FilterItem\" type=\"checkbox\" value=\"OK\" " + filterFlageEn + "/>Item</td>\n";

		filterFlageEn = "";
		if (config.digiFilter & FILTER_QUERY)
			filterFlageEn = "checked";
		html += "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"FilterQuery\" type=\"checkbox\" value=\"OK\" " + filterFlageEn + "/>Query</td>\n";

		filterFlageEn = "";
		if (config.digiFilter & FILTER_BUOY)
			filterFlageEn = "checked";
		html += "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"FilterBuoy\" type=\"checkbox\" value=\"OK\" " + filterFlageEn + "/>Buoy</td>\n";

		filterFlageEn = "";
		if (config.digiFilter & FILTER_POSITION)
			filterFlageEn = "checked";
		html += "<td style=\"border:unset;\"><input class=\"field_checkbox\" name=\"FilterPosition\" type=\"checkbox\" value=\"OK\" " + filterFlageEn + "/>Position</td>\n";

		html += "<td style=\"border:unset;\"></td>";
		html += "</tr></table></fieldset>\n";
		html += "</td></tr>\n";
		html += "</table><br />\n";
		html += "<div><button type='submit' id='submitDIGI'  name=\"commitDIGI\"> Apply Change </button></div>\n";
		html += "<input type=\"hidden\" name=\"commitDIGI\"/>\n";
		html += "</form><br />";
		request->send(200, "text/html", html); // send to someones browser when asked
	}
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

void handle_tlm(AsyncWebServerRequest *request)
{
	if (!request->authenticate(config.http_username, config.http_password))
	{
		return request->requestAuthentication();
	}
	bool En = false;
	bool pos2RF = false;
	bool pos2INET = false;
	String arg = "";

	if (request->hasArg("commitTLM"))
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
			if (request->argName(i) == "myCall")
			{
				if (request->arg(i) != "")
				{
					String name = request->arg(i);
					name.trim();
					name.toUpperCase();
					strcpy(config.tlm0_mycall, name.c_str());
				}
			}
			if (request->argName(i) == "mySSID")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.tlm0_ssid = request->arg(i).toInt();
					if (config.tlm0_ssid > 15)
						config.tlm0_ssid = 3;
				}
			}
			if (request->argName(i) == "infoInv")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.tlm0_info_interval = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "dataInv")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.tlm0_data_interval = request->arg(i).toInt();
				}
			}

			if (request->argName(i) == "Path")
			{
				if (request->arg(i) != "")
				{
					if (isValidNumber(request->arg(i)))
						config.tlm0_path = request->arg(i).toInt();
				}
			}
			if (request->argName(i) == "Comment")
			{
				if (request->arg(i) != "")
				{
					strcpy(config.tlm0_comment, request->arg(i).c_str());
				}
				else
				{
					memset(config.tlm0_comment, 0, sizeof(config.tlm0_comment));
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
			for (int x = 0; x < 13; x++)
			{
				arg = "sensorCH" + String(x);
				if (request->argName(i) == arg)
				{
					if (isValidNumber(request->arg(i)))
						config.tml0_data_channel[x] = request->arg(i).toInt();
				}
				arg = "param" + String(x);
				if (request->argName(i) == arg)
				{
					if (request->arg(i) != "")
					{
						strcpy(config.tlm0_PARM[x], request->arg(i).c_str());
					}
				}
				arg = "unit" + String(x);
				if (request->argName(i) == arg)
				{
					if (request->arg(i) != "")
					{
						strcpy(config.tlm0_UNIT[x], request->arg(i).c_str());
					}
				}
				if (x < 5)
				{
					for (int y = 0; y < 3; y++)
					{
						arg = "eqns" + String(x) + String((char)(y + 'a'));
						if (request->argName(i) == arg)
						{
							if (isValidNumber(request->arg(i)))
								config.tlm0_EQNS[x][y] = request->arg(i).toFloat();
						}
					}
				}
			}
			uint8_t b = 1;
			for (int x = 0; x < 8; x++)
			{
				arg = "bitact" + String(x);
				if (request->argName(i) == arg)
				{
					if (isValidNumber(request->arg(i)))
					{
						if (request->arg(i).toInt() == 1)
						{
							config.tlm0_BITS_Active |= b;
						}
						else
						{
							config.tlm0_BITS_Active &= ~b;
						}
					}
				}
				b <<= 1;
			}
		}
		config.tlm0_en = En;
		config.tlm0_2rf = pos2RF;
		config.tlm0_2inet = pos2INET;

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
		html += "document.getElementById(\"submitTLM\").disabled=true;\n";
		html += "$.ajax({\n";
		html += "url: '/tlm',\n";
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

		/************************ TLM Mode **************************/
		html += "<form id='formTLM' method=\"POST\" action='#' enctype='multipart/form-data'>\n";
		html += "<table>\n";
		html += "<th colspan=\"2\"><span><b>System Telemetry</b></span></th>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Enable:</b></td>\n";
		String EnFlag = "";
		if (config.tlm0_en)
			EnFlag = "checked";
		html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"Enable\" value=\"OK\" " + EnFlag + "><span class=\"slider round\"></span></label></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Station Callsign:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input maxlength=\"7\" size=\"6\" id=\"myCall\" name=\"myCall\" type=\"text\" value=\"" + String(config.tlm0_mycall) + "\" /></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Station SSID:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"mySSID\" id=\"mySSID\">\n";
		for (uint8_t ssid = 0; ssid <= 15; ssid++)
		{
			if (config.tlm0_ssid == ssid)
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
		html += "<td align=\"right\"><b>PATH:</b></td>\n";
		html += "<td style=\"text-align: left;\">\n";
		html += "<select name=\"Path\" id=\"Path\">\n";
		for (uint8_t pthIdx = 0; pthIdx < PATH_LEN; pthIdx++)
		{
			if (config.tlm0_path == pthIdx)
			{
				html += "<option value=\"" + String(pthIdx) + "\" selected>" + String(PATH_NAME[pthIdx]) + "</option>\n";
			}
			else
			{
				html += "<option value=\"" + String(pthIdx) + "\">" + String(PATH_NAME[pthIdx]) + "</option>\n";
			}
		}
		html += "</select></td>\n";
		// html += "<td style=\"text-align: left;\"><input maxlength=\"72\" size=\"72\" name=\"Path\" type=\"text\" value=\"" + String(config.tlm0_path) + "\" /></td>\n";
		html += "</tr>\n";
		html += "<tr>\n";
		html += "<td align=\"right\"><b>Text Comment:</b></td>\n";
		html += "<td style=\"text-align: left;\"><input maxlength=\"50\" size=\"50\" name=\"Comment\" type=\"text\" value=\"" + String(config.tlm0_comment) + "\" /></td>\n";
		html += "</tr>\n";

		html += "<tr><td style=\"text-align: right;\">Info Interval:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"3600\" step=\"1\" name=\"infoInv\" type=\"number\" value=\"" + String(config.tlm0_info_interval) + "\" />Sec.</td></tr>";
		html += "<tr><td style=\"text-align: right;\">Data Interval:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"3600\" step=\"1\" name=\"dataInv\" type=\"number\" value=\"" + String(config.tlm0_data_interval) + "\" />Sec.</td></tr>";

		String Pos2RFFlag = "";
		String Pos2INETFlag = "";
		if (config.tlm0_2rf)
			Pos2RFFlag = "checked";
		if (config.tlm0_2inet)
			Pos2INETFlag = "checked";
		html += "<tr><td style=\"text-align: right;\">TX Channel:</td><td style=\"text-align: left;\"><input type=\"checkbox\" name=\"Pos2RF\" value=\"OK\" " + Pos2RFFlag + "/>RF <input type=\"checkbox\" name=\"Pos2INET\" value=\"OK\" " + Pos2INETFlag + "/>Internet </td></tr>\n";

		// html += "<tr>\n";
		// html += "<td align=\"right\"><b>Time Stamp:</b></td>\n";
		// String timeStampFlag = "";
		// if (config.wx_timestamp)
		// 	timeStampFlag = "checked";
		// html += "<td style=\"text-align: left;\"><label class=\"switch\"><input type=\"checkbox\" name=\"wxTimeStamp\" value=\"OK\" " + timeStampFlag + "><span class=\"slider round\"></span></label></td>\n";
		// html += "</tr>\n";
		for (int ax = 0; ax < 5; ax++)
		{
			html += "<tr><td align=\"right\"><b>Channel A" + String(ax + 1) + ":</b></td>\n";
			html += "<td align=\"center\">\n";
			html += "<table>";

			// html += "<tr><td style=\"text-align: right;\">Name:</td><td style=\"text-align: center;\"><i>Sensor Type</i></td><td style=\"text-align: center;\"><i>Parameter</i></td><td style=\"text-align: center;\"><i>Unit</i></td></tr>\n";

			html += "<tr><td style=\"text-align: right;\">Type/Name:</td>\n";
			html += "<td style=\"text-align: left;\">Sensor Type: ";
			html += "<select name=\"sensorCH" + String(ax) + "\" id=\"sensorCH" + String(ax) + "\">\n";
			for (uint8_t idx = 0; idx < SYSTEM_LEN; idx++)
			{
				if (config.tml0_data_channel[ax] == idx)
				{
					html += "<option value=\"" + String(idx) + "\" selected>" + String(SYSTEM_NAME[idx]) + "</option>\n";
				}
				else
				{
					html += "<option value=\"" + String(idx) + "\">" + String(SYSTEM_NAME[idx]) + "</option>\n";
				}
			}
			html += "</select></td>\n";

			html += "<td style=\"text-align: left;\">Parameter: <input maxlength=\"10\" size=\"8\" name=\"param" + String(ax) + "\" type=\"text\" value=\"" + String(config.tlm0_PARM[ax]) + "\" /></td>\n";
			html += "<td style=\"text-align: left;\">Unit: <input maxlength=\"8\" size=\"5\" name=\"unit" + String(ax) + "\" type=\"text\" value=\"" + String(config.tlm0_UNIT[ax]) + "\" /></td></tr>\n";
			html += "<tr><td style=\"text-align: right;\">EQNS:</td><td colspan=\"3\" style=\"text-align: left;\">a:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax) + "a\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][0], 3) + "\" />  b:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax) + "b\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][1], 3) + "\" /> c:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax) + "c\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][2], 3) + "\" /> (av^2+bv+c)</td></tr>\n";
			html += "</table></td>";
			html += "</tr>\n";
		}

		uint8_t b = 1;
		for (int ax = 0; ax < 8; ax++)
		{
			html += "<tr><td align=\"right\"><b>Channel B" + String(ax + 1) + ":</b></td>\n";
			html += "<td align=\"center\">\n";
			html += "<table>";

			// html += "<tr><td style=\"text-align: right;\">Type/Name:</td>\n";
			html += "<td style=\"text-align: left;\">Type: ";
			html += "<select name=\"sensorCH" + String(ax + 5) + "\" id=\"sensorCH" + String(ax) + "\">\n";
			for (uint8_t idx = 0; idx < SYSTEM_BIT_LEN; idx++)
			{
				if (config.tml0_data_channel[ax + 5] == idx)
				{
					html += "<option value=\"" + String(idx) + "\" selected>" + String(SYSTEM_BITS_NAME[idx]) + "</option>\n";
				}
				else
				{
					html += "<option value=\"" + String(idx) + "\">" + String(SYSTEM_BITS_NAME[idx]) + "</option>\n";
				}
			}
			html += "</select></td>\n";

			html += "<td style=\"text-align: left;\">Parameter: <input maxlength=\"10\" size=\"8\" name=\"param" + String(ax + 5) + "\" type=\"text\" value=\"" + String(config.tlm0_PARM[ax + 5]) + "\" /></td>\n";
			html += "<td style=\"text-align: left;\">Unit: <input maxlength=\"8\" size=\"5\" name=\"unit" + String(ax + 5) + "\" type=\"text\" value=\"" + String(config.tlm0_UNIT[ax + 5]) + "\" /></td>\n";
			String LowFlag = "", HighFlag = "";
			if (config.tlm0_BITS_Active & b)
				HighFlag = "checked=\"checked\"";
			else
				LowFlag = "checked=\"checked\"";
			html += "<td style=\"text-align: left;\"> Active:<input type=\"radio\" name=\"bitact" + String(ax) + "\" value=\"0\" " + LowFlag + "/>LOW <input type=\"radio\" name=\"bitact" + String(ax) + "\" value=\"1\" " + HighFlag + "/>HIGH </td>\n";
			html += "</tr>\n";
			// html += "<tr><td style=\"text-align: right;\">EQNS:</td><td colspan=\"3\" style=\"text-align: left;\">a:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax + 1) + "a\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][0], 3) + "\" />  b:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax + 1) + "b\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][1], 3) + "\" /> c:<input min=\"-999\" max=\"999\" step=\"0.1\" name=\"eqns" + String(ax + 1) + "c\" type=\"number\" value=\"" + String(config.tlm0_EQNS[ax][2], 3) + "\" /> (av^2+bv+c)</td></tr>\n";
			html += "</table></td>";
			html += "</tr>\n";
			b <<= 1;
		}

		// html += "<tr><td align=\"right\"><b>Parameter Name:</b></td>\n";
		// html += "<td align=\"center\">\n";
		// html += "<table>";

		// // html += "<tr><td style=\"text-align: right;\">Latitude:</td><td style=\"text-align: left;\"><input min=\"-90\" max=\"90\" step=\"0.00001\" name=\"PosLat\" type=\"number\" value=\"" + String(config.wx_lat, 5) + "\" />degrees (positive for North, negative for South)</td></tr>\n";
		// // html += "<tr><td style=\"text-align: right;\">Longitude:</td><td style=\"text-align: left;\"><input min=\"-180\" max=\"180\" step=\"0.00001\" name=\"PosLon\" type=\"number\" value=\"" + String(config.wx_lon, 5) + "\" />degrees (positive for East, negative for West)</td></tr>\n";
		// // html += "<tr><td style=\"text-align: right;\">Altitude:</td><td style=\"text-align: left;\"><input min=\"0\" max=\"10000\" step=\"0.1\" name=\"PosAlt\" type=\"number\" value=\"" + String(config.wx_alt, 2) + "\" /> meter. *Value 0 is not send height</td></tr>\n";
		// html += "</table></td>";
		// html += "</tr>\n";

		html += "</table><br />\n";
		html += "<div><button type='submit' id='submitTLM'  name=\"commitTLM\"> Apply Change </button></div>\n";
		html += "<input type=\"hidden\" name=\"commitTLM\"/>\n";
		html += "</form><br />";
		request->send(200, "text/html", html); // send to someones browser when asked
	}
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
        if (request->hasArg("wificlient") && request->arg("wificlient") == "OK") {
            config.wifi_mode |= WIFI_STA_FIX;
        }

        for (int i = 0; i < 5; i++) {
            config.wifi_sta[i].enable = false;
        }

        for (uint8_t i = 0; i < request->args(); i++) {
            for (int n = 0; n < 5; n++) {
                String enableKey = "wifiStation" + String(n);
                if (request->argName(i) == enableKey && request->arg(i) == "OK") {
                    config.wifi_sta[n].enable = true;
                }
                String ssidKey = "wifi_ssid" + String(n);
                if (request->argName(i) == ssidKey) {
                    strncpy(config.wifi_sta[n].wifi_ssid, request->arg(i).c_str(), sizeof(config.wifi_sta[n].wifi_ssid));
                }
                String passKey = "wifi_pass" + String(n);
                if (request->argName(i) == passKey) {
                    strncpy(config.wifi_sta[n].wifi_pass, request->arg(i).c_str(), sizeof(config.wifi_sta[n].wifi_pass));
                }
            }
            if (request->argName(i) == "wifi_pwr" && isValidNumber(request->arg(i))) {
                config.wifi_power = (int8_t)request->arg(i).toInt();
            }
        }
    }

    saveEEPROM();
    request->redirect("/wireless");
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
					{ setMainPage(request); });
	async_server.on("/list-files", HTTP_GET, [](AsyncWebServerRequest *request) {
    handle_list_files(request);
	});				
	async_server.on("/symbol", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_symbol(request); });
	async_server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_logout(request); });
	async_server.on("/radio", HTTP_GET, [](AsyncWebServerRequest *request) 	
					{ handle_radio_get(request);});
	async_server.on("/radio", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_radio_post(request);});		
	async_server.on("/vpn", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handle_vpn(request);});
	async_server.on("/vpn", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_vpn(request);});
	async_server.on("/mod", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_mod(request); });
	async_server.on("/default", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_default(); });
	async_server.on("/igate", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handle_igate_get(request);});
	async_server.on("/igate", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_igate_post(request);});
	async_server.on("/digi", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_digi(request); });
	async_server.on("/tracker", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_tracker(request); });
	async_server.on("/wx", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_wx(request); });
	async_server.on("/tlm", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request)
					{ handle_tlm(request); });
	async_server.on("/system", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handle_system_get(request);});
	async_server.on("/system", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_system_post(request);});					
	async_server.on("/wireless", HTTP_GET, [](AsyncWebServerRequest *request) 
					{ handleWirelessGet(request);});
	async_server.on("/wireless", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handleWirelessPost(request);});
	async_server.on("/tnc2", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_tnc2(request); });
	async_server.on("/gnss", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_gnss(request); });
	async_server.on("/realtime", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_realtime(request); });
    async_server.on("/about", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_about_get(request); });
    async_server.on("/about", HTTP_POST, [](AsyncWebServerRequest *request) 
					{ handle_about_post(request); });
	async_server.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_dashboard(request); });
	async_server.on("/sidebarInfo", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_sidebar(request); });
	async_server.on("/sysinfo", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_sysinfo(request); });
	async_server.on("/lastHeard", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_lastHeard(request); });
	async_server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_css(request); });
	async_server.on("/jquery-3.7.1.js", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handle_jquery(request); });
	async_server.on("/modes-enabled", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleModesEnabled(request); });		
	async_server.on("/network-status", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleNetworkStatus(request); });	
	async_server.on("/statistics", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleStatistics(request); });
	async_server.on("/radio-info", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleRadioInfo(request); });			
	async_server.on("/aprs-server", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleAPRSServer(request); });
	async_server.on("/wifi-info", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleWiFiInfo(request); });		
	async_server.on("/debug-info", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleDebugData(request); });															
	async_server.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request)
					{ handleDebugPage(request); });	
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