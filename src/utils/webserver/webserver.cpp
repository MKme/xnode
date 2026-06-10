/****************************************************************************
 *   Tu May 22 21:23:51 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
 ****************************************************************************/
 
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "webserver.h"
#include "config.h"

#if defined( ENABLE_WEBSERVER )
    #ifdef NATIVE_64BIT
        void asyncwebserver_start(void){ return; };
        void asyncwebserver_end(void){ return; }
    #else
        #include <WiFi.h>
        #include <WiFiClient.h>
        #include <Update.h>
        #include <SPIFFS.h>
        #include <FS.h>
        #include <AsyncTCP.h>
        #include <ESPAsyncWebServer.h>
        #include <SPIFFSEditor.h>
        #include <ESP32SSDP.h>
        #if defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
            #include <TTGO.h>
        #endif

        AsyncWebServer asyncserver( WEBSERVERPORT );
        TaskHandle_t _WEBSERVER_Task;
        AsyncWebHandler mHandler_SPIFFSEditor;
        SPIFFSEditor * mSPIFFSEditor = nullptr;

    static const char* serverIndex = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>XNODE Firmware Update</title>
<style>
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#101820;color:#edf4f7}
main{max-width:720px;margin:0 auto;padding:32px 20px}
h1{font-size:28px;margin:0 0 8px}
p{color:#a9bac3}
form{margin-top:24px;padding:20px;border:1px solid #2f4652;background:#15242c}
input,button{font:inherit}
button{margin-top:16px;border:0;background:#2fc27d;color:#07130d;padding:10px 16px;font-weight:700;cursor:pointer}
#progressbarfull{background:#22323a;width:100%;height:18px;margin-top:18px}
#progressbar{background:#2fc27d;width:0;height:18px}
#prg{margin-top:14px;color:#cfe3ea}
a{color:#7fc7ff}
</style>
</head>
<body>
<main>
<h1>XNODE Firmware Update</h1>
<p>Upload a firmware binary. The watch will reboot after a successful update.</p>
<form method="POST" action="/update" enctype="multipart/form-data" id="upload_form">
<input type="file" name="update" required>
<br><button type="submit">Update Firmware</button>
<div id="prg">Progress: 0%</div>
<div id="progressbarfull"><div id="progressbar"></div></div>
</form>
<p><a href="/">Return to console</a></p>
</main>
<script>
document.getElementById('upload_form').addEventListener('submit',function(e){
  e.preventDefault();
  var request=new XMLHttpRequest();
  request.open('POST','/update');
  request.upload.addEventListener('progress',function(evt){
    if(evt.lengthComputable){
      var pct=Math.round((evt.loaded/evt.total)*100);
      document.getElementById('prg').textContent='Progress: '+pct+'%';
      document.getElementById('progressbar').style.width=pct+'%';
    }
  });
  request.onload=function(){document.getElementById('prg').textContent=request.status<400?'Progress: success':'Progress: error';};
  request.onerror=function(){document.getElementById('prg').textContent='Progress: error';};
  request.send(new FormData(e.target));
});
</script>
</body>
</html>
)rawliteral";

    static const char* meshtasticIndex = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Meshtastic XNODE</title>
<style>
:root{color-scheme:dark;--bg:#0f171b;--panel:#162229;--line:#2a3d47;--text:#eef6f8;--muted:#9cb0ba;--accent:#35c486;--warn:#ffcc66}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:var(--bg);color:var(--text)}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:18px 22px;border-bottom:1px solid var(--line);background:#111c22}
h1{margin:0;font-size:24px;letter-spacing:0}
.sub{color:var(--muted);font-size:13px}
nav{display:flex;flex-wrap:wrap;gap:8px;padding:14px 22px;border-bottom:1px solid var(--line)}
button,a.action{border:1px solid var(--line);background:var(--panel);color:var(--text);padding:10px 13px;text-decoration:none;cursor:pointer;font:inherit}
button:hover,a.action:hover{border-color:var(--accent)}
main{display:grid;grid-template-columns:minmax(0,1fr) 300px;gap:18px;padding:18px 22px}
section{border:1px solid var(--line);background:var(--panel);min-height:240px}
#panel{padding:16px;overflow:auto}
aside{display:grid;gap:18px;align-content:start}
.metric{padding:16px;border:1px solid var(--line);background:#101b21}
.metric h2{margin:0 0 10px;font-size:15px;color:#d8e7ec}
.metric div{color:var(--muted);font-size:14px;line-height:1.6}
.danger{color:var(--warn)}
@media(max-width:820px){main{grid-template-columns:1fr}header{align-items:flex-start;flex-direction:column}}
</style>
</head>
<body>
<header>
  <div>
    <h1>Meshtastic XNODE</h1>
    <div class="sub">T-Watch Ultra firmware console</div>
  </div>
  <div class="sub" id="clock"></div>
</header>
<nav>
  <button data-load="/info">Device</button>
  <button data-load="/memory">Memory</button>
  <button data-load="/network">Network</button>
  <a class="action" href="/edit" target="_blank">Files</a>
  <a class="action" href="/update">Firmware</a>
  <a class="action danger" href="/reset">Reboot</a>
</nav>
<main>
  <section id="panel">Loading device status...</section>
  <aside>
    <div class="metric"><h2>Endpoints</h2><div>/info<br>/memory<br>/network<br>/edit<br>/update</div></div>
    <div class="metric"><h2>Storage</h2><div>Ultra basemaps use /sd/osmmap when an SD card is mounted.</div></div>
    <div class="metric"><h2>Mesh</h2><div>Meshtastic BLE and XNODE services advertise over the watch BLE stack.</div></div>
  </aside>
</main>
<script>
function now(){document.getElementById('clock').textContent=new Date().toLocaleString();}
function extract(html){var doc=new DOMParser().parseFromString(html,'text/html');return doc.body?doc.body.innerHTML:html;}
async function load(path){
  var panel=document.getElementById('panel');
  panel.textContent='Loading '+path+'...';
  try{var res=await fetch(path,{cache:'no-store'});panel.innerHTML=extract(await res.text());}
  catch(e){panel.textContent='Unable to load '+path;}
}
document.querySelectorAll('[data-load]').forEach(function(btn){btn.addEventListener('click',function(){load(btn.dataset.load);});});
now();setInterval(now,1000);load('/info');
</script>
</body>
</html>
)rawliteral";

    void handleUpdate( AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index){
        /*
        * if filename includes spiffs, update the spiffs partition
        */
        int cmd = (filename.indexOf("spiffs") > 0) ? U_SPIFFS : U_FLASH;
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
        Update.printError(Serial);
        }
    }

    /*
    * Write Data an type message if fail
    */
    if (Update.write(data, len) != len) {
        Update.printError(Serial);
    }

    /*
    * After write Update restart
    */
    if (final) {
        AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Please wait while the switch reboots");
        response->addHeader("Refresh", "20");  
        response->addHeader("Location", "/");
        request->send(response);
        if (!Update.end(true)){
        Update.printError(Serial);
        } else {
        Serial.println("Update complete");
        Serial.flush();
        ESP.restart();
        }
    }
    }

    /*
    *
    */
    void asyncwebserver_start(void){
    asyncserver.on("/index.htm", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", meshtasticIndex);
    });

    asyncserver.on("/nav.htm", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Moved");
        response->addHeader("Location", "/index.htm");
        request->send(response);
    });

    asyncserver.on("/meshtastic", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", meshtasticIndex);
    });

    asyncserver.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        FlashMode_t mode = ESP.getFlashChipMode();
        uint32_t FreeSketchSpace = ESP.getFreeSketchSpace();
        uint32_t SketchFull = ESP.getSketchSize() + FreeSketchSpace;

        String html = (String) "<html><head><meta charset=\"utf-8\"></head><body><h3>Information</h3>" +
                    "<b><u>Memory</u></b><br>" +
                    "<b>Heap size: </b>" + ESP.getHeapSize() + "<br>" +
                    "<b>Heap free: </b>" + ESP.getFreeHeap() + "<br>" +
                    "<b>Heap free min: </b>" + ESP.getMinFreeHeap() + "<br>" +
                    "<b>Psram size: </b>" + ESP.getPsramSize() + "<br>" +
                    "<b>Psram free: </b>" + ESP.getFreePsram() + "<br>" +

    //                  "<br><b><u>System</u></b><br>" +
    //                  "\t<b>Battery voltage: </b>" + TTGOClass::getWatch()->power->getBattVoltage() / 1000 + " Volts" + "<br>" +

                    "\t<b>Uptime: </b>" + millis() / 1000 + "<br>" +
                    "<br><b><u>Chip</u></b>" +
                    "<br><b>SdkVersion: </b>" + String(ESP.getSdkVersion()) + "<br>" +
                    "<b>CpuFreq: </b>" + String(ESP.getCpuFreqMHz()) + " MHz<br>" +
                    
                    "<br><b><u>Flash</u></b><br>" +
                    "<b>FlashChipSpeed: </b>" + String(ESP.getFlashChipSpeed() / 1000000) + " MHz<br>" +
                    "<b>Flash mode: </b>" + String( mode == FM_QIO ? "QIO" : mode == FM_QOUT ? "QOUT" : mode == FM_DIO ? "DIO" : mode == FM_DOUT ? "DOUT" : "UNKNOWN") + "</b><br>" +
                    "<b>Flash sector size: </b>" + String( SPI_FLASH_SEC_SIZE) + "<br>" +
                    "<b>FlashChipMode: </b>" + mode + "<br>" +
                    "<b>FlashChipSize (SDK): </b>" + ESP.getFlashChipSize() + "<br>" +
                    
                    "<br><b><u>Firmware</u></b><br>" +
                    "<b>SketchSpace free: </b>" + FreeSketchSpace + " (" + (FreeSketchSpace / (SketchFull / 100)) + "%)<br>" +
                    "<b>BuildTime: </b>" +  __DATE__ + " " + __TIME__  + "<br>" +
                    "<b>Version: </b>" + __FIRMWARE__ + "<br>" +
                    "<b>GCC-Version: </b>" + __VERSION__ + "<br>" +
                    //"<b>SketchMD5: </b>" + String(ESP.getSketchMD5()) + "<br>" +
                    
                    //"<br><b><u>Filesystem</u></b><br>" +
                    //"<b>Total size: </b>" + SPIFFS.totalBytes() + "<br>" +
                    //"<b>Used size: </b>" + SPIFFS.usedBytes() + "<br>" +

                    "</body></html>";
        request->send(200, "text/html", html);
    });

    asyncserver.on("/memory", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = (String) "<html><head><meta charset=\"utf-8\"></head><body><h3>Memory Details</h3>" +
                    "<b>Heap size: </b>" + ESP.getHeapSize() + "<br>" +
                    "<b>Heap free: </b>" + ESP.getFreeHeap() + "<br>" +
                    "<b>Heap free min: </b>" + ESP.getMinFreeHeap() + "<br>" +
                    "<b>Psram size: </b>" + ESP.getPsramSize() + "<br>" +
                    "<b>Psram free: </b>" + ESP.getFreePsram() + "<br>" +

                    "<br><b><u>System</u></b><br>" +
                    "<b>Uptime: </b>" + millis() / 1000 + "<br>" +
                    "</body></html>";
        request->send(200, "text/html", html);
    });

    /*
    asyncserver.on("/battery", HTTP_GET, [](AsyncWebServerRequest *request) {
        TTGOClass * ttgo = TTGOClass::getWatch();

        String html = (String) "<html><head><meta charset=\"utf-8\"></head><body><h3>Battery Details</h3>" +
                    "<b>Battery voltage: </b>" + ttgo->power->getBattVoltage() / 1000 + " Volts" + "<br>" +
                    "<b>Coulomb Charge: </b>" + ttgo->power->getBattChargeCoulomb() + "<br>" +
                    "<b>Coulomb Discharge: </b>" + ttgo->power->getBattDischargeCoulomb() + "<br>" +
                    "<b>Current Charge: </b>" + ttgo->power->getBattChargeCurrent() + "<br>" +
                    "<b>Current Discharge: </b>" + ttgo->power->getBattDischargeCurrent() + "<br>" +
                    "<b>Fuel Gauge: </b>" + ttgo->power->getBattPercentage() + "%" + "<br>" +
                    "<b>Calculated: </b>" + ttgo->power->getCoulombData() + "mAh remaining" + "<br>" +

                    "<br><b><u>System</u></b><br>" +
                    "<b>Uptime: </b>" + millis() / 1000 + "<br>" +
                    "</body></html>";
        request->send(200, "text/html", html);
    });

    asyncserver.on("/touch", HTTP_GET, [](AsyncWebServerRequest *request) {
        TTGOClass * ttgo = TTGOClass::getWatch();

        String html;
        if ( touch_lock_take() ) {

            html = (String) "<html><head><meta charset=\"utf-8\"></head><body><h3>Touch Parameters</h3>" +
                        "<b>Device Mode: </b>" + ttgo->touch->getDeviceMode() + "<br>" +
                        "<b>Interrupt Mode: </b>" + ttgo->touch->getINTMode() + "<br>" +
                        "<b>Control: </b>" + ttgo->touch->getControl() + "<br>" +
                        "<b>Power Mode: </b>" + ttgo->touch->getPowerMode() + "<br>" +
                        "<b>Monitor time: </b>" + ttgo->touch->getMonitorTime() + "<br>" +
                        "<b>Active period: </b>" + ttgo->touch->getActivePeriod() + "<br>" +
                        "<b>Monitor period: </b>" + ttgo->touch->getMonitorPeriod() + "<br>" +

                        "<br><b><u>System</u></b><br>" +
                        "<b>Uptime: </b>" + millis() / 1000 + "<br>" +
                        "</body></html>";
            touch_lock_give();
        } else {
            html = (String) "<html><head><meta charset=\"utf-8\"></head><body>No Lock</body></html>";
        }
        request->send(200, "text/html", html);
    });

    asyncserver.on("/temp", HTTP_GET, [](AsyncWebServerRequest *request) {
        TTGOClass * ttgo = TTGOClass::getWatch();

        String html = (String) "<html><head><meta charset=\"utf-8\"></head><body><h3>Device Temperature</h3>" +
                    "<b>AXP202: </b>" + ttgo->power->getTemp() + "<br>" +
                    "<b>BMA423: </b>" + ttgo->bma->temperature() + "<br>" +

                    "<br><b><u>System</u></b><br>" +
                    "<b>Uptime: </b>" + millis() / 1000 + "<br>" +
                    "</body></html>";
        request->send(200, "text/html", html);
    });
    */
    asyncserver.on("/network", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = (String) "<html><head><meta charset=\"utf-8\"></head><body><h3>Network</h3>" +
                    "<b>IP Addr: </b>" + WiFi.localIP().toString() + "<br>" +
                    "<b>MAC: </b>" + WiFi.macAddress() + "<br>" +
                    "<b>SNMask: </b>" + WiFi.subnetMask().toString() + "<br>" +
                    "<b>GW IP: </b>" + WiFi.gatewayIP().toString() + "<br>" +
                    "<b>DNS 1: </b>" + WiFi.dnsIP(0).toString() + "<br>" +
                    "<b>DNS 2: </b>" + WiFi.dnsIP(1).toString() + "<br>" +
                    "<b>RSSI: </b>" + String(WiFi.RSSI()) + "dB<br>" +
                    "<b>Hostname: </b>" + WiFi.getHostname() + "<br>" +
                    "<b>SSID: </b>" + WiFi.SSID() + "<br>" +
                    "<br>Upnp Info: <a target=\"_blank\" href='/description.xml'>description.xml</a>" + "<br>" +
                    "</body></html>";
        request->send(200, "text/html", html);
    });
    /*
    asyncserver.on("/shot", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(200, "text/plain", "screen is taken\r\n" );
        screenshot_take();
        screenshot_save();
    });
    */
    //start FsEditor with SPIFFS
    setFsEditorFilesystem(SPIFFS);

    asyncserver.rewrite("/", "/index.htm");
    asyncserver.serveStatic("/", SPIFFS, "/");

    asyncserver.onNotFound([](AsyncWebServerRequest *request){
        Serial.printf( "NOT_FOUND: ");
        if(request->method() == HTTP_GET)
        Serial.printf( "GET");
        else if(request->method() == HTTP_POST)
        Serial.printf( "POST");
        else if(request->method() == HTTP_DELETE)
        Serial.printf( "DELETE");
        else if(request->method() == HTTP_PUT)
        Serial.printf( "PUT");
        else if(request->method() == HTTP_PATCH)
        Serial.printf( "PATCH");
        else if(request->method() == HTTP_HEAD)
        Serial.printf( "HEAD");
        else if(request->method() == HTTP_OPTIONS)
        Serial.printf( "OPTIONS");
        else
        Serial.printf( "UNKNOWN");
        Serial.printf( " http://%s%s\n", request->host().c_str(), request->url().c_str());

        if(request->contentLength()){
        Serial.printf( "_CONTENT_TYPE: %s\n", request->contentType().c_str());
        Serial.printf( "_CONTENT_LENGTH: %u\n", request->contentLength());
        }

        int headers = request->headers();
        int i;
        for(i=0;i<headers;i++){
        AsyncWebHeader* h = request->getHeader(i);
        Serial.printf( "_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
        }

        int params = request->params();
        for(i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isFile()){
            Serial.printf( "_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
        } else if(p->isPost()){
            Serial.printf( "_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        } else {
            Serial.printf( "_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
        }
        request->send(404);
    });

    asyncserver.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
        if(!index)
        Serial.printf( "UploadStart: %s\n", filename.c_str());
        Serial.printf("%s", (const char*)data);
        if(final)
        Serial.printf( "UploadEnd: %s (%u)\n", filename.c_str(), index+len);
    });

    asyncserver.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        if(!index) {
        Serial.printf( "BodyStart: %u\n", total);
        }
        Serial.printf( "%s", (const char*)data);
        if(index + len == total) {
        Serial.printf( "BodyEnd: %u\n", total);
        }
    });

    asyncserver.on("/reset", HTTP_GET, []( AsyncWebServerRequest * request ) {
        request->send(200, "text/plain", "Reset\r\n" );
        delay(3000);
        ESP.restart();    
    });

    asyncserver.on("/update", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(200, "text/html", serverIndex);
    });

    asyncserver.on(
        "/update", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) { handleUpdate(request, filename, index, data, len, final); }
    );

    asyncserver.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
        byte mac[6];
        WiFi.macAddress(mac);
        char tmp[6 + 1];
        snprintf(tmp, sizeof(tmp), "%02X%02X%02X", mac[3], mac[4], mac[5]);
        String MacStrPart = String(tmp);

        String xmltext = String("<?xml version=\"1.0\"?>\n") +
                "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
                "<specVersion>\n"
                "\t<major>1</major>\n"
                "\t<minor>0</minor>\n"
                "</specVersion>\n"
                "<URLBase>http://" + WiFi.localIP().toString() + "/</URLBase>\n" 
                "<device>\n"
                "\t<deviceType>upnp:rootdevice</deviceType>\n"

                /*this is the icon name in Windows*/
                /*"\t<friendlyName>" + WiFi.getHostname() + "</friendlyName>\n"*/ 
                "\t<friendlyName>" + DEV_NAME + " " + MacStrPart + "</friendlyName>\n" /*because the hostename is 'Espressif' */

                "\t<presentationURL>/</presentationURL>\n"
                "\t<manufacturer>" + "Dirk Broßwick (sharandac)" + "</manufacturer>\n"
                "\t<manufacturerURL>https://github.com/sharandac/My-TTGO-Watch</manufacturerURL>\n"
                "\t<modelName>" +  DEV_INFO + "</modelName>\n"

                "\t<modelNumber>" + WiFi.getHostname() + "</modelNumber>\n"
                "\t<modelURL>" +
                "/" + "</modelURL>\n"

                "\t<serialNumber>Build: " + __FIRMWARE__ + "</serialNumber>\n"
                //The last six bytes of the UUID are the hardware address of the first Ethernet adapter in the system the UUID was generated on.
                "\t<UDN>uuid:38323636-4558-4DDA-9188-CDA0E6" + MacStrPart + "</UDN>\n"
                "</device>\n"
                "</root>\r\n"
                "\r\n";

        request->send(200, "text/xml", xmltext);
    });

    //Upnp / SSDP presentation - Multicast  - link to description.xml
    SSDP.setSchemaURL("description.xml");
    SSDP.setHTTPPort( UPNPPORT );
    SSDP.setURL("/");
    SSDP.setDeviceType("upnp:rootdevice");
    SSDP.begin();

    asyncserver.begin();

    log_d("enable webserver and ssdp");
    }

    void asyncwebserver_end(void) {
        SSDP.end();
        asyncserver.end();
        log_d("disable webserver and ssdp");
    }

    void setFsEditorFilesystem(const fs::FS& fs) {
        log_d("asyncserver.removeHandler");
        asyncserver.removeHandler(&mHandler_SPIFFSEditor);
        if(mSPIFFSEditor!=nullptr)
            delete mSPIFFSEditor;  
        mSPIFFSEditor = new SPIFFSEditor(fs);
        log_d("asyncserver.addHandler");
        mHandler_SPIFFSEditor = asyncserver.addHandler(mSPIFFSEditor);
    }
    #endif
#endif // ENABLE_WEBSERVER
