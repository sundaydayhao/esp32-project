//****** 天气数据获取
//使用Json解析天气数据，天气实况
bool ParseActualWeather(String content, struct ActualWeather* jgt)
{
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, content);
  //serializeJson(json, Serial);//构造序列化json,将内容从串口输出
  if (error)
  {
    Serial.print("天气实况加载json配置失败:");
    Serial.println(error.f_str());
    Serial.println(" ");
    String z = "天气实况json配置失败:" + String(error.f_str()) + " " + content;
    if (String(error.f_str()) == "EmptyInput")
    {
      display_partialLine(7, "天气实况json 重试中");
      return false;
    }
    else
    {
      display_partialLine(7, z);
      esp_sleep(0);
    }
  }

  //检查API是否有返回错误信息，有返回则进入休眠
  if (doc["status_code"].isNull() == 0) //检查到不为空
  {
    strcpy(jgt->status_code, doc["status_code"]);
    String z;
    if (String(actual.status_code) == "AP010001") z = "API 请求参数错误" ;
    else if (String(actual.status_code) == "AP010002") z = "没有权限访问这个 API 接口" ;
    else if (String(actual.status_code) == "AP010003") z = "API 密钥 key 错误" ;
    else if (String(actual.status_code) == "AP010004") z = "签名错误" ;
    else if (String(actual.status_code) == "AP010005") z = "你请求的 API 不存在" ;
    else if (String(actual.status_code) == "AP010006") z = "没有权限访问这个地点: " + String(eepUserSet.city);
    else if (String(actual.status_code) == "AP010007") z = "JSONP 请求需要使用签名验证方式" ;
    else if (String(actual.status_code) == "AP010008") z = "没有绑定域名" ;
    else if (String(actual.status_code) == "AP010009") z = "API 请求的 user-agent 与你设置的不一致" ;
    else if (String(actual.status_code) == "AP010010") z = "没有这个地点" + String(eepUserSet.city);
    else if (String(actual.status_code) == "AP010011") z = "无法查找到指定 IP 地址对应的城市" ;
    else if (String(actual.status_code) == "AP010012") z = "你的服务已经过期" ;
    else if (String(actual.status_code) == "AP010013") z = "访问量余额不足" ;
    else if (String(actual.status_code) == "AP010014") z = "访问频率超过限制" ;
    else if (String(actual.status_code) == "AP010015") z = "暂不支持该城市的车辆限行信息" ;
    else if (String(actual.status_code) == "AP010016") z = "暂不支持该城市的潮汐数据" ;
    else if (String(actual.status_code) == "AP010017") z = "请求的坐标超出支持的范围" ;
    else if (String(actual.status_code) == "AP100001") z = "心知系统内部错误：数据缺失" ;
    else if (String(actual.status_code) == "AP100002") z = "心知系统内部错误：数据错误" ;
    else if (String(actual.status_code) == "AP100003") z = "心知系统内部错误：服务内部错误" ;
    else if (String(actual.status_code) == "AP100004") z = "心知系统内部错误：网关错误" ;
    else z = "天气实况异常:" + String(actual.status_code);
    display_partialLine(7, z);
    Serial.print("天气实况异常:"); Serial.println(actual.status_code);
    const char* zf_t = z.c_str();//String转换char
    strcpy(jgt->weather_name, zf_t);
    return true;
  }
  JsonObject results_0 = doc["results"][0];

  //const char* results_0_location_name = results_0["location"]["name"]; // "北京"

  JsonObject results_0_now = results_0["now"];
  /*const char* results_0_now_text = results_0_now["text"]; // "晴"
    const char* results_0_now_code = results_0_now["code"]; // "0"
    const char* results_0_now_temperature = results_0_now["temperature"]; // "32"

    const char* results_0_last_update = results_0["last_update"]; // "2021-06-20T10:35:00+08:00"*/

  // 复制我们感兴趣的字符串 ,先检查是否为空，空会导致系统无限重启
  // isNull()检查是否为空 空返回1 非空0
  if (results_0["location"]["name"].isNull() == 0)
    strcpy(jgt->city, results_0["location"]["name"]);

  if (results_0_now["text"].isNull() == 0)
    strcpy(jgt->weather_name, results_0_now["text"]);

  if (results_0_now["code"].isNull() == 0)
    strcpy(jgt->weather_code, results_0_now["code"]);

  if (results_0_now["temperature"].isNull() == 0)
    strcpy(jgt->temp, results_0_now["temperature"]);

  if (results_0["last_update"].isNull() == 0)
    strcpy(jgt->last_update, results_0["last_update"]);

  // 这不是强制复制，你可以使用指针，因为他们是指向"内容"缓冲区内，所以你需要确保
  // 当你读取字符串时它仍在内存中
  return true;
}

//使用Json解析天气数据，今天和未来2天
bool ParseFutureWeather(String content, struct FutureWeather* jgt)
{
  DynamicJsonDocument doc(2048); //分配内存,动态
  DeserializationError error = deserializeJson(doc, content); //解析json
  //serializeJson(doc, Serial);//构造序列化json,将内容从串口输出
  if (error)
  {
    Serial.print("未来天气json配置失败:");
    Serial.println(error.f_str());
    Serial.println(" ");
    String z = "未来天气加载json配置失败:" + String(error.f_str()) + " " + content;
    if (String(error.f_str()) == "EmptyInput")
    {
      display_partialLine(7, "未来天气json 重试中");
      return false;
    }
    else
    {
      display_partialLine(7, z);
      esp_sleep(0);
    }
  }

  //检查API是否有返回错误信息，有返回则进入休眠
  if (doc["status_code"].isNull() == 0) //检查到不为空
  {
    strcpy(jgt->status_code, doc["status_code"]);
    String z;
    if (String(future.status_code) == "AP010001") z = "API 请求参数错误" ;
    else if (String(future.status_code) == "AP010002") z = "没有权限访问这个 API 接口" ;
    else if (String(future.status_code) == "AP010003") z = "API 密钥 key 错误" ;
    else if (String(future.status_code) == "AP010004") z = "签名错误" ;
    else if (String(future.status_code) == "AP010005") z = "你请求的 API 不存在" ;
    else if (String(future.status_code) == "AP010006") z = "没有权限访问这个地点: " + String(eepUserSet.city);
    else if (String(future.status_code) == "AP010007") z = "JSONP 请求需要使用签名验证方式" ;
    else if (String(future.status_code) == "AP010008") z = "没有绑定域名" ;
    else if (String(future.status_code) == "AP010009") z = "API 请求的 user-agent 与你设置的不一致" ;
    else if (String(future.status_code) == "AP010010") z = "没有这个地点" + String(eepUserSet.city);
    else if (String(future.status_code) == "AP010011") z = "无法查找到指定 IP 地址对应的城市" ;
    else if (String(future.status_code) == "AP010012") z = "你的服务已经过期" ;
    else if (String(future.status_code) == "AP010013") z = "访问量余额不足" ;
    else if (String(future.status_code) == "AP010014") z = "访问频率超过限制" ;
    else if (String(future.status_code) == "AP010015") z = "暂不支持该城市的车辆限行信息" ;
    else if (String(future.status_code) == "AP010016") z = "暂不支持该城市的潮汐数据" ;
    else if (String(future.status_code) == "AP010017") z = "请求的坐标超出支持的范围" ;
    else if (String(future.status_code) == "AP100001") z = "心知系统内部错误：数据缺失" ;
    else if (String(future.status_code) == "AP100002") z = "心知系统内部错误：数据错误" ;
    else if (String(future.status_code) == "AP100003") z = "心知系统内部错误：服务内部错误" ;
    else if (String(future.status_code) == "AP100004") z = "心知系统内部错误：网关错误" ;
    else z = "未来天气异常:" + String(future.status_code);
    display_partialLine(7, z);
    Serial.print("未来天气异常:"); Serial.println(future.status_code);
    const char* zf_t = z.c_str();//String转换char
    strcpy(jgt->date0_text_day, zf_t);
    return true;
  }

  // 复制我们感兴趣的字符串，先检查是否为空，空会复制失败导致系统无限重启
  if (doc["results"][0]["daily"][0]["date"].isNull() == 0)        //日期
    strcpy(jgt->date0, doc["results"][0]["daily"][0]["date"]);
  if (doc["results"][0]["daily"][1]["date"].isNull() == 0)
    strcpy(jgt->date1, doc["results"][0]["daily"][1]["date"]);
  if (doc["results"][0]["daily"][2]["date"].isNull() == 0)
    strcpy(jgt->date2, doc["results"][0]["daily"][2]["date"]);

  if (doc["results"][0]["daily"][0]["text_day"].isNull() == 0)    //白天天气现象
    strcpy(jgt->date0_text_day, doc["results"][0]["daily"][0]["text_day"]);
  if (doc["results"][0]["daily"][1]["text_day"].isNull() == 0)
    strcpy(jgt->date1_text_day, doc["results"][0]["daily"][1]["text_day"]);
  if (doc["results"][0]["daily"][2]["text_day"].isNull() == 0)
    strcpy(jgt->date2_text_day, doc["results"][0]["daily"][2]["text_day"]);

  if (doc["results"][0]["daily"][0]["text_night"].isNull() == 0)    //晚间天气现象
    strcpy(jgt->date0_text_night, doc["results"][0]["daily"][0]["text_night"]);
  if (doc["results"][0]["daily"][1]["text_night"].isNull() == 0)
    strcpy(jgt->date1_text_night, doc["results"][0]["daily"][1]["text_night"]);
  if (doc["results"][0]["daily"][2]["text_night"].isNull() == 0)
    strcpy(jgt->date2_text_night, doc["results"][0]["daily"][2]["text_night"]);

  if (doc["results"][0]["daily"][0]["high"].isNull() == 0)
    strcpy(jgt->date0_high, doc["results"][0]["daily"][0]["high"]);  //最高温度
  if (doc["results"][0]["daily"][1]["high"].isNull() == 0)
    strcpy(jgt->date1_high, doc["results"][0]["daily"][1]["high"]);
  if (doc["results"][0]["daily"][2]["high"].isNull() == 0)
    strcpy(jgt->date2_high, doc["results"][0]["daily"][2]["high"]);

  if (doc["results"][0]["daily"][0]["low"].isNull() == 0)             //最低温度
    strcpy(jgt->date0_low, doc["results"][0]["daily"][0]["low"]);
  if (doc["results"][0]["daily"][1]["low"].isNull() == 0)
    strcpy(jgt->date1_low, doc["results"][0]["daily"][1]["low"]);
  if (doc["results"][0]["daily"][2]["low"].isNull() == 0)
    strcpy(jgt->date2_low, doc["results"][0]["daily"][2]["low"]);

  if (doc["results"][0]["daily"][0]["humidity"].isNull() == 0)                //湿度
    strcpy(jgt->date0_humidity, doc["results"][0]["daily"][0]["humidity"]);

  if (doc["results"][0]["daily"][0]["wind_scale"].isNull() == 0)        //风力等级
    strcpy(jgt->date0_wind_scale, doc["results"][0]["daily"][0]["wind_scale"]);
  // 这不是强制复制，你可以使用指针，因为他们是指向"内容"缓冲区内，所以你需要确保
  // 当你读取字符串时它仍在内存中
  return true;
}


//使用Json解析天气数据，天气指数
bool ParseLifeIndex(String content, struct LifeIndex* jgt)
{
  StaticJsonDocument<96> filter;
  filter["results"][0]["suggestion"]["uv"]["brief"] = true;
  filter["status_code"] = true;
  
  StaticJsonDocument<192> doc;

  DeserializationError error = deserializeJson(doc, content, DeserializationOption::Filter(filter));
  //serializeJson(json, Serial);//构造序列化json,将内容从串口输出
  if (error)
  {
    Serial.print("天气指数加载json配置失败:");
    Serial.println(error.f_str());
    Serial.println(" ");
    String z = "天气指数json配置失败:" + String(error.f_str()) + " " + content;
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

  //检查API是否有返回错误信息，有返回则进入休眠
  if (doc["status_code"].isNull() == 0) //检查到不为空
  {
    strcpy(jgt->status_code, doc["status_code"]);
    String z;
    if (String(actual.status_code) == "AP010001") z = "API 请求参数错误" ;
    else if (String(actual.status_code) == "AP010002") z = "没有权限访问这个 API 接口" ;
    else if (String(actual.status_code) == "AP010003") z = "API 密钥 key 错误" ;
    else if (String(actual.status_code) == "AP010004") z = "签名错误" ;
    else if (String(actual.status_code) == "AP010005") z = "你请求的 API 不存在" ;
    else if (String(actual.status_code) == "AP010006") z = "没有权限访问这个地点: " + String(eepUserSet.city);
    else if (String(actual.status_code) == "AP010007") z = "JSONP 请求需要使用签名验证方式" ;
    else if (String(actual.status_code) == "AP010008") z = "没有绑定域名" ;
    else if (String(actual.status_code) == "AP010009") z = "API 请求的 user-agent 与你设置的不一致" ;
    else if (String(actual.status_code) == "AP010010") z = "没有这个地点" + String(eepUserSet.city);
    else if (String(actual.status_code) == "AP010011") z = "无法查找到指定 IP 地址对应的城市" ;
    else if (String(actual.status_code) == "AP010012") z = "你的服务已经过期" ;
    else if (String(actual.status_code) == "AP010013") z = "访问量余额不足" ;
    else if (String(actual.status_code) == "AP010014") z = "访问频率超过限制" ;
    else if (String(actual.status_code) == "AP010015") z = "暂不支持该城市的车辆限行信息" ;
    else if (String(actual.status_code) == "AP010016") z = "暂不支持该城市的潮汐数据" ;
    else if (String(actual.status_code) == "AP010017") z = "请求的坐标超出支持的范围" ;
    else if (String(actual.status_code) == "AP100001") z = "心知系统内部错误：数据缺失" ;
    else if (String(actual.status_code) == "AP100002") z = "心知系统内部错误：数据错误" ;
    else if (String(actual.status_code) == "AP100003") z = "心知系统内部错误：服务内部错误" ;
    else if (String(actual.status_code) == "AP100004") z = "心知系统内部错误：网关错误" ;
    else z = "天气指数异常:" + String(actual.status_code);
    display_partialLine(7, z);
    Serial.print("天气指数异常:"); Serial.println(actual.status_code);
    return true;
  }

  // 复制我们感兴趣的字符串 ,先检查是否为空，空会导致系统无限重启
  // isNull()检查是否为空 空返回1 非空0
  if (doc["results"][0]["suggestion"]["uv"]["brief"].isNull() == 0)    //紫外线指数
    strcpy(jgt->uvi, doc["results"][0]["suggestion"]["uv"]["brief"]);

  //Serial.print("紫外线:"); Serial.println(data->uvi);
  // 这不是强制复制，你可以使用指针，因为他们是指向"内容"缓冲区内，所以你需要确保
  // 当你读取字符串时它仍在内存中
  return true;
}
