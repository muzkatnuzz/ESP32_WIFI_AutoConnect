#include <Arduino.h>
#include "AutoConnect.h"

AsyncWebServer webServer(80);
DNSServer dnsServer;

// ==== Serve up one JPEG frame =============================================
void handleInfo(AsyncWebServerRequest *request)
{
  request->send(200, "plain/text", "hello world");
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;
  delay(200);
  Serial.print("\nStarting Async_AutoConnect_ESP32_minimal on " + String(ARDUINO_BOARD));

  ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, "Async_AutoConnect");

  //ESPAsync_wifiManager.resetSettings();   //reset saved settings
  ESPAsync_wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 132, 1), IPAddress(192, 168, 132, 1), IPAddress(255, 255, 255, 0));
  ESPAsync_wifiManager.autoConnect("AutoConnectAP");
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("Connected. Local IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println(ESPAsync_wifiManager.getStatus(WiFi.status()));
  }

  webServer.on("/info", HTTP_GET, handleInfo);

  
  webServer.begin(); // Web server start
}

void loop()
{
  // put your main code here, to run repeatedly:
}