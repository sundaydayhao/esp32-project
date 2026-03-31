//系统休眠
void esp_sleep(uint32_t minutes)
{
  if (eepUserSet.sdState && sdInitOk) SDFS.end();
  digitalWrite(bat_switch_pin, 0); //关闭电池测量
  pinMode(bat_switch_pin, INPUT);  //改为输入状态避免漏电
  /*pinMode(1, INPUT); // INPUT_PULLUP
    pinMode(13, INPUT); // INPUT_PULLUP
    pinMode(5, INPUT); // INPUT_PULLUP
    pinMode(4, INPUT); // INPUT_PULLUP*/
  if (eepUserSet.runMode == 3) //时钟模式
  {
    display.powerOff();
    RTC_clockTime2 = millis() - clockTime1;
    if (clockTime3 != 0)RTC_clockTime2 += clockTime3 - 100;
    ESP.rtcUserMemoryWrite(RTCdz_clockTime2, &RTC_clockTime2, sizeof(RTC_clockTime2));
  }
  else display.hibernate(); //关闭电源屏幕并进入深度睡眠 display.powerOff()为仅关闭电源
  //查看堆碎片
  //Serial.print("堆碎片度量："); Serial.println(ESP.getHeapFragmentation());
  //Serial.print("可用堆大小："); Serial.println(ESP.getFreeHeap());
  Serial.end();
  ESP.deepSleep(minutes * 1000, WAKE_RF_DEFAULT); //WAKE_RF_DEFAULT  WAKE_RFCAL  WAKE_NO_RFCAL  WAKE_RF_DISABLED RF_DISABLED
}
void WifiShutdown() //正真的关闭WIFI
{
  WiFi.mode(WIFI_OFF);
  WiFi.shutdown(state);
}
void not_updated_at_night() //夜间不更新
{
  if (eepUserSet.runMode != 1)return;  // 不是天气模式 退出
  if (eepUserSet.nightUpdata)return;   // 1-夜间更新 0-夜间不更新 esp_sleep(3600000);
  ESP.rtcUserMemoryRead(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
  if (RTC_hour < 6) //小于早上6点继续休眠
  {
    RTC_hour++;
    ESP.rtcUserMemoryWrite(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
    esp_sleep(3600000);
  }
}

boolean szxp_state; //时钟芯片是否存在
boolean clock_hw_ok()
{
  if (szxp_state) //跳过校准 szxp_state
  {
    getClockChipToRTC();  //获取时钟芯片数据并赋值至RTC数据区
    RTC_clock_code = 200;
    RTC_8025T_error = 0;
    ESP.rtcUserMemoryWrite(RTCdz_tm_error, &RTC_8025T_error, sizeof(RTC_8025T_error));
    ESP.rtcUserMemoryWrite(RTCdz_clock_code, &RTC_clock_code, sizeof(RTC_clock_code));
    RTC_ntpTimeError = 0; //获取成功
    display.init(0, 0, 10, 1);
    return 1;
  }
  else return 0;
}
void read_RTC_time() //时间模式初始化
{
  ESP.rtcUserMemoryRead(RTCdz_minute, &RTC_minute, sizeof(RTC_minute));
  ESP.rtcUserMemoryRead(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
  ESP.rtcUserMemoryRead(RTCdz_seconds, &RTC_seconds, sizeof(RTC_seconds));
  ESP.rtcUserMemoryRead(RTCdz_cq_cuont, &RTC_cq_cuont, sizeof(RTC_cq_cuont));
  ESP.rtcUserMemoryRead(RTCdz_clockTime2, &RTC_clockTime2, sizeof(RTC_clockTime2));
  ESP.rtcUserMemoryRead(RTCdz_wifi_ljcs, &RTC_wifi_ljcs, sizeof(RTC_wifi_ljcs));
  ESP.rtcUserMemoryRead(RTCdz_jsjs, &RTC_jsjs, sizeof(RTC_jsjs));
  ESP.rtcUserMemoryRead(RTCdz_ntpTimeError, &RTC_ntpTimeError, sizeof(RTC_ntpTimeError));
  ESP.rtcUserMemoryRead(RTCdz_clock_code, &RTC_clock_code, sizeof(RTC_clock_code));
  ESP.rtcUserMemoryRead(RTCdz_Bfen_code, &RTC_Bfen_code, sizeof(RTC_Bfen_code));
  ESP.rtcUserMemoryRead(RTCdz_follower, &RTC_follower, sizeof(RTC_follower));
  ESP.rtcUserMemoryRead(RTCdz_tm_error, &RTC_8025T_error, sizeof(RTC_8025T_error));
  //数值限制
  if (RTC_wifi_ljcs > 2) RTC_wifi_ljcs = 0;                    // wifi连接超时次数 超时3次永久休眠
  if (RTC_clockTime2 > 1000000) RTC_clockTime2 = 0;            // 上一次运行时间
  if (RTC_cq_cuont >= eepUserSet.clockJZJG) RTC_cq_cuont = 0;  // 重启的计数 每XX次联网校准一次时间
  if (RTC_jsjs > eepUserSet.clockQSJG) RTC_jsjs = 0;           // 局刷计数 每XX次局刷全刷一次
  if (RTC_ntpTimeError > 4) RTC_ntpTimeError = 0;
  // RCT_ntp错误类型 0-正常 1-首次获取网络时间失败 2-每15分钟的获取网络时间失败 3-每15分钟的wifi连接超时 4-本次校准成功

  RTC_cq_cuont++;//重启计数加1
  ESP.rtcUserMemoryWrite(RTCdz_cq_cuont, &RTC_cq_cuont, sizeof(RTC_cq_cuont));

  if (RTC_minute >= 60 || RTC_hour >= 24) // 首次开机，需要校准时间
  {
    BW_refresh();//黑白全屏刷新一次
    //Serial.println("时钟模式：首次校准"); Serial.println(" ");
    display_partialLine(5, "时钟模式，联网校准中");
    szxp_state = ClockReadCheck(); //时钟芯片的数据是否正确

    if (szxp_state) //检查时钟芯片是否存在
    {
      //按下此外部中断 ClockSkipState会置1
      attachInterrupt(digitalPinToInterrupt(key_yb), ClockSkip, FALLING);//设置中断号、响应函数、触发方式
      display_partialLine(7, "此时 按下按键3 可跳过校准"); //ClockSkipState == 1时 跳过校准
    }

    connectToWifi(); //初始化wifi
    uint8_t wifi_count = 0;
    while (!WiFi.isConnected()) // 尝试进行wifi连接。
    {
      if (szxp_state && ClockSkipState)
      {
        if (clock_hw_ok()) //检查硬件时钟，跳过校准
        {
          display_partialLine(1, "手动跳过校准");
          delay(500);
          return;
        }
      }
      delay(900);
      Serial.print(".");
      wifi_count++;
      display_partialLine(0, String(wifi_count) + "  " + "连接WIFI: " + WiFi.SSID() + " 电压: " + String(getBatVolNew()));
      if (RTC_wifi_ljcs != 0) display_partialLine(6, "WIFI连接超时: " + String(RTC_wifi_ljcs) + "次，至多尝试2次");
      if (wifi_count > 15)
      {
        RTC_wifi_ljcs += 1;
        ESP.rtcUserMemoryWrite(RTCdz_wifi_ljcs, &RTC_wifi_ljcs, sizeof(RTC_wifi_ljcs));
        display_partialLine(6, "WIFI连接超时: " + String(RTC_wifi_ljcs) + "次，至多尝试2次");
        if (RTC_wifi_ljcs >= 2)
        {
          if (clock_hw_ok()) //检查硬件时钟，跳过校准
          {
            display_partialLine(1, "连接超时:" + WiFi.SSID() + "，跳过校准");
            delay(500);
            return;
          }
          else display_bitmap_sleep("* 连接超时 *"); //进入休眠
        }
        ESP.reset();
      }
    }
    display_partialLine(7, " ");
    display_partialLine(1, "连接成功 " + WiFi.localIP().toString());
    //获取日期 时钟显示模式 极简&显示日期
    if (eepUserSet.clock_display_state)
    {
      display_partialLine(2, "获取日期");
      if (ParseRiQi(callHttps("https://api.xygeng.cn/day"), &riqi))
      {
        RTC_clock_code = 200; // 错误代码
        ESP.rtcUserMemoryWrite(RTCdz_clock_code, &RTC_clock_code, sizeof(RTC_clock_code));
        strcpy(eepUserClock.year, riqi.year);         // 年
        strcpy(eepUserClock.month, riqi.month);       // 月
        strcpy(eepUserClock.day, riqi.date);          // 日
        strcpy(eepUserClock.week, riqi.day);          // 星期几
        strcpy(eepUserClock.festival, riqi.festival); // 节日名
        EEPROM.put(eeprom_address1, eepUserClock);
        EEPROM.commit(); //保存
        EEPROM.get(eeprom_address1, eepUserClock);
        display_partialLine(2, "获取日期：成功");
      }
      else display_partialLine(2, "获取日期：失败 " + String(riqi.code));
    }

    if (eepUserSet.inAWord_mod == 3) //B站粉丝
    {
      uint8_t cs_count = 0;
      //拼装B站粉丝API地址
      String BUID = "";
      for (uint8_t i = 4; i < strlen(eepUserSet.inAWord); i++) //获取B站UID
        BUID += eepUserSet.inAWord[i];
      String url_Bfen = "http://api.bilibili.com/x/relation/stat?vmid=" + BUID + "&jsonp=jsonp";
      display_partialLine(2, "获取B站粉丝：" + String(cs_count));
      while (ParseBliBli(callHttp(url_Bfen)) == 0 && cs_count < 2)
      {
        cs_count++;
      }
      if (cs_count >= 2)display_partialLine(2, "获取B站粉丝：失败");
      else display_partialLine(2, "获取B站粉丝：成功");
    }

    uint8_t update_count = 0;
    timeClient.begin();
    while (timeClient.update() == 0 && update_count < 6)
    {
      display_partialLine(2, "获取网络时间:" + String(update_count));
      update_count++;
      //Serial.print("timeClient.update()重试计数："); Serial.println(update_count);
      delay(1000);
      if (update_count == 2)      timeClient.setPoolServerName("s2k.time.edu.cn");
      else if (update_count == 3) timeClient.setPoolServerName("1d.time.edu.cn");
      else if (update_count == 4) timeClient.setPoolServerName("s1c.time.edu.cn");
      else if (update_count == 5) timeClient.setPoolServerName("ntp.sjtu.edu.cn");
    }
    if (update_count >= 6)
    {
      RTC_ntpTimeError = 1; //首次获取网络时间失败
      display_partialLine(0, "时钟模式");
      display_partialLine(1, "获取网络时间:失败");
      if (clock_hw_ok()) //检查硬件时钟，跳过校准
      {
        display_partialLine(2, "跳过校准");
        delay(500);
        return;
      }
      else display_bitmap_sleep(" ");
    }
    else
    {
      RTC_cq_cuont = 0;
      ESP.rtcUserMemoryWrite(RTCdz_cq_cuont, &RTC_cq_cuont, sizeof(RTC_cq_cuont));

      RTC_ntpTimeError = 4; //首次开机校准时间成功
      display_partialLine(2, "获取网络时间:成功");

      RTC_hour = timeClient.getHours();
      RTC_minute = timeClient.getMinutes();
      RTC_seconds = timeClient.getSeconds();
      clockTime0 = millis();

      ESP.rtcUserMemoryWrite(RTCdz_minute, &RTC_minute, sizeof(RTC_minute));
      ESP.rtcUserMemoryWrite(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
      ESP.rtcUserMemoryWrite(RTCdz_seconds, &RTC_seconds, sizeof(RTC_seconds));

      //将时间数据写入BL8025T时钟芯片并检查数据完整性
      tmElements_t tm;
      tm.Year = CalendarYrToTm(atoi(eepUserClock.year));
      tm.Month = atoi(eepUserClock.month);
      tm.Day = atoi(eepUserClock.day);
      tm.Hour = RTC_hour;
      tm.Minute = RTC_minute;
      tm.Second = RTC_seconds;
      if (tmYearToCalendar(tm.Year) < 2020 || tmYearToCalendar(tm.Year) > 2099) RTC_8025T_error = 1;
      else if (tm.Month <= 0 || tm.Month > 12) RTC_8025T_error = 2;
      else if (tm.Day <= 0 || tm.Day > 31) RTC_8025T_error = 3;
      else if (tm.Hour < 0 || tm.Hour > 24) RTC_8025T_error = 4;
      else if (tm.Minute < 0 || tm.Minute > 60) RTC_8025T_error = 5;
      else if (tm.Second < 0 || tm.Second > 60) RTC_8025T_error = 6;
      else RTC_8025T_error = 0;
      if (RTC_8025T_error == 0)
      {
        Wire.begin(13, 14); // 初始化I2C引脚
        rtc.write(tm);      // 写入时钟芯片数据
        display.init(0, 0, 10, 1);
        display_partialLine(3, ">>> 检测时钟芯片中 <<<");
      }
      else
      {
        display_partialLine(3, "网络时间出错：" + String(RTC_8025T_error));
        Serial.print("网络时间出错-RTC_8025T_error:"); Serial.println(RTC_8025T_error);
      }
      //检查BL8025T时钟芯片读取数据是否有错
      if (ClockReadCheck())
      {
        RTC_8025T_error = 0;
        display_partialLine(3, ">>> 时钟芯片：读取数据正常 <<<");
        delay(500);
      }
      else
      {
        RTC_8025T_error = 1;
        display_partialLine(3, ">>> 时钟芯片：读取数据出错或不存在 <<<");
        delay(500);
        display_partialLine(3, ">>> 使用软件计算 <<<");
        delay(500);
      }
      ESP.rtcUserMemoryWrite(RTCdz_tm_error, &RTC_8025T_error, sizeof(RTC_8025T_error));
      clockTime0 = millis();
    }
  }
  else if ((RTC_cq_cuont >= eepUserSet.clockJZJG) && RTC_8025T_error != 0) //第XX次重启了，需要校准时间了,若有时钟芯片的话不需要定时校准
  {
    //Serial.println("时钟模式：15分钟/次校准"); Serial.println(" ");
    connectToWifi();
    uint8_t wifi_count = 0;
    while (!WiFi.isConnected() && wifi_count <= 10)   // 尝试进行wifi连接。
    {
      delay(900);
      wifi_count++;
      if (wifi_count > 10) RTC_ntpTimeError = 3;      //提示错误 wifi连接超时
    }
    if (WiFi.isConnected()) //wifi连接成功，连接NTP服务器获取时间
    {
      uint8_t update_count = 0;
      timeClient.begin();
      //尝试两次
      boolean ntpState = timeClient.update();
      if (ntpState == 0) {
        delay(1000);
        ntpState = timeClient.update();
      }
      if (ntpState == 0) RTC_ntpTimeError = 2; //ntp时间服务器连接失败提示
      else //ntp时间服务器连接成功
      {
        RTC_ntpTimeError = 4;   //定时校准时间成功
        RTC_minute = timeClient.getMinutes();
        RTC_hour = timeClient.getHours();
        RTC_seconds = timeClient.getSeconds();
        clockTime0 = millis();
        ESP.rtcUserMemoryWrite(RTCdz_minute, &RTC_minute, sizeof(RTC_minute));
        ESP.rtcUserMemoryWrite(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
        ESP.rtcUserMemoryWrite(RTCdz_seconds, &RTC_seconds, sizeof(RTC_seconds));
      }
    }
  }
  else if (RTC_hour == 23 && RTC_minute == 59) //如果没开启强制校准时，到了0点0分也静默联网获取
  {
    // 读取时钟芯片数据保存到eeprom
    strcpy(eepUserClock.festival, String("").c_str());
    EEPROM.put(eeprom_address1, eepUserClock);
    EEPROM.commit(); //保存

    RTC_cq_cuont = 0;
    connectToWifi();
    uint8_t wifi_count = 0;
    while (!WiFi.isConnected() && wifi_count <= 10)   // 尝试进行wifi连接。
    {
      delay(900);
      wifi_count++;
      if (wifi_count > 10) RTC_ntpTimeError = 3;      //提示错误 wifi连接超时
    }
    if (WiFi.isConnected()) //wifi连接成功，连接NTP服务器获取时间
    {
      uint8_t update_count = 0;
      timeClient.begin();
      //尝试两次
      boolean ntpState = timeClient.update();
      if (ntpState == 0) {
        delay(1000);
        ntpState = timeClient.update();
      }
      if (ntpState == 0) RTC_ntpTimeError = 2; //ntp时间服务器连接失败提示
      else //ntp时间服务器连接成功
      {
        RTC_ntpTimeError = 4; //定时校准时间成功
        RTC_minute = timeClient.getMinutes();
        RTC_hour = timeClient.getHours();
        RTC_seconds = timeClient.getSeconds();
        clockTime0 = millis();
        ESP.rtcUserMemoryWrite(RTCdz_minute, &RTC_minute, sizeof(RTC_minute));
        ESP.rtcUserMemoryWrite(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
        ESP.rtcUserMemoryWrite(RTCdz_seconds, &RTC_seconds, sizeof(RTC_seconds));
        if (eepUserSet.clock_display_state) //不是极简模式 需要获取日期
        {
          if (ParseRiQi(callHttps("https://api.xygeng.cn/day"), &riqi))
          {
            RTC_clock_code = 200; // 错误代码
            ESP.rtcUserMemoryWrite(RTCdz_clock_code, &RTC_clock_code, sizeof(RTC_clock_code));
            strcpy(eepUserClock.year, riqi.year);         // 年
            strcpy(eepUserClock.month, riqi.month);       // 月
            strcpy(eepUserClock.day, riqi.date);         // 日
            strcpy(eepUserClock.week, riqi.day);           // 星期几
            strcpy(eepUserClock.festival, riqi.festival); // 节日名
            EEPROM.put(eeprom_address1, eepUserClock);
            EEPROM.commit(); //保存
            EEPROM.get(eeprom_address1, eepUserClock);
          }
        }
        if (RTC_8025T_error == 0)
        {
          tmElements_t tm;
          tm.Year = CalendarYrToTm(atoi(eepUserClock.year));
          tm.Month = atoi(eepUserClock.month);
          tm.Day = atoi(eepUserClock.day);
          tm.Hour = RTC_hour;
          tm.Minute = RTC_minute;
          tm.Second = RTC_seconds;
          Wire.begin(13, 14); // 初始化I2C引脚
          rtc.write(tm);      // 写入时钟芯片数据
          display.init(0, 0, 10, 1);
        }
      }
      if (eepUserSet.inAWord_mod == 3) //B站粉丝
      {
        uint8_t cs_count = 0;
        String BUID = "";
        for (uint8_t i = 4; i < strlen(eepUserSet.inAWord); i++) //获取B站UID
          BUID += eepUserSet.inAWord[i];
        String url_Bfen = "http://api.bilibili.com/x/relation/stat?vmid=" + BUID + "&jsonp=jsonp";
        while (ParseBliBli(callHttp(url_Bfen)) == 0 && cs_count < 2) cs_count++;
      }
    }
  }

  //有时钟芯片时
  if (RTC_8025T_error == 0) //将时钟芯片的时间赋值到系统数据
  {
    if (RTC_ntpTimeError == 3 || RTC_ntpTimeError == 2) //校准失败，计算手动补偿值
    {
      ManualCompensation8025T(); //8025T手动补偿算法
    }
    getClockChipToRTC();  //获取时钟芯片数据并赋值至RTC数据区
    RTC_ntpTimeError = 0; //获取成功
  }
  //无时钟芯片时
  //没到校准时间 或 wifi连接失败 或 wifi连接成功但无法连接NTP，则自己计算时间 RTC_ntpTimeError=4表示本次校准过了
  else if (RTC_ntpTimeError == 3 || RTC_ntpTimeError == 2 || (RTC_cq_cuont < eepUserSet.clockJZJG && RTC_ntpTimeError == 0))
  {
    //Serial.println("时钟模式：自己计算时间"); Serial.println(" ");
    //WiFi.mode(WIFI_OFF);
    RTC_minute += 1;
    if (RTC_minute >= 60)
    {
      RTC_hour += 1;
      RTC_minute = 0;
    }
    if (eepUserSet.clock_calibration_state == 0) //强制校准是否开启 1-开启 0-不开启
    {
      if (RTC_hour >= 24)
      {
        RTC_hour = 0;
        //非极简模式下将自动计算日期
        if (eepUserSet.clock_display_state) riqi_js();
      }
    }
    ESP.rtcUserMemoryWrite(RTCdz_minute, &RTC_minute, sizeof(RTC_minute));
    ESP.rtcUserMemoryWrite(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
    if (RTC_seconds != 0)
    {
      RTC_seconds = 0;
      ESP.rtcUserMemoryWrite(RTCdz_seconds, &RTC_seconds, sizeof(RTC_seconds));
    }
  }
  else if (RTC_ntpTimeError == 4) {
    RTC_ntpTimeError = 0; //如校准时间正常，下一次将自己计算时间
  }
  ESP.rtcUserMemoryWrite(RTCdz_ntpTimeError, &RTC_ntpTimeError, sizeof(RTC_ntpTimeError));
  /*Serial.print("RTC_hour:"); Serial.println(RTC_hour);
    Serial.print("RTC_minute:"); Serial.println(RTC_minute);
    Serial.print("RTC_seconds:"); Serial.println(RTC_seconds);
    Serial.print("RTC_cq_cuont:"); Serial.println(RTC_cq_cuont);
    Serial.println(" ");*/
}

#define dc 0
#define cs 15
void EPD_writeCommand(uint8_t c)
{
  SPI.beginTransaction(spi_settings);
  if (dc >= 0) digitalWrite(dc, LOW);  //dc
  if (cs >= 0) digitalWrite(cs, LOW);  //cs
  SPI.transfer(c);
  if (dc >= 0) digitalWrite(dc, HIGH);   //dc
  if (cs >= 0) digitalWrite(cs, HIGH);   //cs
  SPI.endTransaction();
}

void EPD_writeData(uint8_t d)
{
  SPI.beginTransaction(spi_settings);
  if (cs >= 0) digitalWrite(cs, LOW); //cs
  SPI.transfer(d);
  if (cs >= 0) digitalWrite(cs, HIGH); //cs
  SPI.endTransaction();
}
void xiaobian() //消除黑边（四周的边跟随屏幕刷新，仅全局刷新有效）
{
  EPD_writeCommand(0x3c);  // 边界波形控制寄存器
  EPD_writeData(0x33);     // 向里面写入数据

  //EPD_writeCommand(0x2c); // VCOM setting
  //EPD_writeData(0xA1);    // * different   FPC丝印A1 库默认A8
}

String byteConversion(size_t zijie) //字节换算
{
  String zijie_str;
  if (zijie >= 1073741824)
  {
    zijie_str = String(float(zijie) / 1073741824.0) + " GB";
  }
  if (zijie >= 1048576) {
    zijie_str = String(float(zijie) / 1048576.0) + " MB";
  }
  else if (zijie >= 1024) {
    zijie_str = String(float(zijie) / 1024.0) + " KB";
  }
  else zijie_str = String(zijie) + " B";
  return zijie_str;
}

//计算今天是周几的公式  d为几号，m为月份，y为年份
String week_calculate(int y, int m, int d)
{
  if (m == 1 || m == 2)  //一二月换算
  {
    m += 12;
    y--;
  }
  int week = (d + 2 * m + 3 * (m + 1) / 5 + y + y / 4 - y / 100 + y / 400 + 1) % 7;
  if (week == 1) return"周一";
  else if (week == 2 ) return"周二";
  else if (week == 3 ) return"周三";
  else if (week == 4 ) return"周四";
  else if (week == 5 ) return"周五";
  else if (week == 6 ) return"周六";
  else if (week == 0 ) return"周日";
  else return "计算出错";
  //其中1~6表示周一到周六 0表示星期天
}

void riqi_js() //日期自加计算
{
  //1.先将char[]转换成int
  //2.进行日期自加算法计算
  //3.将int转换成String，使用atoi将String转换成char[]
  //4.保存至eeprom中
  uint16_t nian_i = atoi(eepUserClock.year);
  uint8_t yue_i = atoi(eepUserClock.month);
  uint8_t ri_i = atoi(eepUserClock.day);
  /*Serial.print("nian_i:"); Serial.println(nian_i);
    Serial.print("yue_i:"); Serial.println(yue_i);
    Serial.print("ri_i:"); Serial.println(ri_i);*/
  ri_i++; //日加
  //有31天的月份
  if (yue_i == 1 || yue_i == 3 || yue_i == 5 || yue_i == 7 || yue_i == 8 || yue_i == 10 || yue_i == 12)
  {
    if (ri_i > 31)
    {
      yue_i++;
      ri_i = 1;
      if (yue_i > 12)
      {
        nian_i++;
        yue_i = 1;
      }
    }
  }
  //有30天的月份
  else if (yue_i == 4 || yue_i == 6 || yue_i == 9 || yue_i == 11)
  {
    if (ri_i > 30)
    {
      yue_i++;
      ri_i = 1;
      if (yue_i > 12)
      {
        nian_i++;
        yue_i = 1;
      }
    }
  }
  //有29-28天的月份
  else if (yue_i == 2)
  {
    if ((nian_i % 4 == 0 && nian_i % 100 != 0) || nian_i % 400 == 0) //闰年29天
    {
      if (ri_i > 29)
      {
        yue_i++;
        ri_i = 1;
        if (yue_i > 12)
        {
          nian_i++;
          yue_i = 1;
        }
      }
    }
    else //平年28天
    {
      if (ri_i > 28)
      {
        yue_i++;
        ri_i = 1;
        if (yue_i > 12)
        {
          nian_i++;
          yue_i = 1;
        }
      }
    }
  }

  // 计算今日是星期几
  String xqj = week_calculate(nian_i, yue_i, ri_i);
  // String复制到char
  strcpy(eepUserClock.week, xqj.c_str());              // 星期几
  strcpy(eepUserClock.year, String(nian_i).c_str());   // 年
  strcpy(eepUserClock.month, String(yue_i).c_str());   // 月
  strcpy(eepUserClock.day, String(ri_i).c_str());      // 日
  strcpy(eepUserClock.festival, String("").c_str());   // 节日名
  // 写入eeprom
  EEPROM.put(eeprom_address1, eepUserClock);
  EEPROM.commit(); //保存
}

String inputBoxPatternRecognition(String myStr) //多功能输入框模式识别
{
  //中文3个字节 ASCII扩展2个字节
  //对设定值的字符串格式进行识别、转换
  uint16_t myStr_length = myStr.length();
  //Serial.print("myStr_length:"); Serial.println(myStr_length);
  uint8_t kg_conut = 0;    // 无空格的地方
  String szjc_dao = "";    // 首字检测-倒
  String szjc_Bfen = "";   // 首字检测-B粉

  while (myStr[kg_conut] == ' ') //开头空格检查
  {
    kg_conut++;
  }
  if (kg_conut >= 63 || kg_conut == myStr_length)
  {
    eepUserSet.inAWord_mod = 0;
    return "全是空格";
  }

  if (kg_conut > 0) //去除开头的空格
  {
    String qkgStr = "";
    for (uint8_t i = kg_conut; i < myStr_length; i++)
    {
      qkgStr += myStr[i];
    }
    myStr = qkgStr;
  }
  for (uint8_t i = 0; i < 3; i++) //倒计时模式检测
  {
    szjc_dao += myStr[i];
  }
  for (uint8_t i = 0; i < 4; i++) //B粉模式检测
  {
    szjc_Bfen += myStr[i];
  }
  if (szjc_dao == "倒") //检测到有 "倒" 字
  {
    if (myStr_length >= 8)  //并且长度大于等8 如：倒0607 有7个字节，再加上事件就有8个字节以上
    {
      uint8_t sz_conut = 0;
      for (uint8_t j = 0; j < 4; j++) //再检查"倒" 字后面是否有4个数字
      {
        if (myStr[3 + j] >= 48 && myStr[3 + j] <= 57) sz_conut++;
        if (sz_conut == 4)
        {
          uint8_t yue = 0;  //目标月
          uint8_t ri = 0;   //目标日
          uint16_t nian = atoi(eepUserClock.year);
          yue = (myStr[3] - 48) * 10 + (myStr[4] - 48);
          ri = (myStr[5] - 48) * 10 + (myStr[6] - 48);
          if (yue > 12)
          {
            eepUserSet.inAWord_mod = 0;
            return "没有" + String(yue) + "月";
          }
          else if (nian > 2000 && ri > getDaySum(nian, yue))
          {
            eepUserSet.inAWord_mod = 0;
            return String(yue) + "月没有" + String(ri) + "日";
          }
          eepUserSet.inAWord_mod = 2;
        }
        else eepUserSet.inAWord_mod = 1;
      }
    }
    else eepUserSet.inAWord_mod = 1;
  }
  else if (szjc_Bfen == "B粉")
  {
    if (myStr_length >= 8)  //并且长度大于等8 如：B粉1234 有8个字节
    {
      uint8_t sz_conut = 0;
      for (uint8_t j = 0; j < 4; j++) //再检查"B粉" 字后面是否有4个数字
      {
        if (myStr[4 + j] >= 48 && myStr[4 + j] <= 57) sz_conut++;
        if (sz_conut == 4)eepUserSet.inAWord_mod = 3;
        else eepUserSet.inAWord_mod = 1;
      }
    }
    else eepUserSet.inAWord_mod = 1;
  }
  else eepUserSet.inAWord_mod = 1;

  if (eepUserSet.inAWord_mod != 0) return myStr;
  return "错误inputBoxPatternRecognition";
}

String daysCalculation(const char* myChar, uint16_t dq_nian, uint8_t dq_yue, uint8_t dq_ri) //天数倒计时
{
  String myStr = myChar;
  /*Serial.print("myStr:"); Serial.println(myStr);
    Serial.print("dq_nian:"); Serial.println(dq_nian);
    Serial.print("dq_yue:"); Serial.println(dq_yue);
    Serial.print("dq_ri:"); Serial.println(dq_ri);*/
  uint8_t yue = 0;  //目标月
  uint8_t ri = 0;   //目标日
  uint8_t myStr_length = myStr.length(); //strlen ri0_i = atoi(ri0.c_str());
  yue = (myStr[3] - 48) * 10 + (myStr[4] - 48);
  ri = (myStr[5] - 48) * 10 + (myStr[6] - 48);
  String shijian = "";
  for (uint8_t i = 7; i < myStr_length; i++) //获取事件
  {
    shijian += myStr[i];
  }
  //shijian += "（" + String(yue) + "月" + String(ri) + "日" + "）";
  /*Serial.print("myStr[3]:"); Serial.println(myStr[3]);
    Serial.print("myStr[4]:"); Serial.println(myStr[4]);
    Serial.print("myStr[5]:"); Serial.println(myStr[5]);
    Serial.print("myStr[6]:"); Serial.println(myStr[6]);
    Serial.print("yue:"); Serial.println(yue);
    Serial.print("ri:"); Serial.println(ri);
    Serial.print("shijian:"); Serial.println(shijian);*/
  if (yue == dq_yue && ri == dq_ri) //今日会发生
  {
    return "今天 " + shijian;
  }
  else if (yue == dq_yue && ri > dq_ri) //本月会发生，目标日大于当前日
  {
    uint16_t tongji = ri - dq_ri;
    return String(tongji) + "天后 " + shijian;
  }
  else if (yue == dq_yue && ri < dq_ri) //本月已经发生，目标日小于当前日
  {
    uint16_t tongji = dq_ri - ri;
    return String(tongji) + "天前 " + shijian;
  }
  else if (yue > dq_yue) //未来会发生 XX天后
  {
    //计算当前月剩余的天数
    uint16_t tongji = getDaySum(dq_nian, dq_yue) - dq_ri;
    //计算未来月的天数
    while (yue != dq_yue)
    {
      dq_yue++;
      if (yue == dq_yue) tongji += ri;
      else tongji += getDaySum(dq_nian, dq_yue);
    }
    return String(tongji) + "天后 " + shijian;
  }
  else if (yue < dq_yue) //已近发生了 XX天前
  {
    //计算当前月剩余的天数
    uint16_t tongji = dq_ri;
    while (yue != dq_yue)
    {
      dq_yue--;
      if (yue == dq_yue) tongji += getDaySum(dq_nian, dq_yue) - ri;
      else tongji += getDaySum(dq_nian, dq_yue);
    }
    return String(tongji) + "天前 " + shijian;
  }
  return "错误daysCalculation";
}

uint8_t getDaySum(uint16_t nian, uint8_t yue) //获取本月有多少天
{
  if (yue == 1 || yue == 3 || yue == 5 || yue == 7 || yue == 8 || yue == 10 || yue == 12) return 31;
  else if (yue == 2)
  {
    if ((nian % 4 == 0 && nian % 100 != 0) || nian % 400 == 0) //闰年29天
    {
      return 29;
    }
    else return 28; //平年29天
  }
  else return 30;
}
/*void eeprom_commitReset(boolean *state) // 首次保存覆盖掉旧的数值
  {
  if (state > 1) state = 1;
  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commitReset(); // 首次保存覆盖掉旧的数值
  }*/
/*使用ESP.restart()可以软复位系统；

  RTC存储区使用
  使用ESP.rtcUserMemoryWrite(offset, &data, sizeof(data))可以向RTC存储区写数据；
  使用ESP.rtcUserMemoryRead(offset, &data, sizeof(data))可以从RTC存储区读数据；
  RTC存储区共支持128个4字节的数据（即总共可存储512字节内容），地址offset取值为0 ~ 127；

  RTC存储区在系统复位时（非重新上电及固件上传）数据保持不变


  //启动WIFI省电功能
  //uint8_t i = WiFi.setSleepMode(WIFI_MODEM_SLEEP); //WIFI_LIGHT_SLEEP    WIFI_MODEM_SLEEP
  //Serial.print("省电模式："); Serial.println(WiFi.setSleepMode(WIFI_MODEM_SLEEP, 100)); Serial.println(" ");
*/
