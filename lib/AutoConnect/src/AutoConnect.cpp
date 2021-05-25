#include "AutoConnect.h"

#ifndef TIME_BETWEEN_MODAL_SCANS
  // Default to 30s
  #define TIME_BETWEEN_MODAL_SCANS          120000UL
#endif

#ifndef TIME_BETWEEN_MODELESS_SCANS
  // Default to 60s
  #define TIME_BETWEEN_MODELESS_SCANS       120000UL
#endif

#define DEFAULT_PORTAL_TIMEOUT  	60000L

// To permit autoConnect() to use STA static IP or DHCP IP.
#ifndef AUTOCONNECT_NO_INVALIDATE
  #define AUTOCONNECT_NO_INVALIDATE true
#endif

ESPAsync_WiFiManager::ESPAsync_WiFiManager(AsyncWebServer * webserver, DNSServer *dnsserver, const char *iHostname)
{
  server    = webserver;
  dnsServer = dnsserver;
  
  wifiSSIDs     = NULL;
  
  // KH
  wifiSSIDscan  = true;
  //wifiSSIDscan  = false;
  //////
  
  _modeless     = false;
  shouldscan    = true;
  
#if USE_DYNAMIC_PARAMS
  _max_params = WIFI_MANAGER_MAX_PARAMS;
  _params = (ESPAsync_WMParameter**)malloc(_max_params * sizeof(ESPAsync_WMParameter*));
#endif

  //WiFi not yet started here, must call WiFi.mode(WIFI_STA) and modify function WiFiGenericClass::mode(wifi_mode_t m) !!!

  WiFi.mode(WIFI_STA);

  if (iHostname[0] == 0)
  {
    String _hostname = "ESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    _hostname.toUpperCase();

    getRFC952_hostname(_hostname.c_str());
  }
  else
  {
    // Prepare and store the hostname only not NULL
    getRFC952_hostname(iHostname);
  }

  log_w("RFC925 Hostname = %s", RFC952_hostname);

  setHostname();

  networkIndices = NULL;
}

//////////////////////////////////////////

ESPAsync_WiFiManager::~ESPAsync_WiFiManager()
{
#if USE_DYNAMIC_PARAMS
  if (_params != NULL)
  {
    LOGINFO("freeing allocated params!");

    free(_params);
  }
#endif

  if (networkIndices)
  {
    free(networkIndices); //indices array no longer required so free memory
  }
}


//////////////////////////////////////////

char* ESPAsync_WiFiManager::getRFC952_hostname(const char* iHostname)
{
  memset(RFC952_hostname, 0, sizeof(RFC952_hostname));

  size_t len = (RFC952_HOSTNAME_MAXLEN < strlen(iHostname)) ? RFC952_HOSTNAME_MAXLEN : strlen(iHostname);

  size_t j = 0;

  for (size_t i = 0; i < len - 1; i++)
  {
    if (isalnum(iHostname[i]) || iHostname[i] == '-')
    {
      RFC952_hostname[j] = iHostname[i];
      j++;
    }
  }
  // no '-' as last char
  if (isalnum(iHostname[len - 1]) || (iHostname[len - 1] != '-'))
    RFC952_hostname[j] = iHostname[len - 1];

  return RFC952_hostname;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::setupConfigPortal()
{
  stopConfigPortal = false; //Signal not to close config portal

  /*This library assumes autoconnect is set to 1. It usually is
    but just in case check the setting and turn on autoconnect if it is off.
    Some useful discussion at https://github.com/esp8266/Arduino/issues/1615*/
  if (WiFi.getAutoConnect() == 0)
    WiFi.setAutoConnect(1);

#if !( USING_ESP32_S2 || USING_ESP32_C3 )
  #ifdef ESP8266
    // KH, mod for Async
    server->reset();
  #else		//ESP32
    server->reset();
  #endif

  if (!dnsServer)
    dnsServer = new DNSServer;
#endif    // ( USING_ESP32_S2 || USING_ESP32_C3 )

  // optional soft ip config
  // Must be put here before dns server start to take care of the non-default ConfigPortal AP IP.
  // Check (https://github.com/khoih-prog/ESP_WiFiManager/issues/58)
  if (_WiFi_AP_IPconfig._ap_static_ip)
  {
    log_w("Custom AP IP/GW/Subnet = %s, %s, %s", _WiFi_AP_IPconfig._ap_static_ip.toString().c_str(), _WiFi_AP_IPconfig._ap_static_gw.toString().c_str(), _WiFi_AP_IPconfig._ap_static_sn.toString().c_str());
    
    WiFi.softAPConfig(_WiFi_AP_IPconfig._ap_static_ip, _WiFi_AP_IPconfig._ap_static_gw, _WiFi_AP_IPconfig._ap_static_sn);
  }

  /* Setup the DNS server redirecting all the domains to the apIP */
  if (dnsServer)
  {
    dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    
    // DNSServer started with "*" domain name, all DNS requests will be passsed to WiFi.softAPIP()
    if (! dnsServer->start(DNS_PORT, "*", WiFi.softAPIP()))
    {
      // No socket available
      log_e("Can't start DNS Server. No available socket");
    }
  }
  
  _configPortalStart = millis();

  log_i("\nConfiguring AP SSID = %s", _apName);

  if (_apPassword != NULL)
  {
    if (strlen(_apPassword) < 8 || strlen(_apPassword) > 63)
    {
      // fail passphrase to short or long!
      log_e("Invalid AccessPoint password. Ignoring");

      _apPassword = NULL;
    }
    log_w("AP PWD = %s", _apPassword);
  }
  
  
  // KH, To enable dynamic/random channel
  static int channel;
  // Use random channel if  _WiFiAPChannel == 0
  if (_WiFiAPChannel == 0)
    channel = (_configPortalStart % MAX_WIFI_CHANNEL) + 1;
  else
    channel = _WiFiAPChannel;
  
  if (_apPassword != NULL)
  {
    log_w("AP Channel = %i", channel);
    
    //WiFi.softAP(_apName, _apPassword);//password option
    WiFi.softAP(_apName, _apPassword, channel);
  }
  else
  {
    // Can't use channel here
    WiFi.softAP(_apName);
  }
  //////
  
  delay(500); // Without delay I've seen the IP address blank
  
  log_i("AP IP address = %s", WiFi.softAPIP().toString().c_str());

  /* Setup web pages: root, wifi config pages, SO captive portal detectors and not found. */
  
  server->on("/",         std::bind(&ESPAsync_WiFiManager::handleRoot,        this, std::placeholders::_1)).setFilter(ON_AP_FILTER);
  server->on("/wifi",     std::bind(&ESPAsync_WiFiManager::handleWifi,        this, std::placeholders::_1)).setFilter(ON_AP_FILTER);
  server->on("/wifisave", std::bind(&ESPAsync_WiFiManager::handleWifiSave,    this, std::placeholders::_1)).setFilter(ON_AP_FILTER);
  server->on("/close",    std::bind(&ESPAsync_WiFiManager::handleServerClose, this, std::placeholders::_1)).setFilter(ON_AP_FILTER);
  server->on("/i",        std::bind(&ESPAsync_WiFiManager::handleInfo,        this, std::placeholders::_1)).setFilter(ON_AP_FILTER);
  server->on("/r",        std::bind(&ESPAsync_WiFiManager::handleReset,       this, std::placeholders::_1)).setFilter(ON_AP_FILTER);
  server->on("/state",    std::bind(&ESPAsync_WiFiManager::handleState,       this, std::placeholders::_1)).setFilter(ON_AP_FILTER);
  server->on("/scan",     std::bind(&ESPAsync_WiFiManager::handleScan,        this, std::placeholders::_1)).setFilter(ON_AP_FILTER);
  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  server->on("/fwlink",   std::bind(&ESPAsync_WiFiManager::handleRoot,        this, std::placeholders::_1)).setFilter(ON_AP_FILTER);  
  server->onNotFound (std::bind(&ESPAsync_WiFiManager::handleNotFound,        this, std::placeholders::_1));

  server->begin(); // Web server start
  
  log_i("HTTP server started");
}

//////////////////////////////////////////

bool ESPAsync_WiFiManager::autoConnect()
{
  String ssid = "ESP_" + String((uint32_t)ESP.getEfuseMac());

  return autoConnect(ssid.c_str(), NULL);
}

/* This is not very useful as there has been an assumption that device has to be
  told to connect but Wifi already does it's best to connect in background. Calling this
  method will block until WiFi connects. Sketch can avoid
  blocking call then use (WiFi.status()==WL_CONNECTED) test to see if connected yet.
  See some discussion at https://github.com/tzapu/WiFiManager/issues/68
*/


//////////////////////////////////////////

bool ESPAsync_WiFiManager::autoConnect(char const *apName, char const *apPassword)
{
#if AUTOCONNECT_NO_INVALIDATE
  log_i("\nAutoConnect using previously saved SSID/PW, but keep previous settings");
  // Connect to previously saved SSID/PW, but keep previous settings
  connectWifi();
#else
  log_i("\nAutoConnect using previously saved SSID/PW, but invalidate previous settings");
  // Connect to previously saved SSID/PW, but invalidate previous settings
  connectWifi(WiFi_SSID(), WiFi_Pass());  
#endif
 
  unsigned long startedAt = millis();

  while (millis() - startedAt < 10000)
  {
    //delay(100);
    delay(200);

    if (WiFi.status() == WL_CONNECTED)
    {
      float waited = (millis() - startedAt);
       
      log_i("Connected after waiting (s) : %f", waited / 1000);
      log_i("Local ip = %s", WiFi.localIP().toString().c_str());
      
      return true;
    }
  }

  return startConfigPortal(apName, apPassword);
}


///////////////////////////////////////////////////////////////////
// get networks separated by semicolon listed by name quality and encryption separated by comma

String ESPAsync_WiFiManager::networkListAsString()
{
  String pager;
  
  //display networks in page
  for (int i = 0; i < wifiSSIDCount; i++) 
  {
    if (wifiSSIDs[i].duplicate == true) 
      continue; // skip dups
      
    int quality = getRSSIasQuality(wifiSSIDs[i].RSSI);

    if (_minimumQuality == -1 || _minimumQuality < quality) 
    {
      String item = "";
      String rssiQ;
      
      rssiQ += quality;
      item += wifiSSIDs[i].SSID; 
      item += ",";
      item += rssiQ;
      item += ",";

      if (wifiSSIDs[i].encryptionType != WIFI_AUTH_OPEN)
      {
        item += "1";
      } 
      else 
      {
        item += "0";
      }

      item += ";";
      pager += item;

    } 
    else 
    {
      log_d("Skipping due to quality");
    }

  }
  
  return pager;
}

//////////////////////////////////////////

String ESPAsync_WiFiManager::scanModal()
{
  shouldscan = true;
  scan();
  
  String pager = networkListAsString();
  
  return pager;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::scan()
{
  if (!shouldscan) 
    return;
  
  log_d("About to scan");
  
  if (wifiSSIDscan)
  {
    delay(100);
  }

  if (wifiSSIDscan)
  {
    log_d("Start scan");
    wifi_ssid_count_t n = WiFi.scanNetworks();
    log_d("Scan done");
    
    if (n == WIFI_SCAN_FAILED) 
    {
      log_d("WIFI_SCAN_FAILED!");
    }
    else if (n == WIFI_SCAN_RUNNING) 
    {
      log_d("WIFI_SCAN_RUNNING!");
    } 
    else if (n < 0) 
    {
      log_d("Failed, unknown error code!");
    } 
    else if (n == 0) 
    {
      log_d("No network found");
      // page += F("No networks found. Refresh to scan again.");
    } 
    else 
    {
      if (wifiSSIDscan)
      {
        /* WE SHOULD MOVE THIS IN PLACE ATOMICALLY */
        if (wifiSSIDs) 
          delete [] wifiSSIDs;
          
        wifiSSIDs     = new WiFiResult[n];
        wifiSSIDCount = n;

        if (n > 0)
          shouldscan = false;

        for (wifi_ssid_count_t i = 0; i < n; i++)
        {
          wifiSSIDs[i].duplicate=false;

          WiFi.getNetworkInfo(i, wifiSSIDs[i].SSID, wifiSSIDs[i].encryptionType, wifiSSIDs[i].RSSI, wifiSSIDs[i].BSSID, wifiSSIDs[i].channel);
        }

        // RSSI SORT
        // old sort
        for (int i = 0; i < n; i++) 
        {
          for (int j = i + 1; j < n; j++) 
          {
            if (wifiSSIDs[j].RSSI > wifiSSIDs[i].RSSI) 
            {
              std::swap(wifiSSIDs[i], wifiSSIDs[j]);
            }
          }
        }

        // remove duplicates ( must be RSSI sorted )
        if (_removeDuplicateAPs) 
        {
        String cssid;
        
        for (int i = 0; i < n; i++) 
        {
          if (wifiSSIDs[i].duplicate == true) 
            continue;
            
          cssid = wifiSSIDs[i].SSID;
          
          for (int j = i + 1; j < n; j++) 
          {
            if (cssid == wifiSSIDs[j].SSID) 
            {
              log_d("DUP AP: %s", wifiSSIDs[j].SSID.c_str());
              // set dup aps to NULL
              wifiSSIDs[j].duplicate = true; 
            }
          }
        }
        }
      }
    }
  }
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::startConfigPortalModeless(char const *apName, char const *apPassword, bool shouldConnectWiFi) 
{
  _modeless     = true;
  _apName       = apName;
  _apPassword   = apPassword;

  WiFi.mode(WIFI_AP_STA);
  
  log_d("SET AP STA");

  // try to connect
  if (shouldConnectWiFi && connectWifi("", "") == WL_CONNECTED)   
  {
    log_d("IP Address: %s", WiFi.localIP());
       
 	  if ( _savecallback != NULL) 
	  {
	    //todo: check if any custom parameters actually exist, and check if they really changed maybe
	    _savecallback();
	  }
  }

  if ( _apcallback != NULL) 
  {
    _apcallback(this);
  }

  connect = false;
  setupConfigPortal();
  scannow = -1 ;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::loop()
{
	safeLoop();
	criticalLoop();
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::setInfo() 
{
  if (needInfo) 
  {
    pager       = infoAsString();
    wifiStatus  = WiFi.status();
    needInfo    = false;
  }
}

//////////////////////////////////////////

// Anything that accesses WiFi, ESP or EEPROM goes here

void ESPAsync_WiFiManager::criticalLoop()
{
  log_d("criticalLoop: Enter");
  
  if (_modeless)
  {
    if (scannow == -1 || millis() > scannow + TIME_BETWEEN_MODELESS_SCANS)
    {
      log_d("criticalLoop: modeless scan");
      
      scan();
      scannow = millis();
    }
    
    if (connect) 
    {
      connect = false;

      log_d("criticalLoop: Connecting to new AP");

      // using user-provided  _ssid, _pass in place of system-stored ssid and pass
      if (connectWifi(_ssid, _pass) != WL_CONNECTED) 
      {
        log_d("criticalLoop: Failed to connect.");
      } 
      else 
      {
        //connected
        // alanswx - should we have a config to decide if we should shut down AP?
        // WiFi.mode(WIFI_STA);
        //notify that configuration has changed and any optional parameters should be saved
        if ( _savecallback != NULL) 
        {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }

        return;
      }

      if (_shouldBreakAfterConfig) 
      {
        //flag set to exit after config after trying to connect
        //notify that configuration has changed and any optional parameters should be saved
        if ( _savecallback != NULL) 
        {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
      }
    }
  }
}

//////////////////////////////////////////

// Anything that doesn't access WiFi, ESP or EEPROM can go here

void ESPAsync_WiFiManager::safeLoop()
{
  #ifndef USE_EADNS	
  dnsServer->processNextRequest();
  #endif
}

///////////////////////////////////////////////////////////

bool  ESPAsync_WiFiManager::startConfigPortal()
{
  String ssid = "ESP_" + String((uint32_t)ESP.getEfuseMac());

  ssid.toUpperCase();

  return startConfigPortal(ssid.c_str(), NULL);
}

//////////////////////////////////////////

bool  ESPAsync_WiFiManager::startConfigPortal(char const *apName, char const *apPassword)
{
  WiFi.mode(WIFI_AP_STA);

  _apName = apName;
  _apPassword = apPassword;

  //notify we entered AP mode
  if (_apcallback != NULL)
  {
    log_i("_apcallback");
    
    _apcallback(this);
  }

  connect = false;

  setupConfigPortal();

  bool TimedOut = true;

  log_i("startConfigPortal : Enter loop");
  
  scannow = -1 ;

  while (_configPortalTimeout == 0 || millis() < _configPortalStart + _configPortalTimeout)
  {
#if ( USING_ESP32_S2 || USING_ESP32_C3 )   
    // Fix ESP32-S2 issue with WebServer (https://github.com/espressif/arduino-esp32/issues/4348)
    delay(1);
#else
    //DNS
    if (dnsServer)
      dnsServer->processNextRequest();    
    
    //
    //  we should do a scan every so often here and
    //  try to reconnect to AP while we are at it
    //
    if ( scannow == -1 || millis() > scannow + TIME_BETWEEN_MODAL_SCANS)
    {
      log_d("About to modal scan");
      
      // since we are modal, we can scan every time
      shouldscan = true;
      
      WiFi.disconnect(false);

      scan();
      
      //if (_tryConnectDuringConfigPortal) 
      //  WiFi.begin(); // try to reconnect to AP
        
      scannow = millis() ;
    }
#endif    // ( USING_ESP32_S2 || USING_ESP32_C3 )

    if (connect)
    {
      TimedOut = false;
      delay(2000);

      log_e("Connecting to new AP");

      // using user-provided  _ssid, _pass in place of system-stored ssid and pass
      if (connectWifi(_ssid, _pass) != WL_CONNECTED)
      {  
        log_e("Failed to connect");
    
        WiFi.mode(WIFI_AP); // Dual mode becomes flaky if not connected to a WiFi network.
      }
      else
      {
        //notify that configuration has changed and any optional parameters should be saved
        if (_savecallback != NULL)
        {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }
      
      if (_shouldBreakAfterConfig)
      {
        //flag set to exit after config after trying to connect
        //notify that configuration has changed and any optional parameters should be saved
        if (_savecallback != NULL)
        {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }
    }

    if (stopConfigPortal)
    {
      log_e("Stop ConfigPortal");
     
      stopConfigPortal = false;
      break;
    }
    
    yield();
    
#if ( defined(TIME_BETWEEN_CONFIG_PORTAL_LOOP) && (TIME_BETWEEN_CONFIG_PORTAL_LOOP > 0) )
    #warning Using delay in startConfigPortal loop
    delay(TIME_BETWEEN_CONFIG_PORTAL_LOOP);
#endif    
  }

  WiFi.mode(WIFI_STA);
  if (TimedOut)
  {
    setHostname();

    // New v1.0.8 to fix static IP when CP not entered or timed-out
    setWifiStaticIP();
    
    WiFi.begin();
    int connRes = waitForConnectResult();

    log_e("Timed out connection result: %s", getStatus(connRes));
  }

#if !( USING_ESP32_S2 || USING_ESP32_C3 )
  server->reset();
  dnsServer->stop();
#endif

  return  WiFi.status() == WL_CONNECTED;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::setWifiStaticIP()
{ 
#if USE_CONFIGURABLE_DNS
  if (_WiFi_STA_IPconfig._sta_static_ip)
  {
    LOGWARN(F("Custom STA IP/GW/Subnet"));
   
    //***** Added section for DNS config option *****
    if (_WiFi_STA_IPconfig._sta_static_dns1 && _WiFi_STA_IPconfig._sta_static_dns2) 
    { 
      LOGWARN(F("DNS1 and DNS2 set"));
 
      WiFi.config(_WiFi_STA_IPconfig._sta_static_ip, _WiFi_STA_IPconfig._sta_static_gw, _WiFi_STA_IPconfig._sta_static_sn, _WiFi_STA_IPconfig._sta_static_dns1, _WiFi_STA_IPconfig._sta_static_dns2);
    }
    else if (_WiFi_STA_IPconfig._sta_static_dns1) 
    {
      LOGWARN(F("Only DNS1 set"));
     
      WiFi.config(_WiFi_STA_IPconfig._sta_static_ip, _WiFi_STA_IPconfig._sta_static_gw, _WiFi_STA_IPconfig._sta_static_sn, _WiFi_STA_IPconfig._sta_static_dns1);
    }
    else 
    {
      LOGWARN(F("No DNS server set"));
  
      WiFi.config(_WiFi_STA_IPconfig._sta_static_ip, _WiFi_STA_IPconfig._sta_static_gw, _WiFi_STA_IPconfig._sta_static_sn);
    }
    //***** End added section for DNS config option *****

    LOGINFO1(F("setWifiStaticIP IP ="), WiFi.localIP());
  }
  else
  {
    LOGWARN(F("Can't use Custom STA IP/GW/Subnet"));
  }
#else
  // check if we've got static_ip settings, if we do, use those.
  if (_WiFi_STA_IPconfig._sta_static_ip)
  {
    WiFi.config(_WiFi_STA_IPconfig._sta_static_ip, _WiFi_STA_IPconfig._sta_static_gw, _WiFi_STA_IPconfig._sta_static_sn);
    
    log_w("Custom STA IP/GW/Subnet : %s", WiFi.localIP());
  }
#endif
}

//////////////////////////////////////////

// New from v1.1.1
int ESPAsync_WiFiManager::reconnectWifi()
{
  int connectResult;
  
  // using user-provided  _ssid, _pass in place of system-stored ssid and pass
  if ( ( connectResult = connectWifi(_ssid, _pass) ) != WL_CONNECTED)
  {  
    log_e("Failed to connect to \"%s\"", _ssid);
    
    if ( ( connectResult = connectWifi(_ssid1, _pass1) ) != WL_CONNECTED)
    {  
      log_e("Failed to connect to \"%s\"", _ssid1);

    }
    else
      log_e("Connected to \"%s\"", _ssid1);
  }
  else
      log_e("Connected to \"%s\"", _ssid);
  
  return connectResult;
}

//////////////////////////////////////////

int ESPAsync_WiFiManager::connectWifi(String ssid, String pass)
{
  // Add option if didn't input/update SSID/PW => Use the previous saved Credentials.
  // But update the Static/DHCP options if changed.
  if ( (ssid != "") || ( (ssid == "") && (WiFi_SSID() != "") ) )
  {  
    //fix for auto connect racing issue. Move up from v1.1.0 to avoid resetSettings()
    if (WiFi.status() == WL_CONNECTED)
    {
      log_w("Already connected. Bailing out.");
      return WL_CONNECTED;
    }
     
    if (ssid != "")
      resetSettings();

    WiFi.mode(WIFI_AP_STA); //It will start in station mode if it was previously in AP mode.

    setHostname();
    
    setWifiStaticIP();

    if (ssid != "")
    {
      // Start Wifi with new values.
      log_w("Connect to new WiFi using new IP parameters");
      
      WiFi.begin(ssid.c_str(), pass.c_str());
    }
    else
    {
      // Start Wifi with old values.
      log_w("Connect to previous WiFi using new IP parameters");
      
      WiFi.begin();
    }
  }
  else if (WiFi_SSID() == "")
  {
    log_w("No saved credentials");
  }

  int connRes = waitForConnectResult();
  log_w("Connection result: %s", getStatus(connRes));

  //not connected, WPS enabled, no pass - first attempt
  if (_tryWPS && connRes != WL_CONNECTED && pass == "")
  {
    startWPS();
    //should be connected at the end of WPS
    connRes = waitForConnectResult();
  }

  return connRes;
}

//////////////////////////////////////////

wl_status_t ESPAsync_WiFiManager::waitForConnectResult()
{
  if (_connectTimeout == 0)
  {
    unsigned long startedAt = millis();
    
    // In ESP8266, WiFi.waitForConnectResult() @return wl_status_t (0-255) or -1 on timeout !!!
    // In ESP32, WiFi.waitForConnectResult() @return wl_status_t (0-255)
    // So, using int for connRes to be safe
    //int connRes = WiFi.waitForConnectResult();
    WiFi.waitForConnectResult();
    
    float waited = (millis() - startedAt);

    log_w("Connected after waiting (s): %f", waited / 1000);
    log_w("Local ip = %s", WiFi.localIP().toString().c_str());

    // Fix bug from v1.1.0+, connRes is sometimes not correct.
    //return connRes;
    return WiFi.status();
  }
  else
  {
    log_e("Waiting WiFi connection with time out");
    unsigned long start = millis();
    bool keepConnecting = true;
    
    wl_status_t status;

    while (keepConnecting)
    {
      status = WiFi.status();
      if (millis() > start + _connectTimeout)
      {
        keepConnecting = false;
        log_e("Connection timed out");
      }

      if (status == WL_CONNECTED || status == WL_CONNECT_FAILED)    
      {
        keepConnecting = false;
      }
      delay(100);
    }
    
    return status;
  }
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::startWPS()
{
#ifdef ESP8266
  LOGINFO("START WPS");
  WiFi.beginWPSConfig();
  LOGINFO("END WPS");
#else		//ESP32
  // TODO
  log_i("ESP32 WPS TODO");
#endif
}

//////////////////////////////////////////

//Convenient for debugging but wasteful of program space.
//Remove if short of space
const char* ESPAsync_WiFiManager::getStatus(int status)
{
  switch (status)
  {
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    default:
      return "UNKNOWN";
  }
}

//////////////////////////////////////////

String ESPAsync_WiFiManager::getConfigPortalSSID()
{
  return _apName;
}

//////////////////////////////////////////

String ESPAsync_WiFiManager::getConfigPortalPW()
{
  return _apPassword;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::resetSettings()
{
  log_i("Previous settings invalidated");
  
  WiFi.disconnect(true, true);
  
  // Temporary fix for issue of not clearing WiFi SSID/PW from flash of ESP32
  // See https://github.com/khoih-prog/ESPAsync_WiFiManager/issues/25 and https://github.com/espressif/arduino-esp32/issues/400
  WiFi.begin("0","0");
  //////

  delay(200);
  return;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::setTimeout(unsigned long seconds)
{
  setConfigPortalTimeout(seconds);
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::setConfigPortalTimeout(unsigned long seconds)
{
  _configPortalTimeout = seconds * 1000;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::setConnectTimeout(unsigned long seconds)
{
  _connectTimeout = seconds * 1000;
}

void ESPAsync_WiFiManager::setDebugOutput(bool debug)
{
  _debug = debug;
}

//////////////////////////////////////////

// KH, To enable dynamic/random channel
int ESPAsync_WiFiManager::setConfigPortalChannel(int channel)
{
  // If channel < MIN_WIFI_CHANNEL - 1 or channel > MAX_WIFI_CHANNEL => channel = 1
  // If channel == 0 => will use random channel from MIN_WIFI_CHANNEL to MAX_WIFI_CHANNEL
  // If (MIN_WIFI_CHANNEL <= channel <= MAX_WIFI_CHANNEL) => use it
  if ( (channel < MIN_WIFI_CHANNEL - 1) || (channel > MAX_WIFI_CHANNEL) )
    _WiFiAPChannel = 1;
  else if ( (channel >= MIN_WIFI_CHANNEL - 1) && (channel <= MAX_WIFI_CHANNEL) )
    _WiFiAPChannel = channel;

  return _WiFiAPChannel;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::setAPStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn)
{
  log_i("setAPStaticIPConfig");
  _WiFi_AP_IPconfig._ap_static_ip = ip;
  _WiFi_AP_IPconfig._ap_static_gw = gw;
  _WiFi_AP_IPconfig._ap_static_sn = sn;
}

//////////////////////////////////////////

// KH, new using struct
void ESPAsync_WiFiManager::setAPStaticIPConfig(WiFi_AP_IPConfig  WM_AP_IPconfig)
{
  log_i("setAPStaticIPConfig");
  
  memcpy((void *) &_WiFi_AP_IPconfig, &WM_AP_IPconfig, sizeof(_WiFi_AP_IPconfig));
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::getAPStaticIPConfig(WiFi_AP_IPConfig  &WM_AP_IPconfig)
{
  log_i("getAPStaticIPConfig");
  
  memcpy((void *) &WM_AP_IPconfig, &_WiFi_AP_IPconfig, sizeof(WM_AP_IPconfig));
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::setSTAStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn)
{
  log_i("setSTAStaticIPConfig");
  _WiFi_STA_IPconfig._sta_static_ip = ip;
  _WiFi_STA_IPconfig._sta_static_gw = gw;
  _WiFi_STA_IPconfig._sta_static_sn = sn;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::setSTAStaticIPConfig(WiFi_STA_IPConfig WM_STA_IPconfig)
{
  log_i("setSTAStaticIPConfig");
  
  memcpy((void *) &_WiFi_STA_IPconfig, &WM_STA_IPconfig, sizeof(_WiFi_STA_IPconfig));
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::getSTAStaticIPConfig(WiFi_STA_IPConfig &WM_STA_IPconfig)
{
  log_i("getSTAStaticIPConfig");
  
  memcpy((void *) &WM_STA_IPconfig, &_WiFi_STA_IPconfig, sizeof(WM_STA_IPconfig));
}


//////////////////////////////////////////

#if USE_CONFIGURABLE_DNS
void ESPAsync_WiFiManager::setSTAStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn, IPAddress dns_address_1, IPAddress dns_address_2)
{
  log_i("setSTAStaticIPConfig for USE_CONFIGURABLE_DNS");
  _WiFi_STA_IPconfig._sta_static_ip = ip;
  _WiFi_STA_IPconfig._sta_static_gw = gw;
  _WiFi_STA_IPconfig._sta_static_sn = sn;
  _WiFi_STA_IPconfig._sta_static_dns1 = dns_address_1; //***** Added argument *****
  _WiFi_STA_IPconfig._sta_static_dns2 = dns_address_2; //***** Added argument *****
}
#endif

//////////////////////////////////////////

void ESPAsync_WiFiManager::setMinimumSignalQuality(int quality)
{
  _minimumQuality = quality;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::setBreakAfterConfig(bool shouldBreak)
{
  _shouldBreakAfterConfig = shouldBreak;
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::reportStatus(String &page)
{
  if (WiFi_SSID() != "")
  {
    page += "Configured to connect to AP ";
    page += WiFi_SSID();

    if (WiFi.status() == WL_CONNECTED)
    {
      page += " and connected on IP http://";
      page += WiFi.localIP().toString();
      page += "/";
    }
    else
    {
      page += " but not connected.";
    }
  }
  else
  {
    page += "No network configured.";
  }
}

//////////////////////////////////////////

// Handle root or redirect to captive portal
void ESPAsync_WiFiManager::handleRoot(AsyncWebServerRequest *request)
{
  log_d("Handle root");

  // Disable _configPortalTimeout when someone accessing Portal to give some time to config
  _configPortalTimeout = 0;
  
  //wifiSSIDscan  = true;
  //scan();

  if (captivePortal(request))
  {
    // If captive portal redirect instead of displaying the error page.
    return;
  }
  
  String page = "";
  page += _apName;

  if (WiFi_SSID() != "")
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      page += " on ";
      page += WiFi_SSID();
      page += " ";
    }
    else
    {
      page += " on ";
      page += WiFi_SSID();
      page += " ";
    }
  }

  reportStatus(page);
 
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", page);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  
#if USING_CORS_FEATURE
  // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
  response->addHeader("Access-Control-Allow-Origin", "*");
#endif
  
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  
  request->send(response);
}


//////////////////////////////////////////

// Wifi config page handler
void ESPAsync_WiFiManager::handleWifi(AsyncWebServerRequest *request)
{
  log_d("Handle WiFi");

  // Disable _configPortalTimeout when someone accessing Portal to give some time to config
  _configPortalTimeout = 0;
   
  String page = "";

  wifiSSIDscan = false;
  log_d("handleWifi: Scan done");

  if (wifiSSIDCount == 0) 
  {
    log_d("handleWifi: No network found");
    page += "No network found. Refresh to scan again.";
  } 
  else 
  {
    //display networks in page
    String pager = networkListAsString();
    
    page += pager;
  }
  
  wifiSSIDscan = true;
  
  log_d("Static IP = %s", _WiFi_STA_IPconfig._sta_static_ip.toString());
  
  // KH, Comment out to permit changing from DHCP to static IP, or vice versa
  // and add staticIP label in CP
  
  // To permit disable/enable StaticIP configuration in Config Portal from sketch. Valid only if DHCP is used.
  // You'll loose the feature of dynamically changing from DHCP to static IP, or vice versa
  // You have to explicitly specify false to disable the feature.

#if !USE_STATIC_IP_CONFIG_IN_CP
  if (_WiFi_STA_IPconfig._sta_static_ip)
#endif  
  {
    page += "Static IP: ";
    page += _WiFi_STA_IPconfig._sta_static_ip.toString();

    page += "Gateway IP";
    page += _WiFi_STA_IPconfig._sta_static_gw.toString();

    page += "Subnet";
    page += _WiFi_STA_IPconfig._sta_static_sn.toString();

  #if USE_CONFIGURABLE_DNS
    //***** Added for DNS address options *****
    page += "DNS1 IP";
    page += _WiFi_STA_IPconfig._sta_static_dns1.toString();

    page += "DNS2 IP";
    page += _WiFi_STA_IPconfig._sta_static_dns2.toString();
    //***** End added for DNS address options *****
  #endif
  }
  
#if ( USING_ESP32_S2 || USING_ESP32_C3 )
  request->send(200, "text/plain", page);
  
  // Fix ESP32-S2 issue with WebServer (https://github.com/espressif/arduino-esp32/issues/4348)
  delay(1);
#else  
 
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", page);
  
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  
#if USING_CORS_FEATURE
  // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
  response->addHeader("Access-Control-Allow-Origin", "*");
#endif
  
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
  
#endif    // ( USING_ESP32_S2 || USING_ESP32_C3 )

  log_d("Sent config page");
}

//////////////////////////////////////////

// Handle the WLAN save form and redirect to WLAN config page again
void ESPAsync_WiFiManager::handleWifiSave(AsyncWebServerRequest *request)
{
  log_d("WiFi save");

  //SAVE/connect here
  _ssid = request->arg("s").c_str();
  _pass = request->arg("p").c_str();
  
  // New from v1.1.0
  _ssid1 = request->arg("s1").c_str();
  _pass1 = request->arg("p1").c_str();

  if (request->hasArg("ip"))
  {
    String ip = request->arg("ip");
    optionalIPFromString(&_WiFi_STA_IPconfig._sta_static_ip, ip.c_str());
    
    log_d("New Static IP = %s", _WiFi_STA_IPconfig._sta_static_ip.toString());
  }

  if (request->hasArg("gw"))
  {
    String gw = request->arg("gw");
    optionalIPFromString(&_WiFi_STA_IPconfig._sta_static_gw, gw.c_str());
    
    log_d("New Static Gateway = %s", _WiFi_STA_IPconfig._sta_static_gw.toString());
  }

  if (request->hasArg("sn"))
  {
    String sn = request->arg("sn");
    optionalIPFromString(&_WiFi_STA_IPconfig._sta_static_sn, sn.c_str());
    
    log_d("New Static Netmask = %s", _WiFi_STA_IPconfig._sta_static_sn.toString());
  }

#if USE_CONFIGURABLE_DNS
  //*****  Added for DNS Options *****
  if (request->hasArg("dns1"))
  {
    String dns1 = request->arg("dns1");
    optionalIPFromString(&_WiFi_STA_IPconfig._sta_static_dns1, dns1.c_str());
    
    log_d("New Static DNS1 = %s", _WiFi_STA_IPconfig._sta_static_dns1.toString());
  }

  if (request->hasArg("dns2"))
  {
    String dns2 = request->arg("dns2");
    optionalIPFromString(&_WiFi_STA_IPconfig._sta_static_dns2, dns2.c_str());
    
    log_d("New Static DNS2 = %s", _WiFi_STA_IPconfig._sta_static_dns2.toString());
  }
  //*****  End added for DNS Options *****
#endif

  String page = "";
  page +=  "Credentials Saved: ";
  page += _apName;
  page += " ";
  page += _ssid;
  page += " ";
  page += _pass;
  page += " ";
  page += _ssid1;
  page += " ";
  page += _pass1;

#if ( USING_ESP32_S2 || USING_ESP32_C3 )
  request->send(200, "text/plain", page);
  
  // Fix ESP32-S2 issue with WebServer (https://github.com/espressif/arduino-esp32/issues/4348)
  delay(1);
#else    
 
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", page);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  
#if USING_CORS_FEATURE
  // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
  response->addHeader("Access-Control-Allow-Origin", "*");
#endif
  
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
  
#endif    // ( USING_ESP32_S2 || USING_ESP32_C3 )

  log_d("Sent wifi save page");

  connect = true; //signal ready to connect/reset

  // Restore when Press Save WiFi
  _configPortalTimeout = DEFAULT_PORTAL_TIMEOUT;
}

//////////////////////////////////////////

// Handle shut down the server page
void ESPAsync_WiFiManager::handleServerClose(AsyncWebServerRequest *request)
{
  log_d("Server Close");
   
  String page = "";
  page += "Close Server";
  page += "My network is ";
  page += WiFi_SSID();
  page += " IP address is ";
  page += WiFi.localIP().toString();
  page += "Portal closed...";
  
  //page += F("Push button on device to restart configuration server!");
  
#if ( USING_ESP32_S2 || USING_ESP32_C3 )
  request->send(200, "text/plain", page);
  
  // Fix ESP32-S2 issue with WebServer (https://github.com/espressif/arduino-esp32/issues/4348)
  delay(1);
#else    
 
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", page);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  
#if USING_CORS_FEATURE
  // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
  response->addHeader("Access-Control-Allow-Origin", "*");
#endif
  
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
  
#endif    // ( USING_ESP32_S2 || USING_ESP32_C3 )
  
  stopConfigPortal = true; //signal ready to shutdown config portal
  
  log_d("Sent server close page");

  // Restore when Press Save WiFi
  _configPortalTimeout = DEFAULT_PORTAL_TIMEOUT;
}

//////////////////////////////////////////

// Handle the info page
void ESPAsync_WiFiManager::handleInfo(AsyncWebServerRequest *request)
{
  log_d("Info");

  // Disable _configPortalTimeout when someone accessing Portal to give some time to config
  _configPortalTimeout = 0;
 
  String page = "";
  page += "Info";

  if (connect)
    page += "connected. ";
  
  if (connect)
  {
    page += "Trying to connect: ";
    page += wifiStatus;
    page += " ";
  }

  page +=pager;
  
  page += "WiFi Information ";
  reportStatus(page);
  
  page += "Device Data ";  
  page += "Chip ID ";
  page += String((uint32_t)ESP.getEfuseMac(), HEX);		//ESP.getChipId();

  page += "Flash Chip ID ";
  // TODO
  page += "TODO ";

  page += "IDE Flash Size ";
  page += ESP.getFlashChipSize();
  page += " bytes ";
  page += "Real Flash Size ";

  // TODO
  page += "TODO";

  page += " bytes ";
  page += "Access Point IP ";
  page += WiFi.softAPIP().toString();
  page += " Access Point MAC ";
  page += WiFi.softAPmacAddress();

  page += " SSID ";
  page += WiFi_SSID();

  page += " Station IP ";
  page += WiFi.localIP().toString();
  
  page += " Station MAC ";
  page += WiFi.macAddress();

  page += F("<p/>More information about ESPAsync_WiFiManager at");
  page += F("<p/><a href=\"https://github.com/khoih-prog/ESPAsync_WiFiManager\">https://github.com/khoih-prog/ESPAsync_WiFiManager</a>");
 
#if ( USING_ESP32_S2 || USING_ESP32_C3 )
  request->send(200, "text/plain", page);
  
  // Fix ESP32-S2 issue with WebServer (https://github.com/espressif/arduino-esp32/issues/4348)
  delay(1);
#else    
 
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", page);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  
#if USING_CORS_FEATURE
  // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
  response->addHeader("Access-Control-Allow-Origin", "*");
#endif
  
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");

  request->send(response);
#endif    // ( USING_ESP32_S2 || USING_ESP32_C3 )

  log_d("Info page sent");
}

//////////////////////////////////////////

// Handle the state page
void ESPAsync_WiFiManager::handleState(AsyncWebServerRequest *request)
{
  log_d("State-Json");
   
  String page = F("{\"Soft_AP_IP\":\"");
  page += WiFi.softAPIP().toString();
  page += F("\",\"Soft_AP_MAC\":\"");
  page += WiFi.softAPmacAddress();
  page += F("\",\"Station_IP\":\"");
  page += WiFi.localIP().toString();
  page += F("\",\"Station_MAC\":\"");
  page += WiFi.macAddress();
  page += F("\",");

  if (WiFi.psk() != "")
  {
    page += F("\"Password\":true,");
  }
  else
  {
    page += F("\"Password\":false,");
  }

  page += F("\"SSID\":\"");
  page += WiFi_SSID();
  page += F("\"}");
   
#if ( USING_ESP32_S2 || USING_ESP32_C3 )
  request->send(200, "application/json", page);
  
  // Fix ESP32-S2 issue with WebServer (https://github.com/espressif/arduino-esp32/issues/4348)
  delay(1);
#else  
   
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", page);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  
#if USING_CORS_FEATURE
  // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
  response->addHeader("Access-Control-Allow-Origin", "*");
#endif
  
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  
  request->send(response);
#endif    // ( USING_ESP32_S2 || USING_ESP32_C3 )
  
  log_d("Sent state page in json format");
}

//////////////////////////////////////////

/** Handle the scan page */
void ESPAsync_WiFiManager::handleScan(AsyncWebServerRequest *request)
{
  log_d("Scan");

  // Disable _configPortalTimeout when someone accessing Portal to give some time to config
  _configPortalTimeout = 0;		//KH

  log_d("Scan-Json");

  String page = F("{\"Access_Points\":[");
  
  // KH, display networks in page using previously scan results
  for (int i = 0; i < wifiSSIDCount; i++) 
  {
    if (wifiSSIDs[i].duplicate == true) 
      continue; // skip dups
      
    if (i != 0)
      page += F(", ");

    log_d("Index = %i", i);
    log_d("SSID = %s", wifiSSIDs[i].SSID);
    log_d("RSSI = %i", wifiSSIDs[i].RSSI);
      
    int quality = getRSSIasQuality(wifiSSIDs[i].RSSI);

    if (_minimumQuality == -1 || _minimumQuality < quality) 
    {
      String item = FPSTR(JSON_ITEM);
      String rssiQ;
      
      rssiQ += quality;
      item.replace("{v}", wifiSSIDs[i].SSID);
      item.replace("{r}", rssiQ);

      if (wifiSSIDs[i].encryptionType != WIFI_AUTH_OPEN)
      {
      item.replace("{i}", "true");
      }
      else
      {
        item.replace("{i}", "false");
      }
      
      page += item;
      delay(0);
    } 
    else 
    {
      log_d("Skipping due to quality");
    }
  }
  
  page += F("]}");
 
#if ( USING_ESP32_S2 || USING_ESP32_C3 ) 
  request->send(200, "application/json", page);
  
  // Fix ESP32-S2 issue with WebServer (https://github.com/espressif/arduino-esp32/issues/4348)
  delay(1);
#else  
   
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", page);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  
#if USING_CORS_FEATURE
  // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
  response->addHeader("Access-Control-Allow-Origin", "*");
#endif
  
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  
  request->send(response);
#endif    // ( USING_ESP32_S2 || USING_ESP32_C3 )  
   
  log_d("Sent WiFiScan Data in Json format");
}

//////////////////////////////////////////

// Handle the reset page
void ESPAsync_WiFiManager::handleReset(AsyncWebServerRequest *request)
{
  log_d("Reset");
    
  String page = "";
  page += "WiFi Information";
  page += "Resetting";
  
#if ( USING_ESP32_S2 || USING_ESP32_C3 ) 
  request->send(200, "text/plain", page);
  
  // Fix ESP32-S2 issue with WebServer (https://github.com/espressif/arduino-esp32/issues/4348)
  delay(1);
#else    
 
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", page);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  
#if USING_CORS_FEATURE
  // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
  response->addHeader("Access-Control-Allow-Origin", "*");
#endif
  
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
  
#endif    // ( USING_ESP32_S2 || USING_ESP32_C3 )

  log_d("Sent reset page");
  delay(5000);
  
  // Temporary fix for issue of not clearing WiFi SSID/PW from flash of ESP32
  // See https://github.com/khoih-prog/ESP_WiFiManager/issues/25 and https://github.com/espressif/arduino-esp32/issues/400
  resetSettings();
  //WiFi.disconnect(true); // Wipe out WiFi credentials.
  //////

  ESP.restart();
  delay(2000);
}

//////////////////////////////////////////

void ESPAsync_WiFiManager::handleNotFound(AsyncWebServerRequest *request)
{
  if (captivePortal(request))
  {
    // If captive portal redirect instead of displaying the error page.
    return;
  }

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

#if ( USING_ESP32_S2 || USING_ESP32_C3 )
  request->send(200, "text/plain", message);
  
  // Fix ESP32-S2 issue with WebServer (https://github.com/espressif/arduino-esp32/issues/4348)
  delay(1);
#else    
 
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", message);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  
#if USING_CORS_FEATURE
  // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
  response->addHeader("Access-Control-Allow-Origin", "*");
#endif
  
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);

#endif    // ( USING_ESP32_S2 || USING_ESP32_C3 )
}

//////////////////////////////////////////

/**
   HTTPD redirector
   Redirect to captive portal if we got a request for another domain.
   Return true in that case so the page handler do not try to handle the request again.
*/
bool ESPAsync_WiFiManager::captivePortal(AsyncWebServerRequest *request)
{
  if (!isIp(request->host()))
  {
    log_d("Request redirected to captive portal");
    log_d("Location http://%s", request->client()->localIP().toString().c_str());
    
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
    response->addHeader("Location", String("http://") + toStringIp(request->client()->localIP()));
    request->send(response);
       
    return true;
  }
  
  log_d("request host IP = %s", request->host().c_str());
  
  return false;
}

//////////////////////////////////////////

// start up config portal callback
void ESPAsync_WiFiManager::setAPCallback(void(*func)(ESPAsync_WiFiManager* myWiFiManager))
{
  _apcallback = func;
}

//////////////////////////////////////////

// start up save config callback
void ESPAsync_WiFiManager::setSaveConfigCallback(void(*func)())
{
  _savecallback = func;
}

//////////////////////////////////////////

// sets a custom element to add to head, like a new style tag
void ESPAsync_WiFiManager::setCustomHeadElement(const char* element) {
  _customHeadElement = element;
}

//////////////////////////////////////////

// if this is true, remove duplicated Access Points - defaut true
void ESPAsync_WiFiManager::setRemoveDuplicateAPs(bool removeDuplicates)
{
  _removeDuplicateAPs = removeDuplicates;
}

//////////////////////////////////////////

int ESPAsync_WiFiManager::getRSSIasQuality(int RSSI)
{
  int quality = 0;

  if (RSSI <= -100)
  {
    quality = 0;
  }
  else if (RSSI >= -50)
  {
    quality = 100;
  }
  else
  {
    quality = 2 * (RSSI + 100);
  }

  return quality;
}

//////////////////////////////////////////

// Is this an IP?
bool ESPAsync_WiFiManager::isIp(String str)
{
  for (unsigned int i = 0; i < str.length(); i++)
  {
    int c = str.charAt(i);

    if (c != '.' && (c < '0' || c > '9'))
    {
      return false;
    }
  }
  return true;
}

//////////////////////////////////////////

// IP to String
String ESPAsync_WiFiManager::toStringIp(IPAddress ip)
{
  String res = "";
  for (int i = 0; i < 3; i++)
  {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }

  res += String(((ip >> 8 * 3)) & 0xFF);

  return res;
}

//////////////////////////////////////////

#ifdef ESP32
// We can't use WiFi.SSID() in ESP32 as it's only valid after connected.
// SSID and Password stored in ESP32 wifi_ap_record_t and wifi_config_t are also cleared in reboot
// Have to create a new function to store in EEPROM/SPIFFS for this purpose

String ESPAsync_WiFiManager::getStoredWiFiSSID()
{
  if (WiFi.getMode() == WIFI_MODE_NULL)
  {
    return String();
  }

  wifi_ap_record_t info;

  if (!esp_wifi_sta_get_ap_info(&info))
  {
    return String(reinterpret_cast<char*>(info.ssid));
  }
  else
  {
    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    return String(reinterpret_cast<char*>(conf.sta.ssid));
  }

  return String();
}

//////////////////////////////////////////

String ESPAsync_WiFiManager::getStoredWiFiPass()
{
  if (WiFi.getMode() == WIFI_MODE_NULL)
  {
    return String();
  }

  wifi_config_t conf;
  esp_wifi_get_config(WIFI_IF_STA, &conf);
  
  return String(reinterpret_cast<char*>(conf.sta.password));
}
#endif

//////////////////////////////////////////
