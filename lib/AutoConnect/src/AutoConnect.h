#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())
typedef int16_t wifi_ssid_count_t;  

typedef struct
{
  IPAddress _sta_static_ip;
  IPAddress _sta_static_gw;
  IPAddress _sta_static_sn;
  IPAddress _sta_static_dns1;
  IPAddress _sta_static_dns2;
}  WiFi_STA_IPConfig;

typedef struct
{
  IPAddress _ap_static_ip;
  IPAddress _ap_static_gw;
  IPAddress _ap_static_sn;

}  WiFi_AP_IPConfig;

class WiFiResult
{
  public:
    bool duplicate;
    String SSID;
    uint8_t encryptionType;
    int32_t RSSI;
    uint8_t* BSSID;
    int32_t channel;
    bool isHidden;

    WiFiResult()
    {
    }
};

const char JSON_ITEM[] PROGMEM    = "{\"SSID\":\"{v}\", \"Encryption\":{i}, \"Quality\":\"{r}\"}";

#define WFM_LABEL_BEFORE 1
#define WFM_LABEL_AFTER 2
#define WFM_NO_LABEL 0

typedef struct
{
  const char *_id;
  const char *_placeholder;
  char       *_value;
  int         _length;
  int         _labelPlacement;

}  WMParam_Data;

class ESPAsync_WMParameter 
{
  public:
  
    ESPAsync_WMParameter(const char *custom);
    //ESPAsync_WMParameter(const char *id, const char *placeholder, const char *defaultValue, int length);
    //ESPAsync_WMParameter(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom);
    ESPAsync_WMParameter(const char *id, const char *placeholder, const char *defaultValue, int length, 
                          const char *custom = "", int labelPlacement = WFM_LABEL_BEFORE);
                          
    // KH, using struct                      
    ESPAsync_WMParameter(WMParam_Data WMParam_data);                      
    //////
    
    ~ESPAsync_WMParameter();
    
    // Using Struct
    void setWMParam_Data(WMParam_Data WMParam_data);
    void getWMParam_Data(WMParam_Data &WMParam_data);
    //////
 
    const char *getID();
    const char *getValue();
    const char *getPlaceholder();
    int         getValueLength();
    int         getLabelPlacement();
    const char *getCustomHTML();
    
  private:
  
    WMParam_Data _WMParam_data;
    
    const char *_customHTML;

    void init(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom, int labelPlacement);

    friend class ESPAsync_WiFiManager;
};

/////////////////////////////////////////////////////////////////////////////

class ESPAsync_WiFiManager
{
  public:

    ESPAsync_WiFiManager(AsyncWebServer * webserver, DNSServer *dnsserver, const char *iHostname = "");

    ~ESPAsync_WiFiManager();
    
    //Scan for WiFiNetworks in range and sort by signal strength
    void          scan();
    
    String        scanModal();
    void          loop();
    void          safeLoop();
    void          criticalLoop();
    String        infoAsString();

    // Can use with STA staticIP now
    bool          autoConnect();
    bool          autoConnect(char const *apName, char const *apPassword = NULL);
    //////

    // If you want to start the config portal
    bool          startConfigPortal();
    bool          startConfigPortal(char const *apName, char const *apPassword = NULL);
    void startConfigPortalModeless(char const *apName, char const *apPassword, bool shouldConnectWiFi = true);


    // get the AP name of the config portal, so it can be used in the callback
    String        getConfigPortalSSID();
    // get the AP password of the config portal, so it can be used in the callback
    String        getConfigPortalPW();

    void          resetSettings();

    //sets timeout before webserver loop ends and exits even if there has been no setup.
    //usefully for devices that failed to connect at some point and got stuck in a webserver loop
    //in seconds setConfigPortalTimeout is a new name for setTimeout
    void          setConfigPortalTimeout(unsigned long seconds);
    void          setTimeout(unsigned long seconds);

    //sets timeout for which to attempt connecting, usefull if you get a lot of failed connects
    void          setConnectTimeout(unsigned long seconds);


    void          setDebugOutput(bool debug);
    //defaults to not showing anything under 8% signal quality if called
    void          setMinimumSignalQuality(int quality = 8);
    
    // To enable dynamic/random channel
    int           setConfigPortalChannel(int channel = 1);
    //////
    
    //sets a custom ip /gateway /subnet configuration
    void          setAPStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn);
    
    // KH, new using struct
    void          setAPStaticIPConfig(WiFi_AP_IPConfig  WM_AP_IPconfig);
    void          getAPStaticIPConfig(WiFi_AP_IPConfig  &WM_AP_IPconfig);
    //////
    
    //sets config for a static IP
    void          setSTAStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn);
    
    // KH, new using struct
    void          setSTAStaticIPConfig(WiFi_STA_IPConfig  WM_STA_IPconfig);
    void          getSTAStaticIPConfig(WiFi_STA_IPConfig  &WM_STA_IPconfig);
    //////

#if USE_CONFIGURABLE_DNS
    void          setSTAStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn,
                                       IPAddress dns_address_1, IPAddress dns_address_2);
#endif

    //called when AP mode and config portal is started
    void          setAPCallback(void(*func)(ESPAsync_WiFiManager*));
    //called when settings have been changed and connection was successful
    void          setSaveConfigCallback(void(*func)());

    //if this is set, it will exit after config, even if connection is unsucessful.
    void          setBreakAfterConfig(bool shouldBreak);
    
    //if this is set, try WPS setup when starting (this will delay config portal for up to 2 mins)
    //TODO
    //if this is set, customise style
    void          setCustomHeadElement(const char* element);
    
    //if this is true, remove duplicated Access Points - defaut true
    void          setRemoveDuplicateAPs(bool removeDuplicates);
    
    // return SSID of router in STA mode got from config portal. NULL if no user's input //KH
    String				getSSID() 
    {
      return _ssid;
    }

    // return password of router in STA mode got from config portal. NULL if no user's input //KH
    String				getPW() 
    {
      return _pass;
    }
    
    // New from v1.1.0
    // return SSID of router in STA mode got from config portal. NULL if no user's input //KH
    String				getSSID1() 
    {
      return _ssid1;
    }

    // return password of router in STA mode got from config portal. NULL if no user's input //KH
    String				getPW1() 
    {
      return _pass1;
    }
    
    #define MAX_WIFI_CREDENTIALS        2
    
    String				getSSID(uint8_t index) 
    {
      if (index == 0)
        return _ssid;
      else if (index == 1)
        return _ssid1;
      else     
        return String("");
    }
    
    String				getPW(uint8_t index) 
    {
      if (index == 0)
        return _pass;
      else if (index == 1)
        return _pass1;
      else     
        return String("");
    }
    //////
    
    // New from v1.1.1, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
#if USING_CORS_FEATURE
    void setCORSHeader(const char* CORSHeaders)
    {     
      _CORS_Header = CORSHeaders;

      LOGWARN1(F("Set CORS Header to : "), _CORS_Header);
    }
    
    const char* getCORSHeader()
    {
      return _CORS_Header;
    }
#endif     

    //returns the list of Parameters
    ESPAsync_WMParameter** getParameters();
    
    // returns the Parameters Count
    int           getParametersCount();

    const char*   getStatus(int status);

#ifdef ESP32
    String getStoredWiFiSSID();
    String getStoredWiFiPass();
#endif

    String WiFi_SSID()
    {
      return getStoredWiFiSSID();
    }

    String WiFi_Pass()
    {
      return getStoredWiFiPass();
    }

    void setHostname()
    {
      if (RFC952_hostname[0] != 0)
      {
  #if !( USING_ESP32_S2 || USING_ESP32_C3 )
        // See https://github.com/espressif/arduino-esp32/issues/2537
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.setHostname(RFC952_hostname);
  #endif     
      }
    }
    

  private:
  
    DNSServer      *dnsServer;

    AsyncWebServer *server;

    bool            _modeless;
    int             scannow;
    int             shouldscan;
    bool            needInfo = true;
    String          pager;
    wl_status_t     wifiStatus;

#define RFC952_HOSTNAME_MAXLEN      24
    char RFC952_hostname[RFC952_HOSTNAME_MAXLEN + 1];

    char* getRFC952_hostname(const char* iHostname);

    void          setupConfigPortal();
    void          startWPS();

    const char*   _apName               = "no-net";
    const char*   _apPassword           = NULL;
    
    String        _ssid                 = "";
    String        _pass                 = "";
    
    // New from v1.1.0
    String        _ssid1  = "";
    String        _pass1  = "";
    //////

    unsigned long _configPortalTimeout  = 0;

    unsigned long _connectTimeout       = 0;
    unsigned long _configPortalStart    = 0;

    int                 numberOfNetworks;
    int                 *networkIndices;
    
    WiFiResult          *wifiSSIDs;
    wifi_ssid_count_t   wifiSSIDCount;
    bool                wifiSSIDscan;
    
    // To enable dynamic/random channel
    // default to channel 1
    #define MIN_WIFI_CHANNEL      1
    #define MAX_WIFI_CHANNEL      11    // Channel 12,13 is flaky, because of bad number 13 ;-)

    int _WiFiAPChannel = 1;
    //////

    WiFi_AP_IPConfig  _WiFi_AP_IPconfig;
    
    WiFi_STA_IPConfig _WiFi_STA_IPconfig = { IPAddress(0, 0, 0, 0), IPAddress(192, 168, 2, 1), IPAddress(255, 255, 255, 0),
                                             IPAddress(192, 168, 2, 1), IPAddress(8, 8, 8, 8) };

    int           _paramsCount              = 0;
    int           _minimumQuality           = -1;
    bool          _removeDuplicateAPs       = true;
    bool          _shouldBreakAfterConfig   = false;
    bool          _tryWPS                   = false;

    const char*   _customHeadElement        = "";

    int           status                    = WL_IDLE_STATUS;
    
    // New from v1.1.0, for configure CORS Header, default to WM_HTTP_CORS_ALLOW_ALL = "*"
#if USING_CORS_FEATURE
    const char*     _CORS_Header            = WM_HTTP_CORS_ALLOW_ALL;   //"*";
#endif   
    //////

    void          setWifiStaticIP();
    
    // New v1.1.0
    int           reconnectWifi();
    //////
    
    int           connectWifi(String ssid = "", String pass = "");
    
    wl_status_t   waitForConnectResult();
    
    void          setInfo();
    String        networkListAsString();
    
    void          handleRoot(AsyncWebServerRequest *request);
    void          handleWifi(AsyncWebServerRequest *request);
    void          handleWifiSave(AsyncWebServerRequest *request);
    void          handleServerClose(AsyncWebServerRequest *request);
    void          handleInfo(AsyncWebServerRequest *request);
    void          handleState(AsyncWebServerRequest *request);
    void          handleScan(AsyncWebServerRequest *request);
    void          handleReset(AsyncWebServerRequest *request);
    void          handleNotFound(AsyncWebServerRequest *request);
    bool          captivePortal(AsyncWebServerRequest *request);   
    
    void          reportStatus(String &page);

    // DNS server
    const byte    DNS_PORT = 53;

    //helpers
    int           getRSSIasQuality(int RSSI);
    bool          isIp(String str);
    String        toStringIp(IPAddress ip);

    bool          connect;
    bool          stopConfigPortal = false;
    
    bool          _debug = false;     //true;
    
    void(*_apcallback)(ESPAsync_WiFiManager*) = NULL;
    void(*_savecallback)()                = NULL;

    template <typename Generic>
    void          DEBUG_WM(Generic text);

    template <class T>
    auto optionalIPFromString(T *obj, const char *s) -> decltype(obj->fromString(s)) 
    {
      return  obj->fromString(s);
    }
    
    auto optionalIPFromString(...) -> bool 
    {
      log_i("NO fromString METHOD ON IPAddress, you need ESP8266 core 2.1.0+ for Custom IP configuration to work.");
      return false;
    }
};