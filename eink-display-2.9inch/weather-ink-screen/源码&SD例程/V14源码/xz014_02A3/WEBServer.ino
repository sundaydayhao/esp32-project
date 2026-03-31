/*** 初始化webserver配置 ***/
void initWebServer()
{
  // 启动闪存文件系统
  /*if (LittleFS.begin()) Serial.println("LittleFS 启动成功");
    else                  Serial.println("LittleFS 未能成功启动");*/
  // 显示目录中文件名以及文件大小
  /*Dir dir = LittleFS.openDir("");  // 建立“目录”对象
    while (dir.next())                // dir.next()用于检查目录中是否还有“下一个文件”
    {
    String fileName = dir.fileName(); //文件名
    size_t fileSize = dir.fileSize(); //文件大小
    Serial.print("文件:"); Serial.print(fileName); Serial.print("，大小:"); Serial.println(fileSize);
    }
    Serial.println("文件列表输出完毕"); Serial.println(" ");*/

  //display_txt_test();
  server.on("/", webRoot);                                       //处理主页请求
  server.onNotFound(webNotFound);                                //处理网页url请求和404
  server.on("/Read_fileManagement", webRead_fileManagement);     //读取文件系统的文件发送到网页
  server.on("/FileUpdata", HTTP_POST, respondOK, webFileUpload); //处理文件上传页面
  server.on("/Wifi", HTTP_POST, webRootPost);                    //处理主页wifi表单

  // **************** WEB高级文件管理器初始化 ********************************
  // Filesystem status 文件系统状态
  server.on("/status", HTTP_GET, handleStatus);
  // List directory 列表目录
  server.on("/list", HTTP_GET, handleFileList);
  // Load editor 加载编辑器
  server.on("/edit", HTTP_GET, handleGetEdit);
  // Create file 创建文件
  server.on("/edit",  HTTP_PUT, handleFileCreate);
  // Delete file 删除文件
  server.on("/edit",  HTTP_DELETE, handleFileDelete);
  // 上传文件
  //-在请求以所有解析参数结束后调用第一个回调
  //-第二个回调处理该位置的文件上载
  server.on("/edit",  HTTP_POST, replyOK, handleFileUpload);
  // ********************************************************************

  server.on("/RenameFile", HTTP_POST, webRenameFile);         //处理重命名文件表单
  server.on("/DeleteFile", HTTP_POST, webRead_deleteFile);    //删除文件
  server.on("/EnableBmp", HTTP_POST, webRead_enableBmp);      //启用BMP
  server.on("/StopBmp", HTTP_POST, webRead_stopBmp);          //停用BMP

  server.on("/Weather", HTTP_POST, webWeatherPost);             //处理主页天气表单
  server.on("/InAWord", HTTP_POST, webInAWord);                 //处理更多设置里的一句话表单
  server.on("/Read_InAWord", webReadInAWord);                   //获取当前的多功能设置模式

  server.on("/Read_updataAddress", webRead_updataAddress);       //ajax 请求城市固件更新地址
  server.on("/Read_info", webRead_info);                         //ajax 请求系统信息

  server.on("/Put_nightUpdata", webPut_nightUpdata);             //ajax 切换夜间更新信息
  server.on("/Read_nightUpdata", webRead_nightUpdata);           //ajax 请求夜间更新信息

  server.on("/Put_batDisplayType", webPut_batDisplayType);       //ajax 切换电量显示类型
  server.on("/Read_batDisplayType", webRead_batDisplayType);     //ajax 请求电量显示类型

  server.on("/Put_setRotation", webPut_setRotation);             //ajax 切换屏幕旋转方向
  server.on("/Read_setRotation", webRead_setRotation);           //ajax 请求屏幕旋转方向

  server.on("/Put_clockDisplayState", webPut_clockDisplayState);        //ajax 切换时钟显示模式-极简&显示日期
  server.on("/Read_clockDisplayState", webRead_clockDisplayState);      //ajax 获取时钟显示模式-极简&显示日期

  server.on("/Put_clockCalibrationState", webPut_clockCalibrationState);        //ajax 切换时钟强制校准 写入状态
  server.on("/Read_clockCalibrationState", webRead_clockCalibrationState);      //ajax 获取时钟强制校准 获取状态

  server.on("/ClockCompensate", HTTP_POST, webClockCompensate);  //ajax 处理时钟补偿值表单
  server.on("/Read_clockCompensate", webRead_clockCompensate);   //ajax 请求当前的时钟补偿值

  server.on("/SetOutputPower", HTTP_POST, webSetOutputPower);    // ajax 处理发射功率表单
  server.on("/Read_setOutputPower", webRead_setOutputPower);     // ajax 请求发射功率

  server.on("/Put_albumAuto", webPut_albumAuto);         //ajax 图片随机播放 写入状态
  server.on("/Read_albumAuto", webRead_albumAuto);       //ajax 图片随机播放 获取状态

  server.on("/ClockJZJG", HTTP_POST, webClockJZJG);     // ajax 处理时钟校准间隔表单
  server.on("/Read_clockJZJG", webRead_clockJZJG);      // ajax 请求当前的时钟校准间隔值

  server.on("/ClockQSJG", HTTP_POST, webClockQSJG);     // ajax 处理时钟模式全局刷新间隔表单
  server.on("/Read_clockQSJG", webRead_clockQSJG);      // ajax 请求当前的时钟模式全局刷新间隔值

  server.on("/Read_updateNews", webRead_updateButton);  //ajax 请求文件数量超过上限的消息

  server.on("/Read_staNameIp", webRead_staNameIp);      //ajax 请求STAwifi名称和ip地址

  server.on("/Read_city", webRead_city);                //ajax 请求城市名称

  server.on("/Read_scanWifi", webRead_scanWifi);        //ajax 扫描周边wifi

  server.on("/Read_fileSync", webRead_fileSync);      // ajax 请求当前文件系统剩余内存
  server.on("/Put_sdInit", webPut_sdInit);            //ajax 挂载SD卡
  server.on("/Read_sdInit", webRead_sdInit);          //ajax 挂载SD卡

  server.on("/RESET", webRESET);                        //处理重启请求

  //配置http更新服务为为&server
  httpUpdater.setup(&server, "/update", "Jones", "3333");
  //启动WebServer
  server.begin();
}

//回复状态码 200 给客户端
void respondOK()
{
  server.send(200);
}

void webNotFound() //设置处理404情况的函数'webUserRequest'
{
  //String path = server.uri();
  String uri = ESP8266WebServer::urlDecode(server.uri()); // required to read paths with blanks
  if (handleFileRead(uri)) {
    return;
  }
  //Serial.print("load url:");
  //Serial.println(path);
  /*String contentType = getContentType(path);
    String pathWithGz = path + ".gz";
    if (LittleFS.exists(pathWithGz) || LittleFS.exists(path))
    {
    if (LittleFS.exists(pathWithGz)) path += ".gz";
    File file = LittleFS.open(path, "r");
    //server.sendHeader("header Content-Encoding=gzip", "/");
    server.streamFile(file, contentType);
    file.close();
    return;
    }*/
  server.send(404, "text/html", sendHTML_main());
}

/*** 处理根目录get请求 ***/
void webRoot() //处理网站根目录"/"的访问请求
{
  String path = "/GCSBS.html";
  String contentType = getContentType(path);  // 获取文件类型
  String pathWithGz = path + ".gz";
  if (LittleFS.exists(path))  // 如果访问的文件可以在LittleFS中找到
  {
    File file = LittleFS.open(path, "r");   // 则尝试打开该文件
    server.streamFile(file, contentType);   // 并且将该文件返回给浏览器
    file.close();                           // 并且关闭文件
  }
  else if (LittleFS.exists(pathWithGz) || LittleFS.exists(path))
  {
    if (LittleFS.exists(pathWithGz)) path += ".gz";
    File file = LittleFS.open(path, "r");
    //server.sendHeader("header Content-Encoding=gzip", "/");
    server.streamFile(file, contentType);
    file.close();
  }
  else
  {
    server.send(200, "text/html", sendHTML_main());
  }
}


/*** 处理根目录post请求 ***/
void webRootPost()
{
  String wifi_xiaoxi = "WIFI连接中，可能会断开热点";
  //char *strcpy(char *dest, const char *src) 把 src 所指向的字符串复制到 dest。
  //arg(name) —— 根据请求key获取请求参数的值
  //arg(index) —— 获取第几个请求参数的值
  /*Serial.print("WiFi表单-参数个数 "); Serial.println(server.args());
    Serial.print("WiFi表单-参数0名称 "); Serial.println(server.argName(0));
    Serial.print("WiFi表单-参数0数值 "); Serial.println(server.arg("plain"));*/
  if (server.hasArg("ssid")) //查询是否存在某个参数
  {
    strcpy(sta_ssid, server.arg("ssid").c_str());  //保存参数
    if (strlen(sta_ssid) == 0) wifi_xiaoxi = "WIFI名称不能为空"; //检查WIFI名称长度为0时退出
  }
  else wifi_xiaoxi = "未找到参数'ssid'";

  //查询是否存在某个参数
  if (server.hasArg("password"))
    strcpy(sta_password, server.arg("password").c_str()); //保存参数
  else wifi_xiaoxi += "  未找到参数'password'";

  //server.send(200, "text/html", sendHTML_return_home(wifi_xiaoxi));
  server.send(200, "text/plain", wifi_xiaoxi);

  if (wifi_xiaoxi == "WIFI连接中，可能会断开热点")
  {
    WiFi.disconnect();
    overtime = millis(); //重置配网超时时间
    // peiwangInitStete = 0; //重置配网初始化
    ap_state = 5; //发送连接指令
    sta_count = 0;
  }
  display_partialLine(7, wifi_xiaoxi);
}
void webRead_scanWifi() //扫描周边wifi
{
  String web_message;
  int8_t n;
  n = WiFi.scanNetworks(); //扫描wifi
  if (n > 0)
  {
    web_message = "<label for='sel1'>周边网络:</label>";
    web_message += "<select class='form-control' id='sel1' name='ssid'>";
    for (uint8_t i = 0; i < n; i++)
    {
      web_message += "<option>" + WiFi.SSID(i) + "</option>";
    }
    web_message += "</select>";
  }
  else {
    web_message = F("<label for='sel1'>周边网络:</label>");
    web_message += F("<select class='form-control' id='sel1'>");
    web_message += F("<option >无扫描结果</option>");
    web_message += F("</select>");
  }
  server.send(200, "text/plain", web_message);
}
/*** 处理天气相关post请求 ***/
boolean tqxx_display = 0; //天气信息清除
void webWeatherPost()
{
  EEPROM.get(eeprom_address0, eepUserSet);
  String web_message;
  //查询是否存在某个参数 WeatherKey
  if (server.hasArg("WeatherKey")) //查询是否存在某个参数
  {
    strcpy(eepUserSet.weatherKey, server.arg("WeatherKey").c_str());
    if (strlen(eepUserSet.weatherKey) != 0)  //检查名称长度为0时退出
    {
      EEPROM.put(eeprom_address0, eepUserSet);
      EEPROM.commit(); //保存
      web_message = "私钥保存成功";
      tqxx_display = 1;
    }
    else
    {
      EEPROM.get(eeprom_address0, eepUserSet);
      web_message = "私钥未更改";
    }
  }
  else
  {
    web_message = "WeatherKey 参数不存在";
  }

  if (server.hasArg("city")) //查询是否存在某个参数
  {
    strcpy(eepUserSet.city, server.arg("city").c_str());
    if (strlen(eepUserSet.city) != 0)        //检查名称长度为0时退出
    {
      EEPROM.put(eeprom_address0, eepUserSet);
      EEPROM.commit(); //保存
      web_message += "<br>城市保存成功";
      tqxx_display = 1;
    }
    else
    {
      EEPROM.get(eeprom_address0, eepUserSet);
      web_message += "<br>城市未更改";
    }
  }
  else
  {
    web_message += "<br>city 参数不存在";
  }
  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
  eepUserSet.runMode = 4;
}

size_t file_sy_zj;
// 处理上传文件函数
void webFileUpload()
{
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) //上传_文件_开始
  {
    uint8_t file_count = 0; //文件数量计数
    Dir dir = LittleFS.openDir("");  // 建立“目录”对象
    while (dir.next())                   // dir.next()用于检查目录中是否还有“下一个文件”
    {
      //String fileName = dir.fileName(); //文件名
      //size_t fileSize = dir.fileSize(); //文件大小
      //Serial.printf("文件:%s,大小:%d\n", fileName.c_str(), fileSize);
      file_count++; //记录有多少文件
    }
    if (file_count >= FileLimit) //如果文件大于等于32个，提示错误
    {
      String xiaoxi = "文件数量超过上限，最多" + String(FileLimit) + "个";
      //server.send(500, "text/html", xiaoxi);
      display_partialLine(7, xiaoxi);
      replyServerError(xiaoxi);
      server.handleClient();  //处理http请求
      delay(1000);
      ESP.reset();
    }

    FSInfo p;                      // 创建结构体
    LittleFS.info(p); // 获取的值放在结构体中
    file_sy_zj = p.totalBytes - p.usedBytes; //剩余空间，字节

    String uploadFileName = upload.filename;                         // 建立字符串变量用于存放上传文件名
    //if (!uploadFileName.startsWith("/")) uploadFileName = "/" + uploadFileName;  // 为上传文件名前加上"/"
    Serial.println("文件名字: " + uploadFileName);
    Serial.println("文件系统剩余空间: " + String(file_sy_zj));
    //Serial.print("整个post请求的大小: ");  Serial.println(upload.contentLength);
    fsUploadFile = LittleFS.open(uploadFileName, "w");               // 在LittleFS中建立文件用于写入用户上传的文件数据
    if (!fsUploadFile)
    {
      String xiaoxi = upload.filename + " 文件创建失败";
      //server.send(500, "text/html", xiaoxi);
      display_partialLine(7, xiaoxi);
      replyServerError(xiaoxi);
      server.handleClient();  //处理http请求
      delay(1000);
      ESP.reset();
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) // 上传_文件_写入
  {
    //ESP.wdtFeed();
    overtime = millis();  //重置配网超时时间
    yield();
    if (fsUploadFile)
    {
      //fsUploadFile.write(upload.buf, upload.currentSize); // 向LittleFS文件写入浏览器发来的文件数据
      size_t bytesWritten = fsUploadFile.write(upload.buf, upload.currentSize); //写入当前大小
      //Serial.println("upload.currentSize: " + String(upload.currentSize));
      //Serial.println("upload.totalSize: " + String(upload.totalSize)); //累积总大小
      if (upload.totalSize > file_sy_zj) //目前所上传的大小大于剩余空间
      {
        LittleFS.remove(upload.filename);
        String xiaoxi = upload.filename + " 上传失败，存储空间不足";
        //server.send(500, "text/html", xiaoxi);
        display_partialLine(7, xiaoxi);
        //upload.status = UPLOAD_FILE_END;
        replyServerError(xiaoxi);
        server.handleClient();  //处理http请求
        delay(1000);
        ESP.reset();
      }
      if (bytesWritten != upload.currentSize)
      {
        LittleFS.remove(upload.filename);
        String xiaoxi = upload.filename + " 文件写入失败";
        //server.send(500, "text/html", xiaoxi);
        display_partialLine(7, xiaoxi);
        //upload.status = UPLOAD_FILE_END;
        replyServerError(xiaoxi);
        server.handleClient();  //处理http请求
        delay(1000);
        ESP.reset();
      }
    }
  }
  else if (upload.status == UPLOAD_FILE_END) // 上传_文件_结束
  {
    if (fsUploadFile) // 如果文件成功建立
    {
      //uint32_t size1 = fsUploadFile.size(); //记录上传文件的大小
      fsUploadFile.close();// 将文件关闭
      //重新打开文件，并记录文件大小
      fsUploadFile = LittleFS.open(upload.filename, "r");
      uint32_t size1 = fsUploadFile.size(); //记录上传文件的大小
      Serial.print("文件名称："); Serial.println(upload.filename);
      Serial.print("文件大小："); Serial.println(size1);
      if (size1 == 0) //检查文件大小，等于0提示问题
      {
        LittleFS.remove(upload.filename);
        server.send(200, "text/plain", upload.filename + " 上传失败，空文件或存储空间不足");
        display_partialLine(7, upload.filename + " 上传失败，空文件或存储空间不足");
      }
      else
      {
        server.send(200, "text/plain", upload.filename + " 上传成功");
        display_partialLine(7, upload.filename + " 上传成功");
      }
    }
    else // 如果文件未能成功建立
    {
      Serial.println("文件上传失败");// 通过串口监视器输出报错信息
      //server.send(500, "text/html", sendHTML_return_home("500 无法上传文件"));
      server.send(500, "text/html", "上传失败，名称太长或有特殊符号");
      display_partialLine(7, upload.filename + " 上传失败");
    }
  }
}

void webInAWord() //自定义一句话的提交表单处理
{
  EEPROM.get(eeprom_address0, eepUserSet);
  String web_message;
  String web_xx;
  //查询是否存在某个参数 inAWord
  if (server.hasArg("inAWord")) //查询是否存在某个参数
  {
    strcpy(eepUserSet.inAWord, server.arg("inAWord").c_str());
    if (strlen(eepUserSet.inAWord) == 0)  //检查自定义的一句话长度为0时
    {
      web_message = "一言模式";
      eepUserSet.inAWord_mod = 0;
    }
    else if (strlen(eepUserSet.inAWord) >= 64) {
      web_message = "字太长<br>中文<=21个<br>英文<=63个";
    }
    else
    {
      web_xx = inputBoxPatternRecognition(String(eepUserSet.inAWord));
      if (eepUserSet.inAWord_mod == 1) web_message = "自定义一句话启动";
      else if (eepUserSet.inAWord_mod == 2)
      {
        web_message = "天数倒计时启动<br>" + web_xx;
        if (eepUserSet.clock_display_state != 1) {
          web_message += "<br>关闭极简时钟";
          eepUserSet.clock_display_state = 1;
        }
      }
      else if (eepUserSet.inAWord_mod == 3)
      {
        web_message = "B粉显示启动<br>" + web_xx;
        if (eepUserSet.clock_display_state != 1) {
          web_message += "<br>关闭极简时钟";
          eepUserSet.clock_display_state = 1;
        }
      }
      else if (eepUserSet.inAWord_mod == 0)
      {
        web_message = web_xx;
        strcpy(eepUserSet.inAWord, server.arg("").c_str());
      }
    }
  }
  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commit(); //保存
  eepUserSet.runMode = 4;
  server.send(200, "text/plain", web_message);
  if (strlen(eepUserSet.inAWord) == 0)  display_partialLine(7, web_message);
  else display_partialLine(7, String(eepUserSet.inAWord));
}
void webReadInAWord() //读取当前的模式 0-联网获取 1-自定义 2-倒计时模式 3-B粉获取
{
  String web_message = "";
  String myStr = "";
  if (eepUserSet.inAWord_mod == 0) myStr = "一言模式";
  else  myStr = String(eepUserSet.inAWord);
  web_message += "<label for='comment'>多功能输入框：</label>";
  web_message += "<textarea name='inAWord' class='form-control' rows='2' id='comment' maxlength='64'>" + myStr + "</textarea>";
  server.send(200, "text/plain", web_message);
}

void webRead_staNameIp() //ajax请求STA名字和ip
{
  String web_message = WiFi.SSID() + " " + WiFi.localIP().toString();
  if (sta_count == 22) web_message = "WiFi未配置";
  else if (WiFi.localIP().toString() == "(IP unset)" && sta_count == 21) web_message = WiFi.SSID() + " 连接失败";
  else if (WiFi.localIP().toString() == "(IP unset)" && sta_count <= 20) web_message = WiFi.SSID() + " 连接中，勿进行其他操作";
  server.send(200, "text/plain", web_message);
}
void webRead_city() //ajax请求天气
{
  String web_message = (String)eepUserSet.city + " ";
  if (tqxx_display) {
    tqxx_display = 0;
    String weatherKey_s = eepUserSet.weatherKey;
    String city_s = eepUserSet.city;
    //拼装天气实况API地址
    String url_ActualWeather;    //天气实况地址
    url_ActualWeather = "http://api.seniverse.com/v3/weather/now.json";
    url_ActualWeather += "?key=" + weatherKey_s + "&location=" + city_s + "&language=" + language + "&unit=c";
    ParseActualWeather(callHttp(url_ActualWeather), &actual);
  }
  if (WiFi.isConnected() == 1 && String(actual.weather_name) != "天气实况异常:connection failed") web_message += String(actual.weather_name) + " " + String(actual.temp) + "℃";
  server.send(200, "text/plain", web_message);
}
void webRead_updataAddress() //ajax固件更新地址
{
  String web_message;
  web_message += "注意电量，以免在上传过程丢失固件！<br>";
  web_message += "确保8266和设备在同一WIFI网络下！<br>";
  web_message += "当前电压：" + String(getBatVolNew()) + " V<br>";
  web_message += "<p>http://" + WiFi.localIP().toString() + "/update</p>";
  web_message += "<a href=\"http://" + WiFi.localIP().toString() + "/update\">";
  web_message += "<button type='button' class='btn btn-danger'>点击进入更新页面</button>";
  web_message += "</a>";

  server.send(200, "text/plain", web_message);
  display_partialLine(7, "请求更新地址");
}
void webRead_info() //ajax请求系统信息
{
  String web_message;
  web_message = "程序版本：" + version + "-" + version1 + "<br>";
  web_message += "芯片ID：" + String(ESP.getChipId()) + "<br>";
  web_message += "芯片STA-MAC地址：" + String(WiFi.macAddress().c_str()) + "<br>";
  web_message += "芯片AP-MAC地址：" + String(WiFi.softAPmacAddress().c_str()) + "<br>";
  web_message += "可用堆大小：" + String(ESP.getFreeHeap()) + "<br>";
  web_message += "核心库版本：" + String(ESP.getCoreVersion()) + "<br>";
  web_message += "CPU运行频率：" + String(ESP.getCpuFreqMHz()) + "MHZ" + "<br>";
  web_message += "当前固件大小：" + String(ESP.getSketchSize()) + "<br>";
  web_message += "剩余可用固件空间：" + String(ESP.getFreeSketchSpace()) + "<br>";
  web_message += "闪存芯片ID：" + String(ESP.getFlashChipId()) + "<br>";
  web_message += "闪存芯片真正大小：" + String(ESP.getFlashChipRealSize()) + "<br>";
  web_message += "闪存芯片运行频率：" + String(ESP.getFlashChipSpeed()) + "<br><br>";
  web_message += "<p style='font-size:0.8rem'><font color='red'>高级文件管理器涉及系统文件谨慎操作</font><p>";
  web_message += "<p style='font-size:0.8rem'><a href='https://gitee.com/Lichengjiez'>甘草酸不酸</a><p>";
  server.send(200, "text/plain", web_message);
  display_partialLine(7, "请求系统信息");
}
void webRead_fileSync() //文件系统剩余内存
{
  size_t file_sy_zj;
  FSInfo p;                      // 创建结构体
  LittleFS.info(p); // 获取的值放在结构体中
  file_sy_zj = p.totalBytes - p.usedBytes; //剩余空间，字节
  server.send(200, "text/plain", String(file_sy_zj));
}

void webRead_sdInit() //SD卡挂载卸载
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  if (eepUserSet.sdState)
  {
    FSInfo p;    // 创建结构体byteConversion
    SDFS.info(p); // 获取的值放在结构体中
    String totalBytes = "(" + byteConversion(p.totalBytes) + ")";
    web_message = "SD卡：启用" + totalBytes;
  }
  else
    web_message = "SD卡：停用";
  server.send(200, "text/plain", web_message);
}
void webPut_sdInit() //SD卡挂载卸载
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);

  eepUserSet.sdState = !eepUserSet.sdState;
  if (eepUserSet.sdState)
  {
    //SD卡挂载检查
    if (sdBeginCheck())
    {
      FSInfo p;    // 创建结构体byteConversion
      SDFS.info(p); // 获取的值放在结构体中
      String totalBytes = "(" + byteConversion(p.totalBytes) + ")";
      web_message = "SD卡：启用" + totalBytes;
    }
    else web_message = "SD卡：启用失败";
  }
  else {
    SDFS.end();
    web_message = "SD卡：停用";
  }

  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commit(); //保存
  eepUserSet.runMode = 4;
  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
}

void webRead_nightUpdata() //获取夜间更新信息
{
  String web_message;
  //Serial.print("eepUserSet.nightUpdata："); Serial.println(eepUserSet.nightUpdata); Serial.println(" ");
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  if (eepUserSet.nightUpdata)
    web_message = "天气更新模式：全天更新";
  else
    web_message = "天气更新模式：夜间不更新（0-6）";
  server.send(200, "text/plain", web_message);
}
void webPut_nightUpdata() //切换夜间更新信息
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  //Serial.print("eepUserSet.nightUpdata："); Serial.println(eepUserSet.nightUpdata); Serial.println(" ");
  eepUserSet.nightUpdata = !eepUserSet.nightUpdata;
  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commit(); //保存
  eepUserSet.runMode = 4;
  if (eepUserSet.nightUpdata) web_message = "天气更新模式：全天更新";
  else                        web_message = "天气更新模式：夜间不更新（0-6）";

  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
}

void webRead_batDisplayType() //获取电量显示类型
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  if (eepUserSet.batDisplayType)
    web_message = "电量显示：百分比（" + String(getBatVolBfb(getBatVolNew()), 0) + "%）";
  else
    web_message = "电量显示：电压（" + String(getBatVolNew()) + "V）";
  server.send(200, "text/plain", web_message);
}
void webPut_batDisplayType() //切换电量显示类型
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.batDisplayType = !eepUserSet.batDisplayType;
  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commit(); //保存
  eepUserSet.runMode = 4;

  if (eepUserSet.batDisplayType)
    web_message = "电量显示：百分比（" + String(getBatVolBfb(getBatVolNew()), 0) + "%）";
  else
    web_message = "电量显示：电压（" + String(getBatVolNew()) + "V）";

  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
}

void webRead_setRotation() //获取屏幕旋转方向
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  if (eepUserSet.setRotation) web_message = "墨水屏方向：270度（3）";
  else                     web_message = "墨水屏方向：90度（1）";
  server.send(200, "text/plain", web_message);
}
void webPut_setRotation() //切换屏幕旋转方向
{
  overtime = millis();  //重置配网超时时间
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.setRotation = !eepUserSet.setRotation;
  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commit(); //保存
  eepUserSet.runMode = 4;
  if (eepUserSet.setRotation) {
    web_message = "墨水屏方向：270度（3）";
    display.setRotation(3);  //设置方向
  }
  else {
    web_message = "墨水屏方向：90度（1）";
    display.setRotation(1);
  }
  display.init();
  peiwangInitStete = 0;  //刷新配网界面
  webDisplayQh_state = 1;
  server.send(200, "text/plain", web_message);

}

void webRead_clockDisplayState() //获取时钟显示模式-极简&显示日期
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  if (eepUserSet.clock_display_state)
    web_message = "时钟显示模式：显示日期";
  else
    web_message = "时钟显示模式：极简";
  server.send(200, "text/plain", web_message);
}
void webPut_clockDisplayState() //切换时钟显示模式-极简&显示日期
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.clock_display_state = !eepUserSet.clock_display_state;
  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commit(); //保存
  eepUserSet.runMode = 4;
  if (eepUserSet.clock_display_state)
    web_message = "时钟显示模式：显示日期";
  else
    web_message = "时钟显示模式：极简";

  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
}


void webRead_clockCalibrationState() //获取时钟强制校准状态
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  if (eepUserSet.clock_calibration_state)
    web_message = "时钟强制校准：开启";
  else
    web_message = "时钟强制校准：关闭";
  server.send(200, "text/plain", web_message);
}
void webPut_clockCalibrationState() //切换时钟强制校准状态
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.clock_calibration_state = !eepUserSet.clock_calibration_state;
  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commit(); //保存
  eepUserSet.runMode = 4;
  if (eepUserSet.clock_calibration_state)
    web_message = "时钟强制校准：开启";
  else
    web_message = "时钟强制校准：关闭";

  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
}

void webRead_albumAuto() //图片随机播放 获取状态
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  if (eepUserSet.albumAuto)
    web_message = "图片随机播放：开启";
  else
    web_message = "图片随机播放：关闭";
  server.send(200, "text/plain", web_message);
}
void webPut_albumAuto() //图片随机播放 写入状态
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.albumAuto = !eepUserSet.albumAuto;
  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commit(); //保存
  eepUserSet.runMode = 4;
  if (eepUserSet.albumAuto)
    web_message = "图片随机播放：开启";
  else
    web_message = "图片随机播放：关闭";

  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
}

void webRead_clockCompensate() //获取时钟补偿值
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  String xiaoxi =  "当前:" + String(eepUserSet.clockCompensate);
  web_message = "<input type='text' class='form-control' placeholder='" + xiaoxi + "' name='ClockCompensate' autocomplete='off'>";
  server.send(200, "text/plain", web_message);
}
void webClockCompensate() //时钟补偿值表单提交
{
  EEPROM.get(eeprom_address0, eepUserSet);
  String web_message;
  //查询是否存在某个参数 ClockCompensate
  if (server.hasArg("ClockCompensate")) //查询是否存在某个参数
  {
    eepUserSet.clockCompensate = atol(server.arg("ClockCompensate").c_str()); //String 转 int
    if (server.arg("ClockCompensate").length() != 0)  //检查名称长度为0时退出
    {
      EEPROM.put(eeprom_address0, eepUserSet);
      EEPROM.commit(); //保存
      web_message = "保存成功";
    }
    else
    {
      EEPROM.get(eeprom_address0, eepUserSet);
      web_message = "未更改";
    }
  }
  else web_message = "ClockCompensate 参数不存在";

  server.send(200, "text/plain", web_message);
  display_partialLine(7, "时钟补偿值：" + String(eepUserSet.clockCompensate) + web_message);
  eepUserSet.runMode = 4;
}

void webRead_clockJZJG() //获取时钟校准间隔
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  String xiaoxi =  "当前:" + String(eepUserSet.clockJZJG) + "分钟";
  web_message = "<input type='text' class='form-control' placeholder='" + xiaoxi + "' name='ClockJZJG' autocomplete='off'>";
  server.send(200, "text/plain", web_message);
}
void webClockJZJG() //时钟校准间隔表单提交
{
  EEPROM.get(eeprom_address0, eepUserSet);
  String web_message;
  //查询是否存在某个参数 ClockCompensate
  if (server.hasArg("ClockJZJG")) //查询是否存在某个参数
  {
    if (server.arg("ClockJZJG").length() != 0)  //检查名称长度为0时退出
    {
      eepUserSet.clockJZJG = atol(server.arg("ClockJZJG").c_str()); //String 转 int
      if (eepUserSet.clockJZJG < 15)
      {
        eepUserSet.clockJZJG = 15;
        EEPROM.put(eeprom_address0, eepUserSet);
        EEPROM.commit(); //保存
        web_message = "最小为15，保存成功";
      }
      else
      {
        EEPROM.put(eeprom_address0, eepUserSet);
        EEPROM.commit(); //保存
        web_message = "保存成功";
      }
    }
    else
    {
      EEPROM.get(eeprom_address0, eepUserSet);
      web_message = "未更改";
    }
  }
  else web_message = "ClockJZJG 参数不存在";

  server.send(200, "text/plain", web_message);
  display_partialLine(7, "时钟校准间隔：" + String(eepUserSet.clockJZJG) + web_message);
  eepUserSet.runMode = 4;
}

void webRead_clockQSJG() //获取全刷间隔
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  String xiaoxi =  "当前:" + String(eepUserSet.clockQSJG) + "分钟";
  web_message = "<input type='text' class='form-control' placeholder='" + xiaoxi + "' name='ClockQSJG' autocomplete='off'>";
  server.send(200, "text/plain", web_message);
}
void webClockQSJG() //时钟全刷表单提交
{
  EEPROM.get(eeprom_address0, eepUserSet);
  String web_message;
  //查询是否存在某个参数 ClockCompensate
  if (server.hasArg("ClockQSJG")) //查询是否存在某个参数
  {
    if (server.arg("ClockQSJG").length() != 0)  //检查名称长度为0时退出
    {
      eepUserSet.clockQSJG = atol(server.arg("ClockQSJG").c_str()); //String 转 int
      if (eepUserSet.clockQSJG < 5)
      {
        eepUserSet.clockQSJG = 5;
        EEPROM.put(eeprom_address0, eepUserSet);
        EEPROM.commit(); //保存
        web_message = "最小为5，保存成功";
      }
      else
      {
        EEPROM.put(eeprom_address0, eepUserSet);
        EEPROM.commit(); //保存
        web_message = "保存成功";
      }
    }
    else
    {
      EEPROM.get(eeprom_address0, eepUserSet);
      web_message = "未更改";
    }
  }
  else web_message = "ClockQSJG 参数不存在";

  server.send(200, "text/plain", web_message);
  display_partialLine(7, "时钟全刷间隔：" + String(eepUserSet.clockQSJG) + web_message);
  eepUserSet.runMode = 4;
}


void webRead_setOutputPower() //获取当前发射功率
{
  String web_message;
  EEPROM.get(eeprom_address0, eepUserSet);
  eepUserSet.runMode = 4;
  String xiaoxi =  "当前:" + String(eepUserSet.outputPower);
  web_message = "<input type='text' class='form-control' placeholder='" + xiaoxi + "' name='SetOutputPower' autocomplete='off'>";
  server.send(200, "text/plain", web_message);
}
void webSetOutputPower() //表单提交 当前发射功率
{
  EEPROM.get(eeprom_address0, eepUserSet);
  String web_message;
  //查询是否存在某个参数 SetOutputPower
  if (server.hasArg("SetOutputPower")) //查询是否存在某个参数
  {
    if (server.arg("SetOutputPower").length() != 0)  //检查名称长度为0时退出
    {
      eepUserSet.outputPower = atof(server.arg("SetOutputPower").c_str()); //String 转 float
      if (eepUserSet.outputPower < 10)
      {
        eepUserSet.outputPower = 10.0;
        EEPROM.put(eeprom_address0, eepUserSet);
        EEPROM.commit(); //保存
        web_message = "最小为10，已保存";
      }
      else
      {
        EEPROM.put(eeprom_address0, eepUserSet);
        EEPROM.commit(); //保存
        web_message = "保存成功";
      }
    }
    else
    {
      EEPROM.get(eeprom_address0, eepUserSet);
      web_message = "未更改";
    }
  }
  else web_message = "SetOutputPower 参数不存在";

  server.send(200, "text/html", web_message);
  display_partialLine(7, "发射功率：" + String(eepUserSet.outputPower) + web_message);
  eepUserSet.runMode = 4;
}

void webRead_fileManagement() //文件管理-向网页显示文件系统里的文件
{
  // 使用HTTP/1.1分块响应以避免生成巨大的临时字符串
  if (!server.chunkedResponseModeStart(200, "text/html")) {
    server.send(505, F("text/html"), F("HTTP1.1 required"));
    return;
  }

  String web_xx;          // web消息
  //web_xx.reserve(64);
  uint8_t fileCount = 0;  // 统计文件数量

  // 显示目录中文件名以及文件大小
  // 先统计有多少个可显示的文件 fileCount
  Dir dir = LittleFS.openDir(""); // 建立"目录"对象
  while (dir.next()) // dir.next()用于检查目录中是否还有"下一个文件"
  {
    String name = dir.fileName(); //获取文件名
    //检测到不是系统文件
    if (getSystemFile(name) == 0) fileCount++;
  }

  String fileManagement_name[fileCount];     // 收集文件的名称
  uint32_t fileManagement_size[fileCount];   // 收集文件的大小
  fileCount = 0;
  dir = LittleFS.openDir(""); // 建立"目录"对象
  while (dir.next()) // dir.next()用于检查目录中是否还有"下一个文件"
  {
    String name = dir.fileName(); //获取文件名
    if (getSystemFile(name) == 0) //检测到不是系统文件
    {
      fileManagement_name[fileCount] = name;
      fileManagement_size[fileCount] = dir.fileSize();
      fileCount++;
    }
  }
  //Serial.print("fileCount:"); Serial.println(fileCount);
  //计算剩余空间
  /*struct FSInfo {
    size_t totalBytes;    // 整个文件系统的大小
    size_t usedBytes;     // 文件系统所有文件占用的大小
    size_t blockSize;     // LittleFS块大小
    size_t pageSize;      // LittleFS逻辑页数大小
    size_t maxOpenFiles;  // 能够同时打开的文件最大个数
    size_t maxPathLength; // 文件名最大长度（包括一个字节的字符串结束符）
    };*/
  boolean info_state;            // 获取文件系统参数状态 0-失败 1-成功
  FSInfo p;                      // 创建结构体
  info_state = LittleFS.info(p); // 获取的值放在结构体中
  uint32_t sy_zj = p.totalBytes - p.usedBytes; //剩余空间，字节
  float sy_bfb = ((float)sy_zj / (float)p.totalBytes) * 100.0; //剩余空间，百分比
  float yy_bfb = 100.0 - sy_bfb; //已用空间百分比
  String sy_zj_str = byteConversion(sy_zj);
  //显示剩余空间进度条
  String nc_info = "已用" + String(yy_bfb) + "%，剩余" + sy_zj_str;
  web_xx = "<div class='container' onclick=\"show_toast('文件系统','" + nc_info + "')\">";
  web_xx += "<div class='progress'>";
  if (info_state) {
    web_xx += "<div class='progress-bar' style='width:" + String(yy_bfb) + "%'>已用" + String(yy_bfb) + "%</div>";
    web_xx += "<div class='progress-bar bg-success' style='width:" + String(sy_bfb) + "%'>剩余" + sy_zj_str + "</div>";
  }
  else web_xx += "<div class='progress-bar' style='width:0%'>Info获取失败</div>";
  web_xx += "</div>";
  web_xx += "</div><br>";
  //显示文件列表
  web_xx += "<table class='table table-sm'>";
  web_xx += "<thead>";
  web_xx += "<tr>";
  web_xx += "<th> </th>";
  web_xx += "<th>名称</th>";
  web_xx += "<th>大小</th>";
  web_xx += "</tr>";
  web_xx += "</thead>";
  web_xx += "<tbody>";

  server.sendContent(web_xx); //发送内容
  web_xx.clear();
  //显示文件
  for (uint8_t i = 0; i < fileCount; i++)
  {
    web_xx += "<tr>";

    web_xx += "<td>" + String(i + 1) + "</td>"; //序号

    web_xx += "<td>";
    web_xx += "<a href='#delete" + String(i + 1) + "' data-toggle='collapse'>" + fileManagement_name[i] + "</a>"; //文件名
    web_xx += "<div id='delete" + String(i + 1) + "' class='collapse'>";   //折叠的项目1 (删除&&重命名)
    web_xx += "<form style='margin:0px;display:inline' id='wjgl_delete" + String(i + 1) + "' name='" + fileManagement_name[i] + "'>";
    web_xx += "<button type='button' class='btn btn-danger btn-sm' style='margin-right:5%' onclick='tjxx_deleteFile(" + String(i + 1) + ")'>删除</button>";
    web_xx += "</form>";
    //Serial.print("文件:"); Serial.println(fileManagement_name[i]);
    if (fileManagement_name[i].endsWith(".bmp")) //检测到为bmp文件，多加一个按钮
    {
      if (fileManagement_name[i] == eepUserSet.customBmp) // 判断该文件是否启用中
      {
        web_xx += "<form style='margin:0px; display:inline' id='wjgl_stopBmp" + String(i + 1) + "' name='" + fileManagement_name[i] + "'>";
        web_xx += "<button type='button' class='btn btn-warning btn-sm' style='margin-right:5%' onclick='tjxx_stopBmp(" + String(i + 1) + ")'>停用</button>";
        web_xx += "</form>";
      }
      else
      {
        web_xx += "<form style='margin:0px; display:inline' id='wjgl_enableBmp" + String(i + 1) + "' name='" + fileManagement_name[i] + "'>";
        web_xx += "<button type='button' class='btn btn-success btn-sm' style='margin-right:5%' onclick='tjxx_enableBmp(" + String(i + 1) + ")'>启用</button>";
        web_xx += "</form>";
      }
    }
    web_xx += "<button type='button' class='btn btn-primary btn-sm' data-toggle='collapse' data-target='#rename" + String(i + 1) + "'>重命名</button>";
    web_xx += "<form style='margin:0px;display:inline' id='wjgl_rename" + String(i + 1) + "' target='iframe_rename'>";
    web_xx += "<div id='rename" + String(i + 1) + "' class='collapse'>"; //折叠的项目2
    web_xx += "<input type='text' class='form-control' name='cmm' placeholder='限26个字符,1中文=3' autocomplete='off' value='" + fileManagement_name[i] + "'>";
    web_xx += "<input type='text' class='form-control' name='" + fileManagement_name[i] + "' style='display:none;'>";
    web_xx += "<button type='submit' class='btn btn-primary btn-sm' onclick='tjxx_renameFile(" + String(i + 1) + ")'>确定修改</button>"; //重命名提交表格
    web_xx += "</div>"; //折叠的项目2
    web_xx += "</form>";
    web_xx += F("<iframe id='iframe_rename' name='iframe_rename' style='display:none;'></iframe>");  //不跳转页面

    web_xx += "</div>"; //折叠的项目1

    web_xx += "</td>";

    web_xx += "<td>" + byteConversion(fileManagement_size[i]) + "</td>";

    web_xx += "</tr>";

    server.sendContent(web_xx); //发送内容
    web_xx.clear();
  }

  //发送最后一个字符串
  web_xx += "</tbody>";
  web_xx += "</table>";
  server.sendContent(web_xx);          // 发送内容
  server.chunkedResponseFinalize();    // 分块响应完成
  display_partialLine(7, "文件管理器");
  web_xx.clear();
  //size_t web_xx_lenth = web_xx.length();
  //server.send(200, "text/html", web_xx.c_str(), web_xx_lenth); // 发送不了太长的String
  //display_partialLine(7, "文件管理器 " + String(web_xx_lenth));
  // ESP.wdtDisable();                     // 使能软件看门狗的触发间隔
  // Serial.println(web_xx);
  // ESP.wdtEnable(10000);                // 使能软件看门狗的触发间隔
}

void webRead_deleteFile() //删除文件
{
  String web_message;
  /*Serial.print("删除文件-参数个数 "); Serial.println(server.args());
    Serial.print("删除文件-参数0名称 "); Serial.println(server.argName(0));
    Serial.print("删除文件-参数0数值 "); Serial.println(server.arg("plain"));*/
  String deleteFlieName = server.arg("plain"); //获取需要删除的文件名称
  if (deleteFlieName.length() != 0)
  {
    if (LittleFS.remove(deleteFlieName))
      web_message = deleteFlieName + " 删除成功";
    else
      web_message = deleteFlieName + " 删除失败";
  }
  else web_message = deleteFlieName + " 参数不存在";
  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
}

void webRenameFile()
{
  /*Serial.print("删除文件-参数个数 "); Serial.println(server.args());
    Serial.print("删除文件-参数0名称 "); Serial.println(server.argName(0));
    Serial.print("删除文件-参数0数值 "); Serial.println(server.arg(0));

    Serial.print("删除文件-参数1名称 "); Serial.println(server.argName(1));
    Serial.print("删除文件-参数1数值 "); Serial.println(server.arg("cmm"));*/
  String web_message;
  String nweName;
  String oldName = server.argName(1); //获取旧的文件名称
  //查询是否存在某个参数 inAWord
  if (server.hasArg("cmm")) //查询是否存在某个参数
  {
    nweName = server.arg("cmm");
    //检查字符长度，太长提示不行
    if (nweName.length() > 29)  web_message = "名称过长";
    //检查自定义的一句话长度为0时
    else if (nweName.length() != 0)
    {
      if (LittleFS.rename(oldName, nweName))
        web_message = oldName + " 改为 " + nweName + " 成功";
      else
        web_message = oldName + " 改为 " + nweName + " 失败";
    }
    //名称未输入
    else web_message = "请输入新名称";
  }
  else web_message = "cmm 参数不存在";
  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
}

void webRead_updateButton() //文件上传超过上限的按钮
{
  String web_message;
  uint8_t file_count = 0; //文件数量统计
  Dir dir = LittleFS.openDir("");  // 建立“目录”对象
  while (dir.next()) file_count++; //记录有多少文件
  if (file_count >= FileLimit)  //文件数量小于32个，允许上传文件
  {
    web_message = "<div class='alert alert-warning  alert-dismissible fade show' style='margin:10px;'>";
    web_message += "<button type='button' class='close' data-dismiss='alert'>&times;</button>";
    web_message += "<strong>文件数量已达上限</strong></div>";
  }
  else {
    web_message = "";
  }
  server.send(200, "text/plain", web_message);
  display_partialLine(7, "文件上传");
}

void webRead_enableBmp() //启用Bmp文件
{
  EEPROM.get(eeprom_address0, eepUserSet);
  String web_message;
  String enableBmpName = server.arg("plain"); //获取需要删除的文件名称
  if (enableBmpName.length() != 0)
  {
    strcpy(eepUserSet.customBmp, enableBmpName.c_str()); // 要显示的自定义图片
    EEPROM.put(eeprom_address0, eepUserSet);
    EEPROM.commit(); //保存
    web_message = "自定义图片设为 " + enableBmpName;
  }
  else web_message = enableBmpName + " 参数不存在";
  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
  eepUserSet.runMode = 4;
}
void webRead_stopBmp() //停用Bmp文件
{
  EEPROM.get(eeprom_address0, eepUserSet);
  String web_message;
  String stopBmpName = server.arg("plain"); //获取需要删除的文件名称
  if (stopBmpName.length() != 0)
  {
    strcpy(eepUserSet.customBmp, String("").c_str()); // 要显示的自定义图片
    EEPROM.put(eeprom_address0, eepUserSet);
    EEPROM.commit(); //保存
    web_message = stopBmpName + "  已取消";
  }
  else web_message = stopBmpName + " 参数不存在";
  server.send(200, "text/plain", web_message);
  display_partialLine(7, web_message);
  eepUserSet.runMode = 4;
}
//*** 系统重启
void webRESET()
{
  String web_message = "系统准备重启";
  display_partialLine(7, web_message);
  web_message = "<meta charset = 'UTF-8'>系统重启，该页面已失效";
  server.send(200, "text/html", web_message);
  //WiFi.begin(sta_ssid, sta_password);  //要连接的WiFi名称和密码
  delay(1000);
  ESP.reset();
  //ESP.restart();
}

// 获取文件类型
String getContentType(String filename)
{
  if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

#define systemNameNum  9 //系统文件的数量
//判断该文件是否是系统文件
boolean getSystemFile(String fileName)
{
  /*const char* fileName_char = fileName.c_str();  //String转换char*
    String systemName[systemNameNum] = {
    "GCSBS.html",
    "GCSBS.html.gz"
    "bootstrap.min.css.gz",
    "toast.css.gz",
    "jquery.slim.min.js.gz",
    "bootstrap.bundle.min.js.gz",
    "toast.js.gz",
    "favicon.ico.gz",
    "edit",
    };
    for (uint8_t i = 0; i < systemNameNum; i++) {
    const char* systemName_char = systemName[i].c_str();  //String转换char
    if (strcmp(fileName_char, systemName_char) == 0) return 1;
    }
    return 0;*/
  String systemName[systemNameNum] = {
    "GCSBS.html",
    "GCSBS.html.gz",
    "bootstrap.min.css.gz",
    "toast.css.gz",
    "jquery.slim.min.js.gz",
    "bootstrap.bundle.min.js.gz",
    "toast.js.gz",
    "favicon.ico.gz",
    "edit",
  };
  for (uint8_t i = 0; i < systemNameNum; i++)
  {
    if (fileName == systemName[i]) return 1;
  }
  return 0;
}
