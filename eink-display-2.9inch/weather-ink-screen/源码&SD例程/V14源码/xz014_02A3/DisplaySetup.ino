//连接wifi的显示界面
#define cg_y1 112
#define cg_y2 cg_y1+13
#define cg_x1 40

void FixedRefresh() //定次刷新,每5次刷黑白一次，每15次正式全局刷黑白
{
  //每6次快速全屏刷新一次，第30次完全全屏刷新
  display.init(0, 0, 10, 0);   //串口使能 初始化完全刷新使能 复位时间 ret上拉使能
  if (pageUpdataCount == 30)  //第30次完全刷新
  {
    display.init(0, 1, 10, 0);
    pageUpdataCount = 0;
  }
  else if (pageUpdataCount % 6 == 0)  //每6次黑白刷新一次
  {
    display.fillScreen(heise);  // 填充屏幕
    display.display(1);         // 显示缓冲内容到屏幕，用于全屏缓冲
    display.fillScreen(baise);
    display.display(1);
  }
  else
  {
    display.fillScreen(baise);
    if (PageChange != 0 && eepUserSet.fastFlip == 0) display.display(1);//快速翻页关闭
  }
  pageUpdataCount++;
}

void display_partialLineNoCenter(uint8_t line, String zf) //发送局部刷新的显示信息到屏幕,不带居中
{
  display.setPartialWindow(0, line * 16, display.width(), 16);  //整行刷新
  display.firstPage();
  do
  {
    u8g2Fonts.setCursor(2, line * 16 + 13);
    u8g2Fonts.print(zf);
  }
  while (display.nextPage());
  //display.powerOff(); //关闭屏幕电源
}

void display_partialLine(uint8_t line, String zf) ////发送局部刷新的显示信息到屏幕,带居中
{
  /*
    display_partialLine()
    发送局部刷新的显示信息到屏幕,带居中

    line        行数（0-7）
    zf          字符内容
    lineRefresh 整行刷新 1-是 0-仅刷新字符长度的区域
  */
  //u8g2Fonts.setFont(chinese_gb2312);
  const char *character = zf.c_str();                            //String转换char
  uint16_t zf_width = u8g2Fonts.getUTF8Width(character);         //获取字符的像素长度
  uint16_t x = (display.width() / 2) - (zf_width / 2);           //计算字符居中的X坐标（屏幕宽度/2-字符宽度/2）
  display.setPartialWindow(0, line * 16, display.width(), 16);   //整行刷新
  display.firstPage();
  do
  {
    u8g2Fonts.setCursor(x, line * 16 + 13);
    u8g2Fonts.print(character);
  }
  while (display.nextPage());
  //display.powerOff(); //关闭屏幕电源
}

void display_Bitmap_Setup() //显示开机图片
{
  //display.setPartialWindow(0, 0, display.width(), display.height()); //设置局部刷新窗口
  display.setFullWindow(); //设置全屏刷新 height
  display.firstPage();
  do
  {
    display.drawInvertedBitmap(0, 0, Bitmap_567, 296, 128, heise); //显示开机图片
  }
  while (display.nextPage());
  //display.powerOff(); //关闭屏幕电源
}

void display_bitmap_bottom(const unsigned char* bitmaps, String z) //显示开机图片,并在底部显示文字
{
  u8g2Fonts.setFontMode(0);
  const char* zhifu = z.c_str();                       //String转char
  uint16_t zf_width = u8g2Fonts.getUTF8Width(zhifu);   //获取字符的像素长度
  uint16_t x = (display.width() / 2) - (zf_width / 2); //计算字符居中的X位置
  display.setPartialWindow(0, 0, display.width(), display.height()); //设置局部刷新窗口
  display.firstPage();
  do
  {
    //display.fillScreen(baise);
    display.drawInvertedBitmap(0, 0, bitmaps, 296, 128, heise); //显示开机图片
    u8g2Fonts.setCursor(x, cg_y2);
    u8g2Fonts.print(zhifu);
  }
  while (display.nextPage());
  //display.powerOff(); //关闭屏幕电源
}

void display_bitmap_sleep(String z) //显示休眠图标，并在底下显示原因
{
  u8g2Fonts.setFont(chinese_gb2312);
  display.init(0, 0, 10, 0);
  u8g2Fonts.setFontMode(0);
  const char* zhifu = z.c_str();                       //String转char
  uint16_t zf_width = u8g2Fonts.getUTF8Width(zhifu);   //获取字符的像素长度
  uint16_t x = (display.width() / 2) - (zf_width / 2); //计算字符居中的X位置
  display.setPartialWindow(77, 32, 141, 64);           //设置局部刷新窗口
  display.firstPage();
  do
  {
    display.drawInvertedBitmap(77, 32, Bitmap_yxm_yzq_50, 141, 48, heise); //显示休眠图标
    u8g2Fonts.setCursor(x, 96 - 2);
    u8g2Fonts.print(zhifu);
  }
  while (display.nextPage());
  esp_sleep(0);
}

uint16_t getCenterX(String z) //计算字符居中的X位置
{
  uint16_t zf_width = u8g2Fonts.getUTF8Width(z.c_str());  //获取字符的像素长度
  uint16_t x = (display.width() / 2) - (zf_width / 2);    //计算字符居中的X位置
  return x;
}
uint16_t getCenterX(const char* z) //计算字符居中的X位置
{
  uint16_t zf_width = u8g2Fonts.getUTF8Width(z);        //获取字符的像素长度
  uint16_t x = (display.width() / 2) - (zf_width / 2);  //计算字符居中的X位置
  return x;
}
void displayShuaBai() //刷白确认
{
  //刷白表示已执行确认操作，并进入while等等按键释放
  display.init(0, 0, 10, 0);
  display.fillScreen(baise);
  display.display(1);
  u8g2Fonts.setFont(chinese_gb2312);
  displayUpdate = 1;     //允许更新屏幕
  while (digitalRead(key_yb) == 0) ESP.wdtFeed();
}
void BW_refresh() //黑白刷新一次
{
  display.init(0, 0, 10, 0);
  display.fillScreen(heise);  // 填充屏幕
  display.display(1);         // 显示缓冲内容到屏幕，用于全屏缓冲
  display.fillScreen(baise);
  display.display(1);
}
void display_setup() //天气开机连接画面
{
  //读取天气模式开机显示 0-不显示 其他值-显示
  ESP.rtcUserMemoryRead(RTCdz_tqmskjxs, &RTC_tqmskjxs, sizeof(RTC_tqmskjxs));

  u8g2Fonts.setFont(chinese_gb2312);

  if (RTC_tqmskjxs != 0) display_Bitmap_Setup(); //开机壁纸

  //检查wifi是否有配置到
  if (WiFi.SSID().length() == 0) {
    display_partialLine(7, "未配置WiFi，请到配网模式设置");
    esp_sleep(0);
  }

  uint8_t wifi_count1 = 0; //wifi连接次数
  //Serial.println(WiFi.psk());HFMIS001
  Serial.print("正在连接WIFI:");
  while (!WiFi.isConnected()) //wifiMulti.run() != WL_CONNECTED
  {
    String vcc_zfc = WiFi.SSID() + " 电压:" + String(getBatVolNew()) + "V";
    wifi_count1++;
    delay(1000);
    if (wifi_count1 == 1)
    {
      if (RTC_tqmskjxs != 0) display_partialLine(7, "正在连接WIFI：" + vcc_zfc);
    }
    else if (wifi_count1 == 60)
    {
      if (RTC_tqmskjxs != 0) display_partialLine(7, "连接超时，已休眠！WIFI: " + vcc_zfc);

      display.init(0, 0, 10, 0);
      display.setPartialWindow(0, 80, 26, 24);  //设置局刷范围
      display.firstPage();
      do
      {
        display.drawInvertedBitmap(0, 80, Bitmap_wifidk, 26, 22, heise); //wifi断开
      }
      while (display.nextPage());

      esp_sleep(3600000);
    }
    else if (wifi_count1 % 15 == 0)
    {
      Serial.println("WIFI_OFF");
      WiFi.mode(WIFI_OFF);   //设置工作模式
      delay(1000);
      WiFi.mode(WIFI_STA);   //设置工作模式
      WiFi.begin();  //要连接的WiFi名称和密码
      Serial.println("WIFI_STA");
    }
    Serial.println(wifi_count1);
  }

  if (RTC_tqmskjxs != 0) display_partialLine(7, "OK！" + WiFi.localIP().toString());

  Serial.println(WiFi.localIP().toString());
  Serial.println(" ");
}

void display_clock() //时钟显示界面
{
  //0-正常 1-首次获取网络时间失败 2-每10分钟的wifi连接超时 3-每10分钟的获取网络时间失败

  if (RTC_ntpTimeError == 1) {
    display_partialLine(6, "NTP服务器连接失败");
    display_bitmap_sleep(" ");
    esp_sleep(0);
  }

  //每xx次局刷全刷一次
  if (RTC_jsjs == 0) display.init(0, 1, 10, 0);
  else               display.init(0, 0, 10, 0);
  RTC_jsjs++; //局刷计数
  ESP.rtcUserMemoryWrite(RTCdz_jsjs, &RTC_jsjs, sizeof(RTC_jsjs));
  display.setPartialWindow(0, 0, display.width(), display.height()); //设置局部刷新窗口
  display.firstPage();
  do
  {
    //************************ 时间显示 ************************
    if (eepUserSet.inAWord_mod == 2 || eepUserSet.inAWord_mod == 3)
    {
      u8g2Fonts.setFont(u8g2_font_logisoso78_tn);
      //拼装时间 小时和分,不够两位数需要补0
      String hour, minute, assembleTime;
      if (RTC_hour < 10)   hour = "0" + String(RTC_hour);
      else                 hour = String(RTC_hour);
      if (RTC_minute < 10) minute = "0" + String(RTC_minute);
      else                 minute = String(RTC_minute);
      assembleTime = hour + ":" + minute;

      int8_t sz_x = 0; //显示位置X轴的偏移量
      if (RTC_hour >= 10 && RTC_hour <= 19) sz_x = -3; //10-19点
      else sz_x = 2;
      //显示时间
      u8g2Fonts.setCursor(sz_x, 95);
      u8g2Fonts.print(assembleTime);
      //画一条垂直线
      display.drawLine(230, 7, 230, 103, 0);
    }
    if (eepUserSet.inAWord_mod == 2 && RTC_clock_code == 200) //需要显示天数倒计
    {
      //显示倒计时事件或B粉
      char clock_year[5];   // 年
      char clock_month[3];  // 月
      char clock_day[3];   // 日
      uint16_t nian_i  = atoi(eepUserClock.year);
      uint8_t yue_i  = atoi(eepUserClock.month);
      uint8_t ri_i  = atoi(eepUserClock.day);
      String myStr = daysCalculation(eepUserSet.inAWord, nian_i, yue_i, ri_i);
      //Serial.print("myStr:"); Serial.println(myStr);
      String tianshu = "";  // 天数
      String tqth = "";     // 天前或天后
      String shijian = "";  // 事件
      String shijian_shijian = "";  // 事件里的时间
      //提取eepUserSet.inAWord里的时间
      String myStr1 = String(eepUserSet.inAWord);
      uint8_t shijian_yue = (myStr1[3] - 48) * 10 + (myStr1[4] - 48);
      uint8_t shijian_ri = (myStr1[5] - 48) * 10 + (myStr1[6] - 48);
      shijian_shijian = String(shijian_yue) + "月" + String(shijian_ri) + "日";
      if (myStr[0] >= 48 && myStr[0] <= 57) //判断首字符是否是数字
      {
        //提取天数、天前、天后、事件
        uint8_t count = 0;
        while (myStr[count] >= 48 && myStr[count] <= 57) //检测有几个数字
          count++;
        for (uint8_t i = 0; i < count; i++) //获取天数
          tianshu += myStr[i];
        for (uint8_t i = count; i < count + 6; i++) //获取天前或天后
          tqth += myStr[i];
        for (uint8_t i = count + 7; i < myStr.length(); i++) //获取事件
          shijian += myStr[i];

        //显示天数、天前、天后
        u8g2Fonts.setFont(u8g2_font_logisoso30_tn);
        boolean one_state = 0; //检测1的个数
        uint8_t pianyi = 115;  //偏移量
        //检测到1就往左偏移3个像素，补偿视觉上的误差
        for (uint8_t i = 0; i < tianshu.length(); i++)
        {
          if (tianshu[i] == '1') one_state = 1;
        }
        if (one_state) pianyi -= 3;
        u8g2Fonts.setCursor(getCenterX(tianshu) + pianyi, 90); //显示天数
        u8g2Fonts.print(tianshu);
        u8g2Fonts.setFont(chinese_gb2312);
        u8g2Fonts.setCursor(252, 105); //显示天前天后
        u8g2Fonts.print(tqth);
      }
      else //今天会发生的事件
      {
        for (uint8_t i = 7; i < myStr.length(); i++) //获取事件
          shijian += myStr[i];
        display.drawInvertedBitmap(235, 66, Bitmap_jintian, 57, 24, heise); //显示今天的图片
      }
      //长&短字符不同的显示位置
      if (u8g2Fonts.getUTF8Width(shijian.c_str()) <= 54) //短字符
      {
        u8g2Fonts.setFont(chinese_gb2312);
        u8g2Fonts.setCursor(getCenterX(shijian) + 115, 32); //显示事件
        u8g2Fonts.print(shijian);
      }
      else //对长字符进行分割
      {
        String shijian1 = "", shijian2 = "";
        uint8_t count = 0;
        while (u8g2Fonts.getUTF8Width(shijian1.c_str()) < 51)
        {
          //检查字符的格式 + 数据处理 + 长度计算
          char c = shijian[count];
          byte a = B11100000;
          byte b = c & a;
          if (b == B11100000) //中文等 3个字节
          {
            shijian1 += shijian[count];
            count++;
            shijian1 += shijian[count];
            count++;
            shijian1 += shijian[count];
            count++;
          }
          else if (b == B11000000) //ascii扩展 2个字节
          {
            shijian1 += shijian[count];
            count++;
            shijian1 += shijian[count];
            count++;
          }
          else if (c >= 0 && c <= 127)
          {
            shijian1 += shijian[count];
            count++;
          }
        }
        for (uint8_t i = count; i < shijian.length(); i++)  shijian2 += shijian[i];
        //事件和日期的显示
        u8g2Fonts.setCursor(237, 20); //显示事件1
        u8g2Fonts.print(shijian1);
        u8g2Fonts.setCursor(237, 37); //显示事件2
        u8g2Fonts.print(shijian2);
      }
      //显示目标日期
      u8g2Fonts.setCursor(getCenterX(shijian_shijian) + 115, 56); //目标日期
      u8g2Fonts.print(shijian_shijian);
    }
    else if (eepUserSet.inAWord_mod == 3) //b站粉丝
    {
      u8g2Fonts.setFont(chinese_gb2312);
      display.drawInvertedBitmap(243, 55, Bitmap_bilbil, 52, 55, heise); //显示今天的图片
      if (RTC_Bfen_code == 0)
      {
        String followerStr = "";
        if (RTC_follower < 9999)followerStr = String(RTC_follower);
        else if (RTC_follower < 99999999)
        {
          followerStr = String(float(RTC_follower) / 10000.0, 1) + "W";
        }
        u8g2Fonts.setFont(u8g2_font_cupcakemetoyourleader_tr);
        u8g2Fonts.setCursor(getCenterX(followerStr) + 115, 35); //粉丝数量
        u8g2Fonts.print(followerStr);
      }
      else {
        u8g2Fonts.setCursor(getCenterX("获取错误") + 115, 20); //目标日期
        u8g2Fonts.print("获取错误");
        u8g2Fonts.setCursor(getCenterX(String(RTC_Bfen_code)) + 115, 38); //错误代码
        u8g2Fonts.print(RTC_Bfen_code);
      }
    }
    else //不需要天数倒计时和B粉显示
    {
      u8g2Fonts.setFont(u8g2_font_logisoso92_tn); //时钟用的超大字体 u8g2_font_logisoso78_tn
      //拼装时间 小时和分,不够两位数需要补0
      String hour, minute, assembleTime;
      if (RTC_hour < 10)   hour = "0" + String(RTC_hour);
      else                 hour = String(RTC_hour);
      if (RTC_minute < 10) minute = "0" + String(RTC_minute);
      else                 minute = String(RTC_minute);
      assembleTime = hour + ":" + minute;

      uint8_t sz_x = 0; //显示位置X轴的偏移量
      if (RTC_hour >= 10 && RTC_hour <= 19) sz_x = 6; //10-19点
      else sz_x = 14;
      //显示时间
      u8g2Fonts.setCursor(sz_x, 100);
      u8g2Fonts.print(assembleTime);
      //Serial.print("RTC_hour:"); Serial.println(RTC_hour);
      //Serial.print("RTC_minute:"); Serial.println(RTC_minute);
      //Serial.print("RTC_seconds:"); Serial.println(RTC_seconds);
    }
    //************************ 电池电量显示 ************************ 电池电量0-电压 1-百分比
    u8g2Fonts.setFont(chinese_gb2312);
    String batVol; uint16_t batVol_length;
    if (eepUserSet.batDisplayType == 0) //显示电池显示类型 电池电压
    {
      batVol = String(getBatVolNew()) + "V";
      if (eepUserSet.clock_display_state || RTC_ntpTimeError != 0) //显示日期模式
      {
        batVol_length = u8g2Fonts.getUTF8Width(batVol.c_str());      //获取字符的长度
        display.drawInvertedBitmap(294 - batVol_length - 13, 112, Bitmap_dlxtb, 11, 16, heise); //显示电池小图标
        u8g2Fonts.setCursor(294 - batVol_length, 125);
      }
      else  //极简模式
        u8g2Fonts.setCursor(getCenterX(batVol), 125);

      u8g2Fonts.print(batVol);
    }
    else  //显示电池显示类型 百分比
    {
      batVol = String(getBatVolBfb(getBatVolNew()), 0) + "%";
      if (eepUserSet.clock_display_state || RTC_ntpTimeError != 0) //开启日期模式或有错误都居右显示
      {
        batVol_length = u8g2Fonts.getUTF8Width(batVol.c_str());      //获取字符的长度
        display.drawInvertedBitmap(294 - batVol_length - 13, 112, Bitmap_dlxtb, 11, 16, heise); //显示电池小图标
        u8g2Fonts.setCursor(294 - batVol_length, 125);
      }
      else //极简模式 无错误居中显示
        u8g2Fonts.setCursor(getCenterX(batVol), 125);

      u8g2Fonts.print(batVol);
    }
    //************************ 日期显示 ************************
    String rq; //日期数据 年月日
    uint16_t rq_length; //日期数据长度
    if (eepUserSet.clock_display_state) //极简&显示日期 1-显示日期 0-极简
    {
      //获取节日名的像素长度
      //strcpy(eepUserClock.festival, String("扶贫日").c_str()); //测试用
      //RTC_ntpTimeError = 3; //测试用
      uint16_t festival_length = u8g2Fonts.getUTF8Width(eepUserClock.festival);
      //Serial.print("festival_length:"); Serial.println(festival_length); //测试用

      display.drawLine(0, 110, 295, 110, 0); //画水平线
      //拼装日期数据 + " " + String(eepUserClock.festival) String(eepUserClock.week)
      if (RTC_clock_code == 200) //日期API没有错误
      {
        //拼装、显示年月日
        if (dht30_temp != 0.0 && dht30_humi != 0.0) //温湿度芯片存在
        {
          //如果出现错误 或 出现节日，则省去年份
          if (RTC_ntpTimeError != 0 || (festival_length > 0 && festival_length <= 84))
            rq = String(eepUserClock.month) + "-" + String(eepUserClock.day);
          else
            rq = String(eepUserClock.year) + "-" + String(eepUserClock.month) + "-" + String(eepUserClock.day);
        }
        else  //温湿度芯片不存在
        {
          if (festival_length > 0) //节日名像素长度大于0 则省去年份
            rq = String(eepUserClock.month) + "-" + String(eepUserClock.day);
          else
            rq = String(eepUserClock.year) + "-" + String(eepUserClock.month) + "-" + String(eepUserClock.day);
        }
        u8g2Fonts.setFont(u8g2_font_cupcakemetoyourleader_tr);
        rq_length = u8g2Fonts.getUTF8Width(rq.c_str());
        u8g2Fonts.setCursor(1, 126);
        u8g2Fonts.print(rq);
        //拼装、显示星期几
        u8g2Fonts.setFont(chinese_gb2312);
        u8g2Fonts.setCursor(rq_length + 4, 125);
        u8g2Fonts.print(eepUserClock.week);
        //拼装、显示节假日
        if (dht30_temp != 0.0 && dht30_humi != 0.0) //温湿度芯片存在的情况
        {
          if (festival_length <= 5 * 14 && RTC_ntpTimeError == 0) //错误提示不存在,可以显示5个字
          {
            u8g2Fonts.setCursor(rq_length + 4 + 46, 125);
            u8g2Fonts.print(eepUserClock.festival); //显示节假日
            /*Serial.println(" ");
              Serial.println(eepUserClock.festival);
              Serial.println(festival_length);
              Serial.println(" ");*/
          }
          /*else if (festival_length <= 3 * 14 && RTC_ntpTimeError != 0) //错误提示存在,可以显示3个字
            {
            u8g2Fonts.setCursor(rq_length + 4 + 46, 125);
            u8g2Fonts.print(eepUserClock.festival);
            }*/
        }
        else //温湿度芯片不存在的情况
        {
          u8g2Fonts.setCursor(rq_length + 4 + 46, 125);
          u8g2Fonts.print(eepUserClock.festival);
        }
      }
      else //日期API发生错误
      {
        String rq = "无法获取日期";
        u8g2Fonts.setCursor(1, 125);
        u8g2Fonts.print(rq);
      }
      //************************ 温湿度显示 ************************
      if (dht30_temp != 0.0 && dht30_humi != 0.0)
      {
        //温度图标 与 数值
        display.drawInvertedBitmap(189 - batVol_length, 125 - 13, Bitmap_tempSHT30, 16, 16, heise);
        u8g2Fonts.setCursor(189 - batVol_length + 16, 125);
        u8g2Fonts.print(dht30_temp, 1);

        display.drawInvertedBitmap(189 - batVol_length  + 46, 125 - 13, Bitmap_humiditySHT30, 16, 16, heise);
        u8g2Fonts.setCursor(189 - batVol_length  + 46 + 16, 125);
        u8g2Fonts.print(dht30_humi, 1);
      }
    }

    //************************ 错误图标显示 ************************
    if (RTC_ntpTimeError == 3) //RCT_ntp错误类型 0-正常 1-首次获取网络时间失败 2-定时连接NTP服务器失败 3-定时连接wifi校准超时
    {
      if (eepUserSet.clock_display_state) //日期模式
      {
        if (dht30_temp != 0.0 && dht30_humi != 0.0) //如果温湿度芯片存在
        {
          display.drawInvertedBitmap(118, 125 - 11, Bitmap_ljdk, 16, 13, heise); //显示连接断开图标
          u8g2Fonts.setCursor(118 + 18, 125);
          u8g2Fonts.print("WiFi");
        }
        else
        {
          display.drawInvertedBitmap(208, 125 - 11, Bitmap_ljdk, 16, 13, heise); //显示连接断开图标
          u8g2Fonts.setCursor(208 + 18, 125);
          u8g2Fonts.print("WiFi");
        }
      }
      else //极简
      {
        display.drawInvertedBitmap(1, 125 - 11, Bitmap_ljdk, 16, 13, heise); //显示连接断开图标
        u8g2Fonts.setCursor(1 + 18, 125);
        u8g2Fonts.print("WiFi");
      }
    }
    else if (RTC_ntpTimeError == 2) //RCT_ntp错误类型 0-正常 1-首次获取网络时间失败 2-定时连接NTP服务器失败 3-定时连接wifi校准超时
    {
      if (eepUserSet.clock_display_state)  //日期模式
      {
        if (dht30_temp != 0.0 && dht30_humi != 0.0) //如果温湿度芯片存在
        {
          display.drawInvertedBitmap(122 + 2, 125 - 11, Bitmap_ljdk, 16, 13, heise); //显示连接断开图标
          u8g2Fonts.setCursor(122 + 2 + 18, 125);
          u8g2Fonts.print("NTP");
        }
        else
        {
          display.drawInvertedBitmap(208, 125 - 11, Bitmap_ljdk, 16, 13, heise); //显示连接断开图标
          u8g2Fonts.setCursor(208 + 18, 125);
          u8g2Fonts.print("NTP");
        }
      }
      else //极简
      {
        display.drawInvertedBitmap(1, 125 - 11, Bitmap_ljdk, 16, 13, heise); //显示连接断开图标
        u8g2Fonts.setCursor(1 + 18, 125);
        u8g2Fonts.print("NTP");
      }
    }

  } while (display.nextPage());

  //首次秒数对齐、计算休眠时间
  int32_t sleep_num = 0;
  clockTime1 = millis();
  if (RTC_8025T_error != 0) //时钟芯片不存在
  {
    if (RTC_cq_cuont >= eepUserSet.clockJZJG)    sleep_num = eepUserSet.clockCompensate + 60000 - (RTC_seconds * 1000) - ((clockTime1 - clockTime0) + RTC_clockTime2);
    else if (RTC_seconds != 0) sleep_num = 60000 - (RTC_seconds * 1000) - ((clockTime1 - clockTime0) + RTC_clockTime2);
    else if (RTC_jsjs == eepUserSet.clockQSJG + 1)    sleep_num = 60000 - ((clockTime1 - clockTime0) + RTC_clockTime2);
    else sleep_num = 60000 + eepUserSet.clockCompensate;
    //如果计算出来的休眠值小于0
    if (sleep_num <= 0)
    {
      clockTime3 = abs(sleep_num);
      sleep_num = 100;
    }
  }
  else //时钟芯片存在
  {
    sleep_num = 60000 - (RTC_seconds * 1000);
    if (sleep_num <= 0) sleep_num = 100;
  }
  //低压休眠
  if (bat_vcc <= BatLow)  display_bitmap_sleep("* 电量过低 *");
  /*Serial.print("sleep_num:"); Serial.println(sleep_num);
    Serial.print("RTC_ntpTimeError:"); Serial.println(RTC_ntpTimeError);
    Serial.print("RTC_cq_cuont:"); Serial.println(RTC_cq_cuont);
    Serial.print("RTC_jsjs:"); Serial.println(RTC_jsjs);
    Serial.print("RTC_clockTime2:"); Serial.println(RTC_clockTime2);*/
  //Serial.print("sleep_num:"); Serial.println(sleep_num);
  //Serial.print("sleep_num:"); Serial.println(sleep_num);
  //Serial.print("sleep_num:"); Serial.println(sleep_num);
  esp_sleep(sleep_num);
}

/*void display_Bitmap() //显示开机图片
  {
  display.setPartialWindow(0, 0, 296, 120); //设置局部刷新窗口
  display.fillScreen(baise);
  display.drawInvertedBitmap(0, 0, Bitmap_setup1, 296, 128, heise); //显示开机图片
  display.display(1); // full update
  }*/
//setPartialWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
//根据实际旋转使用参数。
//x和w应该是8的倍数，对于旋转0或2，
//y和h应该是8的倍数，对于旋转1或3，
//否则窗口会根据需要增加，
//这是电子纸控制器的寻址限制
