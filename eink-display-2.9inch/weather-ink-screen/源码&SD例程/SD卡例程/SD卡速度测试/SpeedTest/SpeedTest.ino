//文件系统对象的简单速度测试
//由Earle F.Philhower，III发布至公共领域
#include <FS.h>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>

//选择要测试的文件系统 3选1
//警告：文件系统将在测试开始时格式化！
//#define TESTFS LittleFS
//#define TESTFS SPIFFS
#define TESTFS SDFS

#define key3  5   // 墨水屏的按键3，可改
#define SD_CS 5   // SD卡选择引脚，可改
#define SCK   14  // 硬件SPI不可改
#define MOSI  13  // 硬件SPI不可改
#define MISO  12  // 硬件SPI不可改
#define SPI_SPEED SD_SCK_MHZ(40) //SD卡速度
boolean sdInitOk = 0; //SD卡挂载状态 0-失败 1-成功

//要测试的文件有多大
#define TESTSIZEKB 512

void DoTest(FS *fs)
{
  //自带FLASH用的初始化
  /*if (!fs->format()) {
    Serial.printf("无法 format(), 中止\n");
    return;
    }

    if (!fs->begin()) {
    Serial.printf("无法 begin(), 中止\n");
    return;
    }*/

  //SD卡用的初始化
  if (!SD.begin(SD_CS, SPI_SPEED)) {
    Serial.println("无法挂载SD卡, 已进入休眠");
    ESP.deepSleep(0, WAKE_RF_DEFAULT);
    return;
  }

  /*struct FSInfo {
    size_t totalBytes;    // 整个文件系统的大小
    size_t usedBytes;     // 文件系统所有文件占用的大小
    size_t blockSize;     // LittleFS块大小
    size_t pageSize;      // LittleFS逻辑页数大小
    size_t maxOpenFiles;  // 能够同时打开的文件最大个数
    size_t maxPathLength; // 文件名最大长度（包括一个字节的字符串结束符）
    };*/
  FSInfo p;          // 创建结构体byteConversion
  fs->info(p);      // 获取的值放在结构体中
  Serial.println("SD卡大小：" + String(p.totalBytes));

  Dir dir = fs->openDir("");  // 建立“目录”对象
  {
    while (dir.next())                // dir.next()用于检查目录中是否还有“下一个文件”
    {
      String fileName = dir.fileName(); //文件名
      size_t fileSize = dir.fileSize(); //文件大小
      if (!dir.isDirectory()) //非文件夹
      {
        Serial.print("文件:" + String(fileName));  Serial.println("，大小:" + String(fileSize));
        if (fileName == "testwrite.bin" || fileName == "test1b.bin")
        {
          Serial.print("删除文件:"); Serial.println(fileName);
          fs->remove(fileName); //删除
        }
      }
    }
  }

  uint8_t data[256];
  for (int i = 0; i < 256; i++) {
    data[i] = (uint8_t) i;
  }

  Serial.printf("正在创建 %dKB 文件, 可能需要一段时间...\n", TESTSIZEKB);
  long start = millis();
  File f = fs->open("/testwrite.bin", "w");
  if (!f) {
    Serial.printf("无法打开文件进行写入，中止\n");
    return;
  }
  for (int i = 0; i < TESTSIZEKB; i++)
  {
    ESP.wdtFeed();//喂狗
    for (int j = 0; j < 4; j++)
    {
      f.write(data, 256);
    }
  }
  f.close();
  long stop = millis();
  Serial.printf("==> 在256b块中写入%dKB的时间 = %ld毫秒\n", TESTSIZEKB, stop - start);

  f = fs->open("/testwrite.bin", "r");
  Serial.printf("==> 创建的文件大小 = %d\n", f.size());
  f.close();

  Serial.printf("以256b块顺序读取%dKB文件\n", TESTSIZEKB);
  start = millis();
  f = fs->open("/testwrite.bin", "r");
  for (int i = 0; i < TESTSIZEKB; i++)
  {
    ESP.wdtFeed();//喂狗
    for (int j = 0; j < 4; j++)
    {
      f.read(data, 256);
    }
  }
  f.close();
  stop = millis();
  Serial.printf("==> 按256b块顺序读取%dKB的时间 = %ld毫秒 = %ld字节/s\n", TESTSIZEKB, stop - start, TESTSIZEKB * 1024 / (stop - start) * 1000);

  Serial.printf("以256b块顺序读取闪存和RAM中的%dKB文件未对齐\n", TESTSIZEKB);
  start = millis();
  f = fs->open("/testwrite.bin", "r");
  f.read();
  for (int i = 0; i < TESTSIZEKB; i++)
  {
    ESP.wdtFeed();//喂狗
    for (int j = 0; j < 4; j++)
    {
      f.read(data + 1, 256);
    }
  }
  f.close();
  stop = millis();
  Serial.printf("==> 读取256b块中闪存和RAM中顺序未对齐的%dKB的时间 = %ld毫秒 = %ld字节/s\n", TESTSIZEKB, stop - start, TESTSIZEKB * 1024 / (stop - start) * 1000);


  Serial.printf("以256b块反向读取%dKB文件\n", TESTSIZEKB);
  start = millis();
  f = fs->open("/testwrite.bin", "r");
  for (int i = 0; i < TESTSIZEKB; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      ESP.wdtFeed();//喂狗
      if (!f.seek(256 + 256 * j * i, SeekEnd))
      {
        Serial.printf("无法搜索到%d，正在中止\n", -256 - 256 * j * i);
        return;
      }
      if (256 != f.read(data, 256))
      {
        Serial.printf("无法读取256字节，正在中止\n");
        return;
      }
    }
  }
  f.close();
  stop = millis();
  Serial.printf("==> 在256b块中反向读取%dKB的时间 = %ld毫秒 = %ld字节/s\n", TESTSIZEKB, stop - start, TESTSIZEKB * 1024 / (stop - start) * 1000);


  Serial.printf("以1字节块写入64K文件\n");
  start = millis();
  f = fs->open("/test1b.bin", "w");
  for (int i = 0; i < 65536; i++)
  {
    ESP.wdtFeed();//喂狗
    f.write((uint8_t*)&i, 1);
  }
  f.close();
  stop = millis();
  Serial.printf("==> 在1b块中写入64KB的时间 = %ld毫秒 = %ld字节/s\n", stop - start, 65536 / (stop - start) * 1000);

  Serial.printf("以1字节块读取64K文件\n");
  start = millis();
  f = fs->open("/test1b.bin", "r");
  for (int i = 0; i < 65536; i++)
  {
    ESP.wdtFeed();//喂狗
    char c;
    f.read((uint8_t*)&c, 1);
  }
  f.close();
  stop = millis();
  Serial.printf("==> 读取1b块中64KB的时间 = %ld毫秒 = %ld字节/s\n", stop - start, 65536 / (stop - start) * 1000);


}


void setup()
{
  Serial.begin(74880);
  Serial.printf("开始测试\n");
  Serial.flush();
  ESP.wdtEnable(5000);     //使能软件看门狗的触发间隔
  pinMode(SD_CS, INPUT_PULLUP);
  Serial.println("等待按下按键3");
  boolean sw = 0;
  while (sw == 0)
  {
    ESP.wdtFeed();//喂狗
    if (digitalRead(key3) == 0)
    {
      sw = 1;
      sdBeginCheck(); //挂载SD卡
      if (sdInitOk)DoTest(&TESTFS);
    }
  }
  ESP.deepSleep(0, WAKE_RF_DEFAULT); //WAKE_RF_DEFAULT  WAKE_RFCAL  WAKE_NO_RFCAL  WAKE_RF_DISABLED RF_DISABLED
}

void loop()
{
  ;
}

boolean sdBeginCheck() //SD挂载检查
{
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, 1);
  delay(1);
  SDFS.end();
  if (SD.begin(SD_CS, SPI_SPEED))
  {
    Serial.println("SD卡挂载成功");
    sdInitOk = 1;
    return 1;
  }
  else
  {
    Serial.println("无法挂载SD卡");
    sdInitOk = 0;
    return 0;
  }
  return 0;
}
