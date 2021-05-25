#include <Arduino.h>
#include "AutoConnect.h"

AsyncWebServer webServer(80);
DNSServer dnsServer;

ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, "Async_AutoConnect");

// ==== Test if server is reachable =============================================
void handleTest(AsyncWebServerRequest *request)
{
  request->send(200, "plain/text", "Hello World");
}

void handleReset(AsyncWebServerRequest *request)
{
  request->send(200, "plain/text", "Handle reset. About to restart...");
  
  // reset wifi settings (remove credentials) 
  ESPAsync_wifiManager.resetSettings();   //reset saved settings
  
  ESP.restart();
  delay(2000);
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;
  delay(200);
  Serial.print("\nStarting Async_AutoConnect_ESP32_minimal on " + String(ARDUINO_BOARD));

  ESPAsync_wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 132, 1), IPAddress(192, 168, 132, 1), IPAddress(255, 255, 255, 0));
  ESPAsync_wifiManager.autoConnect("Babbaphone", "MamaMama");
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
  
  webServer.begin(); // Web server start
}

void loop()
{
  // put your main code here, to run repeatedly:
}