#include <Arduino.h>
#include "AutoConnect.h"

AsyncWebServer webServer(80);
DNSServer dnsServer;

ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, "Babbaphone");

// ==== Test if server is reachable =============================================
void handleTest(AsyncWebServerRequest *request)
{
  request->send(200, "plain/text", "Hello World");
}

void handleReset(AsyncWebServerRequest *request)
{
  request->send(200, "plain/text", "Handle reset. About to restart...");

  // reset wifi settings (remove credentials)
  ESPAsync_wifiManager.resetSettings(); //reset saved settings

  ESP.restart();
  delay(2000);
}

void handleNotFound(AsyncWebServerRequest *request)
{
  String message = "File Not Found\n\n";

  message += "URI: ";
  message += request->url();
  message += "\nMethod: ";
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += request->args();
  message += "\n";

  for (uint8_t i = 0; i < request->args(); i++)
  {
    message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
  }

  message += "\nHeaders: ";
  message += request->headers();
  message += "\n";

  for (uint8_t i = 0; i < request->headers(); i++)
  {
    message += " " + request->getHeader(i)->name() + ": " + request->getHeader(i)->value() + "\n";
  }

  AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", message);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");

#if USING_CORS_FEATURE
  // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
  response->addHeader("Access-Control-Allow-Origin", "*");
#endif

  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;
  delay(200);
  Serial.print("\nStarting AutoConnectAPI on " + String(ARDUINO_BOARD));

  ESPAsync_wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 251, 89), IPAddress(192, 168, 251, 89), IPAddress(255, 255, 255, 0));
  ESPAsync_wifiManager.autoConnect("Babbaphone", "babbaphone");
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("Connected. Local IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println(ESPAsync_wifiManager.getStatus(WiFi.status()));
  }

  webServer.on("/test", HTTP_GET, handleTest);
  webServer.on("/reset", HTTP_GET, handleReset);
  webServer.onNotFound(handleNotFound);

  webServer.begin(); // Web server start in case of STA wifi connection
}

void loop()
{
  // put your main code here, to run repeatedly:
}