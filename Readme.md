# AutoConnectAPI
## Intro
This library is a stripped down version of the [Wifi Manager](https://github.com/khoih-prog/ESPAsync_WiFiManager) to be used without config portal but via API only.

As with the WIfi Manager for now only the last used Wifi credentials are remembered.

## Usage
If no credentials are available an access point named "Babbaphone", pwd "babbaphone", is created. Connect with your device to this access point. 

The web server establishes the */wifisave* endpoint. Use following curl command to provide credentials:
    
    `curl -i 192.168.251.89/wifisave -H "SSID: ssid" -H "Pwd: pwd"`

The ESP32 will restart and connect to the given network. The webserver establishes a */test* endpoint which responds a "Hello World".

To reset credentials use the */reset* endpoint:

    `curl -i 192.168.251.89/reset`

## TODO
* remember several SSIDs and PWD: https://hieromon.github.io/AutoConnect/api.html
* use https

