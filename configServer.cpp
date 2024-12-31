#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "LittleFS.h"

#include "configServer.h"
#include "settings.h"

// soft AP
const char *AP_ssid_prefix = "JClock";
const char *AP_password = "jclock99";
IPAddress local_ip(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

#define CONFIG_BUTTON 6

bool wifiConnected = false;
bool configMode = false;
int wiperValue = 0;

WebServer webServer;

static void setConfigMode(bool mode);
static bool handleFileRead(String path);

void configServerStart(String ssid, String passwd)
{
  pinMode(CONFIG_BUTTON, INPUT_PULLUP);

  // start the wifi soft AP
  char AP_ssid[100];
  snprintf(AP_ssid, sizeof(ssid), "%s-%" PRIu64, AP_ssid_prefix, ESP.getEfuseMac());
  Serial.printf("Establishing '%s'\n", AP_ssid);
  WiFi.softAP(AP_ssid, AP_password);
  WiFi.softAPConfig(local_ip, gateway, subnet);
 
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  if (MDNS.begin("jclock")) {
    Serial.println("mDNS started");
  }
  
  webServer.on("/connect", []() {
    Serial.println("Got /connect request");
    String ssid, passwd;
    if (webServer.hasArg("ssid")) {
      ssid = webServer.arg("ssid");
    }
    if (webServer.hasArg("passwd")) {
      passwd = webServer.arg("passwd");
    }
    if (ssid.length() != 0) {
      Serial.printf("Connecting to '%s'\n", ssid.c_str());
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(ssid, passwd);
      setStringSetting("SSID", ssid.c_str());
      setStringSetting("Password", passwd.c_str());
      configMode = false;
    }
    else {
      Serial.println("SSID is empty");
    }
    webServer.send(200, "text/html", "");
  });

  webServer.on("/config-mode", []() {
    Serial.println("Got /config-mode request");
    webServer.send(200, "text/html", String(configMode ? 1 : 0));
  });

  webServer.on("/volume", []() {
    Serial.println("Got /volume request");
    webServer.send(200, "text/html", String(wiperValue));
  });

  webServer.on("/set-volume", HTTP_POST, []() {
    Serial.print("Got /set-volume ");
    Serial.print(wiperValue);
    Serial.println(" request");
    if (webServer.hasArg("volume")) {
      wiperValue = webServer.arg("volume").toInt();
    }
    webServer.send(200, "text/html", "");
  });

  webServer.onNotFound([&]() {
    Serial.print("file request: ");
    Serial.println(webServer.uri());
    if (!handleFileRead(webServer.uri())) {
      webServer.send(404);
    }
  });
  
  webServer.begin();

  // start connecting to the target wifi AP if the encoder button is pressed
  if (digitalRead(CONFIG_BUTTON) == 0) {
    Serial.println("Entering Configuration Mode");
    WiFi.mode(WIFI_AP);
    setConfigMode(true);
  }
  else {
    if (ssid.length() != 0) {
      Serial.print("Connecting to ");
      Serial.println(ssid);
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(ssid, passwd);
      setConfigMode(false);
    }
    else {
      Serial.println("No SSID set. Entering Configuration Mode");
      WiFi.mode(WIFI_AP);
      setConfigMode(true);
    }
  }
}

void configServerLoop()
{
  // check to see if wifi is still connected
  if (wifiConnected && WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
  }
    
  // handle wifi connection to the target
  if (!wifiConnected) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected to target");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      wifiConnected = true;
    }
  }
  
  webServer.handleClient();
}

static void setConfigMode(bool mode) 
{
  Serial.print("configMode ");
  Serial.println(mode);
  configMode = mode;
  //digitalWrite(LED, configMode ? 0 : 1);
}

//code from fsbrowser example, consolidated.
static bool handleFileRead(String path) 
{
  // default to index.html if nothing else was specified
  if (path.length() == 0) path = "/";
  if (path.endsWith("/")) path += "index.html";

  // dtermine content type
  String contentType;
  if (path.endsWith(".html")) contentType = "text/html";
  else if (path.endsWith(".css")) contentType = "text/css";
  else if (path.endsWith(".js")) contentType = "application/javascript";
  else if (path.endsWith(".png")) contentType = "image/png";
  else if (path.endsWith(".gif")) contentType = "image/gif";
  else if (path.endsWith(".jpg")) contentType = "image/jpeg";
  else if (path.endsWith(".ico")) contentType = "image/x-icon";
  else if (path.endsWith(".xml")) contentType = "text/xml";
  else if (path.endsWith(".pdf")) contentType = "application/x-pdf";
  else if (path.endsWith(".zip")) contentType = "application/x-zip";
  else if (path.endsWith(".gz")) contentType = "application/x-gzip";
  else if (path.endsWith(".json")) contentType = "application/json";
  else contentType = "text/plain";

  // split filepath and extension
  String prefix = path, ext = "";
  int lastPeriod = path.lastIndexOf('.');
  if (lastPeriod >= 0) {
    prefix = path.substring(0, lastPeriod);
    ext = path.substring(lastPeriod);
  }

  //look for smaller versions of file
  //minified file, good (myscript.min.js)
  if (LittleFS.exists(prefix + ".min" + ext)) path = prefix + ".min" + ext;
  //gzipped file, better (myscript.js.gz)
  if (LittleFS.exists(prefix + ext + ".gz")) path = prefix + ext + ".gz";
  //min and gzipped file, best (myscript.min.js.gz)
  if (LittleFS.exists(prefix + ".min" + ext + ".gz")) path = prefix + ".min" + ext + ".gz";

  // send the file if it exists
  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");
    if (webServer.hasArg("download"))
      webServer.sendHeader("Content-Disposition", " attachment;");
    if (webServer.uri().indexOf("nocache") < 0)
      webServer.sendHeader("Cache-Control", " max-age=172800");

    //optional alt arg (encoded url), server sends redirect to file on the web
    if (WiFi.status() == WL_CONNECTED && webServer.hasArg("alt")) {
      webServer.sendHeader("Location", webServer.arg("alt"), true);
      webServer.send ( 302, "text/plain", "");
    } else {
      //server sends file
      size_t sent = webServer.streamFile(file, contentType);
    }
    file.close();
    return true;
  }

  // file not found
  return false;
}

