//配网模式显示界面
boolean sdgzState = 1;
void display_peiwang()
{
  uint16_t y0 = 15;
  uint16_t y1 = y0 + 23;
  uint16_t y2 = y1 + 23;
  uint16_t x0 = 100;

  display.setPartialWindow(0, 0, display.width(), display.height()); //设置局部刷新窗口
  u8g2Fonts.setFont(chinese_gb2312);
  display.firstPage();
  do
  {
    display.fillScreen(heise);  // 填充屏幕
    display.display(1);         // 显示缓冲内容到屏幕，用于全屏缓冲
    display.fillScreen(baise);  // 填充屏幕
    display.display(1);         // 显示缓冲内容到屏幕，用于全屏缓冲

    //display.drawInvertedBitmap(15, 15, Bitmap_fashe, 48, 41, heise); //发射站图标
    display.drawInvertedBitmap(5, 0, Bitmap_pwewm, 64, 64, heise); //二维码图标

    u8g2Fonts.setCursor(x0, y0);
    u8g2Fonts.print("热点名称:");
    u8g2Fonts.setCursor(x0 + 65, y0);
    u8g2Fonts.print(ap_ssid);

    u8g2Fonts.setCursor(x0, y1);
    u8g2Fonts.print("热点密码:");
    u8g2Fonts.setCursor(x0 + 65, y1);
    u8g2Fonts.print(String(ap_password) + "(9个3)");

    u8g2Fonts.setCursor(x0, y2);
    u8g2Fonts.print("热点地址:");
    u8g2Fonts.setCursor(x0 + 65, y2);
    u8g2Fonts.print(WiFi.softAPIP());
  }
  while (display.nextPage());
}

boolean qqtq_state1 = 1;      // 请求天气状态位
uint32_t sta_count = 0;       // sta连接计数
uint32_t getvcc_time = 0;     // 获取电池电压时间
int8_t ap_state = 0;          // 启动ap模式状态 -1扫描到网络，尝试连接 0-无 1-WiFi未配置 2-配置的WiFi扫描不到 3-连接失败 4-连接成功 5-更换wifi
boolean peiwangInitStete = 0; // 配网初始化 0-未初始化 1-已初始化

void peiwang_mod()
{
  if (millis() - overtime > 300000) display_bitmap_sleep("* 操作超时 *");
  if (peiwangInitStete == 0)
  {
    peiwangInitStete = 1;
    u8g2Fonts.setFont(chinese_gb2312);
    //display_batvcc();
    if (ap_state <= 2)
    {
      //先检查wifi是否有配置
      if (WiFi.SSID().length() == 0) ap_state = 1;  // wifi名称为空开启ap模式
      else if (WiFi.SSID().length() > 0)  // wifi名称存在，尝试连接
      {
        display.firstPage();
        do {
          display.setPartialWindow(0, 0, display.width(), 56); //设置局部刷新窗口
          display.drawInvertedBitmap(124, 14, Bitmap_fashe, 48, 41, heise); //发射站图标
        } while (display.nextPage());
        ap_state = -1;
      }
    }
    if (ap_state > 0)
    {
      if (ap_state == 4) initApSTA();
      else  initAp();

      if (webDisplayQh_state == 0)
      {
        initWebServer();     // 初始化WEB服务器、webServerOTA服务
      }

      display_peiwang();   // 显示热点信息
      display_batvcc();    // 获取电池电压

      if (ap_state == 1) {
        sta_count = 22;
        display_partialLine(6, "WiFi未配置，已启动热点");
      }
      /*else if (ap_state == 2) {
        sta_count = 21;
        display_partialLine(6, "扫描不到已配置的WiFi，已启动热点");
        }*/
      else if (ap_state == 3) display_partialLine(6, WiFi.SSID() + " 连接失败，已启动热点");
      else if (ap_state == 4) display_partialLine(6, WiFi.SSID() + " 连接成功(" + WiFi.localIP().toString() + ")");
    }
  }

  if (ap_state == -1 || ap_state == 5) //扫描到网络，设置工作模式
  {
    //WiFi.setAutoConnect(1);   //设置自动连接wifi
    if (ap_state == -1)
    {
      WiFi.mode(WIFI_STA);
      WiFi.begin();
    }
    else if (ap_state == 5)
    {
      WiFi.persistent(true);  //需要保存
      WiFi.mode(WIFI_AP_STA); //设置工作模式
      WiFi.begin(sta_ssid, sta_password);
    }

    display_partialLine(6, "正在尝试连接：" + WiFi.SSID());

    sta_count = 0;
    while (WiFi.isConnected() == 0 && sta_count < 15) //尝试连接
    {
      ESP.wdtFeed();           // 喂狗
      server.handleClient();
      delay(900);
      sta_count++;
      if (sta_count >= 15)
      {
        peiwangInitStete = 0;
        ap_state = 3; //wifi连接失败
      }
    }

    if (WiFi.isConnected()) //连接成功，尝试获取天气参数
    {
      peiwangInitStete = 0;
      if (ap_state == -1) //仅首次连接获取天气数据，https不能获取两次以上 http可以
      {
        String weatherKey_s = eepUserSet.weatherKey;
        String city_s = eepUserSet.city;
        String url_ActualWeather;
        //拼装天气实况API地址
        url_ActualWeather = "http://api.seniverse.com/v3/weather/now.json";
        url_ActualWeather += "?key=" + weatherKey_s + "&location=" + city_s + "&language=" + language + "&unit=c";
        ParseActualWeather(callHttp(url_ActualWeather), &actual);
      }

      if (ap_state == 5) {
        webDisplayQh_state = 0;
      }
      ap_state = 4; //wifi连接成功
    }
  }
  else if (ap_state > 0)
  {
    server.handleClient();  //处理http请求
    if (millis() - getvcc_time > 10000) //定时任务
    {
      getvcc_time = millis();
      display_batvcc(); //更新电池电压
      yield();
    }
  }
}

void display_batvcc()
{
  float bat_vcc = getBatVolNew();              // 获取电压值
  uint8_t bat_vcc_bfb = getBatVolBfb(bat_vcc); // 换成百分比
  display_partialLine(5, "电量：" + String(bat_vcc_bfb) + "% (" + String(bat_vcc) + "V)");
  display.powerOff(); //仅关闭电源
}
//Serial.print("WiFi.SSID："); Serial.println(WiFi.SSID());
//Serial.print("WiFi.psk："); Serial.println(WiFi.psk());
