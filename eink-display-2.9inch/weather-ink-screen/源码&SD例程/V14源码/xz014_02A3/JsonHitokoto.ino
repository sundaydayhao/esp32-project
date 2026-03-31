//****** 获取一言数据
boolean ParseHitokoto(String content, struct Hitokoto* jgt)
{
  StaticJsonDocument<16> filter;
  filter["hitokoto"] = true;

  StaticJsonDocument<128> doc;

  DeserializationError error = deserializeJson(doc, content, DeserializationOption::Filter(filter));
  //serializeJson(json, Serial);//构造序列化json,将内容从串口输出
  if (error)   //检查API是否有返回错误信息，有返回则进入休眠
  {
    Serial.print("一言加载json配置失败:");
    Serial.println(error.f_str());
    Serial.println(" ");
    String z = "一言json配置失败:" + String(error.f_str()) + " " + content;
    if (String(error.f_str()) == "EmptyInput")
    {
      display_partialLine(7, "天气指数json 重试中");
      return false;
    }
    else
    {
      display_partialLine(7, z);
      esp_sleep(0);
    }
  }
  if (doc["status_code"].isNull() == 0) //检查到不为空
  {
    strcpy(jgt->status_code, doc["status_code"]); //strncpy(jgt->status_code, doc["status_code"].c_str(), doc["status_code"].length() + 1);
    String z = "一言异常:" + String(yiyan.status_code) + " 重试中";
    display_partialLine(7, z);
    Serial.print("一言异常:"); Serial.println(yiyan.status_code);
    return false;
  }
  else
  {
    if (doc["hitokoto"].isNull() == 0) strcpy(jgt->hitokoto, doc["hitokoto"]);
    else {
      strcpy(jgt->hitokoto, "哎呀\"hitokoto\"没有数据呢");
      return false;
    }
  }
  // 复制我们感兴趣的字符串 ,先检查是否为空，空会导致系统无限重启
  // 这不是强制复制，你可以使用指针，因为他们是指向"内容"缓冲区内
  // 所以你需要确保 当你读取字符串时它仍在内存中
  // isNull()检查是否为空 空返回1 非空0
  return true;
}

//****** 获取日期数据
boolean ParseRiQi(String content, struct RiQi* jgt)
{
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, content);
  //serializeJson(doc, Serial); // 构造序列化json,将内容从串口输出
  if (error)  // 检查API是否有返回错误信息，有返回则进入休眠或提示
  {
    Serial.print("日期数据json配置失败：");
    Serial.println(error.f_str());
    Serial.println(" ");
    String z = "日期数据json配置失败：" + String(error.f_str());
    return false;
  }

  int code = doc["code"]; // 200
  if (code == 200) {
    JsonObject data = doc["data"];
    JsonObject data_solar = data["solar"];

    if (data_solar["year"].isNull() == 0)
      strcpy(jgt->year, data_solar["year"]);
    if (data_solar["month"].isNull() == 0)
      strcpy(jgt->month, data_solar["month"]);
    if (data_solar["date"].isNull() == 0)
      strcpy(jgt->date, data_solar["date"]);
    if (data_solar["day"].isNull() == 0)
      strcpy(jgt->day, data_solar["day"]);
    if ( data["festival"][0].isNull() == 0)
      strcpy(jgt->festival,  data["festival"][0]);
    return true;
  }
  else {
    //String z = "日期数据获取失败：" + String(code);
    //const char *character = z.c_str();
    return false;
  }
  return true;
  /*Serial.println(" ");
    Serial.print("日期年："); Serial.println(riqi.year);
    Serial.print("日期月："); Serial.println(riqi.month);
    Serial.print("日期日："); Serial.println(riqi.date);
    Serial.print("日期星期："); Serial.println(riqi.day);*/
  /* const char* data_solar_year = data_solar["year"];    // "2021"
    const char* data_solar_month = data_solar["month"];  // "06"
    const char* data_solar_date = data_solar["date"];    // "19"
    const char* data_solar_day = data_solar["day"];      // "星期六"*/
  /*JsonObject data_lunar = data["lunar"];
    const char* data_lunar_year = data_lunar["year"];     // "辛丑"
    const char* data_lunar_animal = data_lunar["animal"]; // "牛"
    const char* data_lunar_month = data_lunar["month"];   // "五月"
    const char* data_lunar_date = data_lunar["date"];     // "初十 "*/
  // 复制我们感兴趣的字符串 ,先检查是否为空，空会导致系统无限重启
  // 这不是强制复制，你可以使用指针，因为他们是指向"内容"缓冲区内
  // 所以你需要确保 当你读取字符串时它仍在内存中
  // isNull()检查是否为空 空返回1 非空0
}

boolean ParseBliBli(String content)
{
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, content);
  //serializeJson(doc, Serial); // 构造序列化json,将内容从串口输出
  if (error)
  {
    Serial.print(F("B站粉丝json配置失败"));
    Serial.println(error.f_str());
    Serial.println(" ");
    String z = "B站粉丝json配置失败：" + String(error.f_str());
    if (String(error.f_str()) == "EmptyInput")
    {
      display_partialLine(7, "B站粉丝json 重试中");
      RTC_Bfen_code = 233;
      return false;
    }
    else
    {
      display_partialLine(7, z);
      RTC_Bfen_code = 233;
    }
  }
  RTC_Bfen_code = doc["code"];   // 0
  JsonObject data = doc["data"];
  RTC_follower = data["follower"];// 3486
  ESP.rtcUserMemoryWrite(RTCdz_Bfen_code, &RTC_Bfen_code, sizeof(RTC_Bfen_code));
  ESP.rtcUserMemoryWrite(RTCdz_follower, &RTC_follower, sizeof(RTC_follower));
  return true;
}
