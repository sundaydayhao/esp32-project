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
  //u8g2Fonts.setFontMode(0);
  //display.init(0, 0, 10, 1);
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

void BWClearScreen()
{
  //display.init(0, 0, 10, 1);   //串口使能 初始化完全刷新使能 复位时间 ret上拉使能
  display.setPartialWindow(0, 0, display.width(), display.height()); //设置局部刷新窗口
  display.firstPage();
  do
  {
    display.fillScreen(heise);  // 填充屏幕
    display.display(1);         // 显示缓冲内容到屏幕，用于全屏缓冲
    display.fillScreen(baise);  // 填充屏幕
    display.display(1);         // 显示缓冲内容到屏幕，用于全屏缓冲
  }
  while (display.nextPage());
}
//****** 消除四周的黑边 ******
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
  SPI.beginTransaction(spi_settings); //开始通讯
  if (cs >= 0) digitalWrite(cs, LOW); //cs
  SPI.transfer(d);
  if (cs >= 0) digitalWrite(cs, HIGH); //cs
  SPI.endTransaction();//结束通讯
}
void xiaobian() //消除黑边（四周的边跟随屏幕刷新，仅全局刷新有效）
{
  EPD_writeCommand(0x3c);  // 边界波形控制寄存器
  EPD_writeData(0x33);     // 向里面写入数据

  //EPD_writeCommand(0x2c); // VCOM setting
  //EPD_writeData(0xA1);    // * different   FPC丝印A1 库默认A8
}

//SD卡挂载失败会导致软看门狗重启
boolean sdBeginCheck() //SD挂载检查
{
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, 1);
  delay(1);
  SDFS.end();
  RTC_SDInitError = 1;
  ESP.rtcUserMemoryWrite(RTCdz_SDInitError, &RTC_SDInitError, sizeof(RTC_SDInitError));
  if (SD.begin(SD_CS, SPI_SPEED))
  {
    RTC_SDInitError = 0;
    ESP.rtcUserMemoryWrite(RTCdz_SDInitError, &RTC_SDInitError, sizeof(RTC_SDInitError));
    Serial.println("SD卡挂载成功");
    sdInitOk = 1;
    return 1;
  }
  else
  {
    RTC_SDInitError = 0;
    ESP.rtcUserMemoryWrite(RTCdz_SDInitError, &RTC_SDInitError, sizeof(RTC_SDInitError));
    Serial.println("无法挂载SD卡");
    sdInitOk = 0;
    return 0;
  }
  return 0;
}

/*void sd_init()
{
  pinMode(SD_CS, OUTPUT);
  pinMode(SCK, OUTPUT);
  pinMode(MOSI, OUTPUT);
  digitalWrite(SD_CS, 1);
  digitalWrite(SCK, 0);
  digitalWrite(MOSI, 0);
}

void sdcs_init()
{
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, 1);
}*/
