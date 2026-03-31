/*** 初始化STA配置 ***/
void connectToWifi() //连接wifi
{
  //WiFi.persistent(true);
  //WiFi.disconnect();      //断开WIFI连接,清掉之前最近一个连接点信息
  //WiFi.mode(WIFI_OFF);      //设置工作模式
  WiFi.setOutputPower(eepUserSet.outputPower);  //设置发射功率
  WiFi.setAutoConnect(1);   //设置自动连接wifi
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.config(0, 0, 0, dns1, dns2);
  WiFi.mode(WIFI_STA);      //设置工作模式
  WiFi.begin();  //要连接的WiFi名称和密码
}

/*** 初始化AP配置 ***/
void initApSTA()
{
  //WiFi.persistent(true);
  WiFi.mode(WIFI_AP_STA); //设置工作模式
  WiFi.softAPConfig(local_IP, gateway, subnet); //ap的网络参数
  WiFi.softAP(ap_ssid, ap_password, random(1, 14), 0, 1);  //ap的名称和密码
  WiFi.begin();
}

void initAp()
{
  //WiFi.persistent(true);
  //WiFi.mode(WIFI_OFF);   //设置工作模式
  WiFi.softAPConfig(local_IP, gateway, subnet); //ap的网络参数
  WiFi.softAP(ap_ssid, ap_password, random(1, 14), 0, 1); //ap的名称、密码、信道、是否隐藏、同时连接数
  WiFi.mode(WIFI_AP); //设置工作模式
}

//********* 扫描周边网络 0-扫描到 1-无
boolean scanNetworks(String currentSSID)
{
  boolean scanState = 0; //扫描状态，0-扫描到 1-无
  uint8_t n = WiFi.scanNetworks(); //扫描wifi
  if (n == 0) scanState = 0; //扫描不到开启ap模式
  else
  {
    for (uint8_t i = 0; i < n; i++)
    {
      //Serial.println(WiFi.SSID(i));
      if (strcmp(currentSSID.c_str(), WiFi.SSID(i).c_str()) == 0) scanState = 1;
      else if (scanState != 1 && strcmp(currentSSID.c_str(), WiFi.SSID(i).c_str()) != 0)scanState = 0;
    }
  }
  WiFi.scanDelete();  //删除扫描的结果
  return scanState;
}
/*
  WiFi.mode(); WIFI_AP /WIFI_STA /WIFI_AP_STA /WIFI_OFF
*/
//****** OTA设置并启动 ******
//ArduinoOTA.handle();
/*void initOTA()
  {
  ArduinoOTA.setHostname("esp8266");
  ArduinoOTA.setPassword("333333");
  ArduinoOTA.begin();
  }*/
