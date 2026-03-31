/*
  适用 8266-4MB(FS3MB OTA~512Kb)
  
  版本介绍
  V001: 初步测试SD的挂载、按键、墨水屏显示列表。
  因为8266引脚不够用，SD卡的CS引脚与按键3共用，
  故每次调用完按键就必须卸载SD卡再挂载SD卡。

  V002: 正在构建，会放出比较完整的SD卡、按键、墨水屏联动显示例程
*/

/*
  1.中文的实现由U8g2_for_Adafruit_GFX提供
  原版对8266大字库支持不好，故使用我修改的过的库
  字库为GB2312+ASCII+ASCII扩展+自添加字

  2.屏幕的驱动由GxEPD2提供
  默认8266接屏幕的引脚定义
  GPIO4-BUSY
  GPIO2-RES
  GPIO0-DC
  GPIO15-CS
  GPIO14-SCLK
  GPIO13-SDI
*/


//基类gxepd2u GFX可用于将引用或指针作为参数传递到显示实例，使用~1.2k更多代码
//启用或禁用GxEPD2_GFX基类
#include <ESP8266WiFi.h>
#define ENABLE_GxEPD2_GFX 1
#include <GxEPD2_BW.h>
//#include <GxEPD2_3C.h>
//#include <GxEPD2_7C.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SPI.h>
#include <SD.h>
SPISettings spi_settings(4000000, MSBFIRST, SPI_MODE0); //SPI设置
GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(GxEPD2_290(/*CS*/ 15, /*DC*/ 0, /*RST*/ 2, /*BUSY*/ 4));                         // GDEM029A01
//GxEPD2_BW<GxEPD2_290_T94, GxEPD2_290_T94::HEIGHT> display(GxEPD2_290_T94(/*CS=D8*/ 15, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4)); // GDEM029T94
//GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(GxEPD2_290_T94_V2(/*CS*/ 15, /*DC*/ 0, /*RST*/ 2, /*BUSY*/ 4));    // GDEM029T94, Waveshare 2.9" V2 variant
//GxEPD2_BW<GxEPD2_290_I6FD, GxEPD2_290_I6FD::HEIGHT> display(GxEPD2_290_I6FD(/*CS=15*/ SS, /*DC=4*/ 4, /*RST=2*/ 2, /*BUSY=5*/ 5)); // GDEW029I6FD
//GxEPD2_BW<GxEPD2_290_T5, GxEPD2_290_T5::HEIGHT> display(GxEPD2_290_T5(/*CS=D8*/ 15, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4));    // GDEW029T5
//GxEPD2_BW<GxEPD2_290_T5D, GxEPD2_290_T5D::HEIGHT> display(GxEPD2_290_T5D(/*CS=D8*/ 15, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4));   // GDEW029T5D
//GxEPD2_BW<GxEPD2_290_M06, GxEPD2_290_M06::HEIGHT> display(GxEPD2_290_M06(/*CS=D8*/ 15, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4)); // GDEW029M06
//GxEPD2_3C<GxEPD2_290c, GxEPD2_290c::HEIGHT> display(GxEPD2_290c(/*CS*/ 15, /*DC*/ 0, /*RST*/ 2, /*BUSY*/ 4));                      // GDEW029Z10
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
#define baise  GxEPD_WHITE  //白色
#define heise  GxEPD_BLACK  //黑色

#define key3 5   // 墨水屏的按键3，可改
#define SD_CS 5  // SD卡选择引脚，可改
#define SCK 14   // 硬件SPI不可改
#define MOSI 13  // 硬件SPI不可改
#define MISO 12  // 硬件SPI不可改
#define BAT_SWITCH 12 //控制电池电压测量的开关，可改

#define SPI_SPEED SD_SCK_MHZ(40)    // SD卡频率

uint32_t RTC_SDInitError = 0;    // SD挂载错误 0-无 1-错误
#define RTCdz_SDInitError   0    // RTC数据-地址 SD挂载错误 0-无 1-错误

//声明gb2312.c
#include "gb2312.c"
//声明外部字体常量
extern const uint8_t chinese_gb2312[252730] U8G2_FONT_SECTION("chinese_gb2312");
File root;

boolean sdInitOk = 0; //SD卡挂载状态 0-失败 1-成功

void setup()
{
  WiFi.mode(WIFI_OFF); // 关闭wifi
  Serial.begin(74880);
  ESP.wdtEnable(5000);     //使能软件看门狗的触发间隔

  //Serial.print("上次重启原因："); Serial.println(ESP.getResetReason());

  //屏幕初始化
  //display.init();
  display.init(0, 1, 10, 1); // 串口使能 初始化完全刷新使能 复位时间 ret上拉使能
  xiaobian();                  // 消除黑边
  display.setRotation(3);      // 设置旋转方向 0-0° 1-90° 2-180° 3-270°
  u8g2Fonts.begin(display);             // 将u8g2过程连接到Adafruit GFX
  u8g2Fonts.setFontMode(1);             // 使用u8g2透明模式（这是默认设置）
  u8g2Fonts.setFontDirection(0);        // 从左到右（这是默认设置）
  u8g2Fonts.setForegroundColor(heise);  // 设置前景色
  u8g2Fonts.setBackgroundColor(baise);  // 设置背景色
  u8g2Fonts.setFont(chinese_gb2312);    // 设置字体

  if (ESP.getResetReason() == "Software Watchdog")
  {
    ESP.rtcUserMemoryRead(RTCdz_SDInitError, &RTC_SDInitError, sizeof(RTC_SDInitError));
    if (RTC_SDInitError == 1)
    {
      Serial.println("因SD卡挂载超时导致软看门狗重启");
      Serial.println("请检查SD卡或电路");
      Serial.println("已进入休眠状态");
      display_partialLine(0, "因SD卡挂载超时导致软看门狗重启");
      display_partialLine(1, "请检查SD卡或电路");
      display_partialLine(2, "已进入休眠状态");
      esp_sleep(0);
    }
  }

}

void loop()
{
  pinMode(key3, INPUT_PULLUP);
  Serial.println("等待按下按键3");
  display_partialLine(0, "按下按键3初始化SD卡");
  display_partialLine(7, "1电池电压：" + String(getBatVolNew()) + "V");
  boolean sw = 0;
  while (sw == 0)
  {
    ESP.wdtFeed();//喂狗
    if (digitalRead(key3) == 0)
    {
      sw = 1;
      sdBeginCheck();
      if (sdInitOk)
      {
        root = SD.open("/");
        printDirectory(root, 0); //打印目录
        Serial.println("文件列表输出完毕");
        Serial.println(" ");

        root = SD.open("/");
        File myFile = SD.open("测试.txt"); // 打开测试.txt文件
        if (myFile)
        {
          Serial.println("测试.txt:");
          while (myFile.available()) Serial.write(myFile.read());//串口输出内容
          myFile.close();
        }
        else Serial.println("打开失败 测试.txt");
        Serial.println(" "); Serial.println(" ");
      }
    }
  }

  //BWClearScreen();//黑白清屏
  display_partialLine(1, "按下按键3输出文件列表");
  pinMode(key3, INPUT_PULLUP);
  sw = 0;
  while (sw == 0)
  {
    ESP.wdtFeed();//喂狗
    if (digitalRead(key3) == 0)
    {
      sw = 1;
      sdBeginCheck();
      display_partialLine(1, "文件列表");
      uint8_t count = 2;
      root = SD.open("/");
      while (1) //输出列表
      {
        ESP.wdtFeed();//喂狗
        File entry = root.openNextFile();
        // Serial.print("entry:"); Serial.println(entry);
        if (! entry) break;
        String fileName = entry.name(); //文件名
        size_t fileSize = entry.size(); //文件大小
        Serial.print(fileName); Serial.print(" "); Serial.println(fileSize);
        String xiaoxi = String(count - 1) + ". " + fileName + "  " + String(fileSize) + "字节";
        display_partialLine(count, xiaoxi);
        count++;
        entry.close();
      }
    }
  }
  display_partialLine(7, "2电池电压：" + String(getBatVolNew()) + "V");
  esp_sleep(0);
}



//打印目录
void printDirectory(File dir, int numTabs)
{
  while (true)
  {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory())
    {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    }
    else
    {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.print(entry.size(), DEC);
      time_t cr = entry.getCreationTime();
      time_t lw = entry.getLastWrite();
      struct tm * tmstruct = localtime(&cr);
      Serial.printf("\tCREATION: %d-%02d-%02d %02d:%02d:%02d", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
      tmstruct = localtime(&lw);
      Serial.printf("\tLAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
    }
    entry.close();
  }
}
