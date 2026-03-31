void GetData()
{
  String url_ActualWeather;    //天气实况地址
  String url_FutureWeather;    //未来天气地址
  String url_LifeIndex;        //生活指数地址
  String weatherKey_s = eepUserSet.weatherKey; //秘钥
  String city_s = eepUserSet.city;             //城市
  //"http://api.seniverse.com/v3/weather/now.json?key=S6pG_Q54kjfnBAi6i&location=深圳&language=zh-Hans&unit=c"
  //拼装天气实况API地址
  url_ActualWeather = "http://api.seniverse.com/v3/weather/now.json";
  url_ActualWeather += "?key=" + weatherKey_s + "&location=" + city_s + "&language=" + language + "&unit=c";

  //https://api.seniverse.com/v3/weather/daily.json?key=S6pG_Q54kjfnBAi6i&location=深圳&language=zh-Hans&unit=c&start=0&days=3
  //拼装实况未来API地址
  url_FutureWeather = "http://api.seniverse.com/v3/weather/daily.json";
  url_FutureWeather += "?key=" + weatherKey_s + "&location=" + city_s + "&language=" + language + "&start=0" + "&days=3";

  //https://api.seniverse.com/v3/life/suggestion.json?key=S6pG_Q54kjfnBAi6i&location=shanghai&language=zh-Hans
  //拼装生活指数API地址
  url_LifeIndex = "http://api.seniverse.com/v3/life/suggestion.json";
  url_LifeIndex += "?key=" + weatherKey_s + "&location=" + city_s;

  uint8_t cs_count = 0; //重试计数
  uint8_t cs_max = 2;   //重试次数
  if (RTC_tqmskjxs == 0) display_Bitmap_Setup(); //开机壁纸
  display_partialLine(7, "获取生活指数");
  while (ParseLifeIndex(callHttp(url_LifeIndex), &life_index) == 0 && cs_count < cs_max) //获取生活指数
  {
    cs_count++;
  }

  cs_count = 0;
  display_bitmap_bottom(Bitmap_xiaohei, "获取天气实况数据中");
  while (ParseActualWeather(callHttp(url_ActualWeather), &actual) == 0 && cs_count < cs_max)
  {
    cs_count++;
  }

  cs_count = 0;
  display_bitmap_bottom(Bitmap_byx, "获取未来天气数据中");
  while (ParseFutureWeather(callHttp(url_FutureWeather), &future) == 0 && cs_count < cs_max)
  {
    cs_count++;
  }

  if (eepUserSet.inAWord_mod == 0) //获取一言数据
  {
    cs_count = 0;
    display_bitmap_bottom(Bitmap_kon, "获取一言数据");
    while (ParseHitokoto(callHttps(url_yiyan), &yiyan) == 0 && cs_count < cs_max)
    {
      cs_count++;
    }
  }
  else if (eepUserSet.inAWord_mod == 1) //自定义句子不联网
  {
    display_bitmap_bottom(Bitmap_byk, "使用自定义句子");
  }
  else if (eepUserSet.inAWord_mod == 2) //天数倒计时
  {
    display_bitmap_bottom(Bitmap_kon, "启用天数倒计时");
  }
  else if (eepUserSet.inAWord_mod == 3) //B站粉丝
  {
    //拼装B站粉丝API地址
    String BUID = "";
    for (uint8_t i = 4; i < strlen(eepUserSet.inAWord); i++) //获取B站UID
    {
      BUID += eepUserSet.inAWord[i];
    }
    String url_Bfen = "http://api.bilibili.com/x/relation/stat?vmid=" + BUID + "&jsonp=jsonp";
    display_bitmap_bottom(Bitmap_kon, "获取B站粉丝");
    while (ParseBliBli(callHttp(url_Bfen)) == 0 && cs_count < cs_max)
    {
      cs_count++;
    }
  }

  display_bitmap_bottom(Bitmap_wlq4, "获取时间");
  //获取时间
  uint8_t update_count = 0;
  while (timeClient.update() == 0 && update_count < 6)
  {
    Serial.print("NTP超时计数："); Serial.println(update_count);
    delay(100);
    if (update_count == 2) timeClient.setPoolServerName("s2k.time.edu.cn");
    else if (update_count == 3) timeClient.setPoolServerName("1d.time.edu.cn");
    else if (update_count == 4) timeClient.setPoolServerName("s1c.time.edu.cn");
    else if (update_count == 5) timeClient.setPoolServerName("ntp.sjtu.edu.cn");
    update_count++;
  }
  if (update_count < 6)
  {
    RTC_hour = timeClient.getHours();
    ESP.rtcUserMemoryWrite(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
    //Serial.println(timeClient.getFormattedTime());
    Serial.print("获取时间："); Serial.println(RTC_hour);
    timeClient.end();
    display_partialLine(7, "NTP，OK!");
  }
  else
  {
    String a; String b;
    a = actual.last_update[11]; b = actual.last_update[12]; //String转char
    RTC_hour = atoi(a.c_str()) * 10; //char转int
    RTC_hour += atoi(b.c_str()) + 1;
    ESP.rtcUserMemoryWrite(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
    Serial.print("获取时间："); Serial.println(RTC_hour);
    display_partialLine(7, "获取NTP时间失败,改用天气时间");
  }
}
