//   >>> >>> 常见问题 <<< <<<
// * 按住按键3不放，再按复位按键，即可进入模式选择界面(按键3-GPIO5 按键2-GPIO0)
// * 所有界面的按键操作逻辑为：
// *  a.单独短按为切换选项
// *  b.长按按键3为确认操作或调出菜单

// * 库安装问题，码云内可找到
// * 原版U8g2_for_Adafruit_GFX库无法使用大字库，请使用我修改的
// * RX8025T库使用我提供的V1.0版本
// * 其他库均可在库管理器下载的到

// * 其他问题
// * 按键2不可按得太快，不能在屏幕刷新的时候按，会导致屏幕死机，原因是按键2与屏幕刷新共用一个io口
// * 无法连接wifi请检查是否被路由器拉黑
// * 无法获取天气信息请检查城市名是否填对，免费用户只能查看到地级市
// * 低压休眠的请检查电池测量电路是否正常，电池电压是否大于3.3V（搭板的玩家自己给A0加上分压电路接上5V，分压后不能超过1V，否则烧ADC）

//已知BUG1，在配网页面连接无效的的WIFI会卡一段时间，因为模块在后台连接新网络，大概10S左右，连接失败就会主动关闭STA模式（硬件限制）
//已知BUG2，配网模式容易重启

//    复位     按键2    按键3
//    +--+    +--+    +--+
// +--+--+----+--+----+--+-------------------------------------+
// |                                                           |
// |        +----------------------------------------------+   |
// |        |                                              |   |
// |        |     天气模式                     阅读模式       |   |
// |        |                                              |   |
// |        |                                              |   |
// |        |                                              |   |
// |        |                                              |   |
// |        |     时钟模式                     配网模式       |   |
// |        |                                              |   |
// |        +----------------------------------------------+   |
// |                                                           |
// +-----------------------------------------------------------+
//基类gxepd2u GFX可用于将引用或指针作为参数传递到显示实例，使用~1.2k更多代码
//启用或禁用GxEPD2_GFX基类
#define ENABLE_GxEPD2_GFX 1
//****** 可调参数 & 引脚配置 ******
static const String version = "014-02";    // 程序版本
static const String version1 = "A3";       // 程序版本

#define BatLow          3.3         // 低压提示和永不唤醒电压
#define noUpdateTime    5           // 夜间不更新时间截止时间
#define FileLimit       32          // 文件系统文件数量上限
#define jiaoZhunTime    20          // 校准间隔，已停用
//#define clockShuaXin    9           // 时钟局刷XX次就全局刷新一次，停用
#define key_lb_max      3           //按键滤波次数

#define LED             2           // 8266模块板载LED，未启用
#define bat_switch_pin  12          // 电池电压读取开关
#define bat_vcc_pin     A0          // 读取电池电压引脚，不能超过1V
#define key_zj          0           // 中间的按键，也是烧录模式选择按键
#define key_yb          5           // 右边的按键，进入配网模式的按键+
#define SD_CS           5           // SD卡选择开关
boolean bright = LOW;               // 默认亮灯的电平，未启用
float bat_vcc = 0.0;                // 电池电压
#define SPI_SPEED SD_SCK_MHZ(20)    // SD卡频率
#define DBG_OUTPUT_PORT Serial
//****************************************************************
#include <GxEPD2_BW.h>
//#include <GxEPD2_3C.h>
//#include <GxEPD2_7C.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SPI.h>
#include <SD.h>
SPISettings spi_settings(4000000, MSBFIRST, SPI_MODE0);
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

//声明gb2312.c
#include "gb2312.c"
//声明外部字体常量
extern const uint8_t chinese_gb2312[253023] U8G2_FONT_SECTION("chinese_gb2312");
//图标声明
extern const unsigned char Bitmap_setup0[] PROGMEM;
extern const unsigned char Bitmap_xiaohei[] PROGMEM;
extern const unsigned char Bitmap_byk[] PROGMEM;
extern const unsigned char Bitmap_byx[] PROGMEM;
extern const unsigned char Bitmap_567[] PROGMEM;
extern const unsigned char Bitmap_wlq1[] PROGMEM;
extern const unsigned char Bitmap_wlq2[] PROGMEM;
extern const unsigned char Bitmap_wlq3[] PROGMEM;
extern const unsigned char Bitmap_wlq4[] PROGMEM;
extern const unsigned char Bitmap_qt[] PROGMEM;
extern const unsigned char Bitmap_dy[] PROGMEM;
extern const unsigned char Bitmap_yt[] PROGMEM;
extern const unsigned char Bitmap_zheny[] PROGMEM;
extern const unsigned char Bitmap_lzy[] PROGMEM;
extern const unsigned char Bitmap_lzybbb[] PROGMEM;
extern const unsigned char Bitmap_xy[] PROGMEM;
extern const unsigned char Bitmap_zhongy[] PROGMEM;
extern const unsigned char Bitmap_dayu[] PROGMEM;
extern const unsigned char Bitmap_by[] PROGMEM;
extern const unsigned char Bitmap_dby[] PROGMEM;
extern const unsigned char Bitmap_tdby[] PROGMEM;
extern const unsigned char Bitmap_dongy[] PROGMEM;
extern const unsigned char Bitmap_yjx[] PROGMEM;
extern const unsigned char Bitmap_zhenx[] PROGMEM;
extern const unsigned char Bitmap_xx[] PROGMEM;
extern const unsigned char Bitmap_zhongx[] PROGMEM;
extern const unsigned char Bitmap_dx[] PROGMEM;
extern const unsigned char Bitmap_bx[] PROGMEM;
extern const unsigned char Bitmap_fc[] PROGMEM;
extern const unsigned char Bitmap_ys[] PROGMEM;
extern const unsigned char Bitmap_scb[] PROGMEM;
extern const unsigned char Bitmap_w[] PROGMEM;
extern const unsigned char Bitmap_m[] PROGMEM;
extern const unsigned char Bitmap_f[] PROGMEM;
extern const unsigned char Bitmap_jf[] PROGMEM;
extern const unsigned char Bitmap_ljf[] PROGMEM;
extern const unsigned char Bitmap_wz[] PROGMEM;
extern const unsigned char Bitmap_qt_ws[] PROGMEM;
extern const unsigned char Bitmap_yt_wz[] PROGMEM;
extern const unsigned char Bitmap_dy_wz[] PROGMEM;
extern const unsigned char Bitmap_zy_wz[] PROGMEM;
extern const unsigned char Bitmap_zx_wz[] PROGMEM;
extern const unsigned char Bitmap_weizhi[] PROGMEM;
extern const unsigned char Bitmap_zhuangtai[] PROGMEM;
extern const unsigned char Bitmap_gengxing[] PROGMEM;
extern const unsigned char Bitmap_riqi[] PROGMEM;
extern const unsigned char Bitmap_batlow[] PROGMEM;
extern const unsigned char Bitmap_humidity[] PROGMEM;
extern const unsigned char Bitmap_fx[] PROGMEM;
extern const unsigned char Bitmap_tempSHT30[] PROGMEM;
extern const unsigned char Bitmap_humiditySHT30[] PROGMEM;
extern const unsigned char Bitmap_peiwangMod[] PROGMEM;
extern const unsigned char Bitmap_shizhongMod[] PROGMEM;
extern const unsigned char Bitmap_yueduMod[] PROGMEM;
extern const unsigned char Bitmap_tianqiMod[] PROGMEM;
extern const unsigned char Bitmap_txtMain[] PROGMEM;
extern const unsigned char Bitmap_yxm_yzq_50[] PROGMEM;
extern const unsigned char Bitmap_dlsd[] PROGMEM;
extern const unsigned char Bitmap_bat1[] PROGMEM;
extern const unsigned char Bitmap_bat2[] PROGMEM;
extern const unsigned char Bitmap_bat3[] PROGMEM;
extern const unsigned char Bitmap_sjjg[] PROGMEM;
extern const unsigned char Bitmap_dlxtb[] PROGMEM;
extern const unsigned char Bitmap_ljdk[] PROGMEM;
extern const unsigned char Bitmap_pwewm[] PROGMEM;
extern const unsigned char Bitmap_dfsn[] PROGMEM;
extern const unsigned char Bitmap_zdy[] PROGMEM;
extern const unsigned char Bitmap_jintian[] PROGMEM;
extern const unsigned char Bitmap_bilbil[] PROGMEM;
extern const unsigned char Bitmap_kon[] PROGMEM;
extern const unsigned char Bitmap_wifidk[] PROGMEM;
//****************************************************************
#include <ESP8266WiFi.h>
#include <include/WiFiState.h> // WiFiState结构详图
WiFiState state;
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP_EEPROM.h>  //根据官方库制作的升级版库，带磨损均衡
#include <Wire.h>
//#include <Ticker.h> //软件定时器库
//Ticker txtClock; //软件时钟计时用

//硬件定时器
#define USING_TIM_DIV1                false   // 最短最准确的计时器
#define USING_TIM_DIV16               true    // 用于中等时间和中等精度计时器
#define USING_TIM_DIV256              false   // 计时器最长，但最不准确。默认  26.843542秒
#include "ESP8266TimerInterrupt.h" //硬件定时器库
ESP8266Timer ITimer; //硬件定时器用

#include "ClosedCube_SHT31D.h"
ClosedCube_SHT31D sht3xd;

//#include "LittleFS.h"
#include <LittleFS.h> // 官方要求的新文件系统库  #include "FS.h"未来将不会得官方支持，已弃用
const char* fsName = "LittleFS";
FS* fileSystem = &LittleFS;
LittleFSConfig fileSystemConfig = LittleFSConfig();

File fsUploadFile;           // 建立文件对象用于闪存文件上传
static bool fsOK;
String unsupportedFiles = String();
static const char TEXT_PLAIN[] PROGMEM = "text/plain";
static const char FS_INIT_ERROR[] PROGMEM = "FS INIT ERROR";
static const char FILE_NOT_FOUND[] PROGMEM = "FileNotFound";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp1.aliyun.com", 8 * 3600, 60000); //udp，服务器地址，时间偏移量，更新间隔

//****** 8025T时钟芯片，支持拆机RX8025T原装BL8025T，不支持全新RX8025T ******
#include <TimeLib.h>
#include <Wire.h>
#include "BL8025_RTC.h"
BL8025_RTC rtc;

//****** HTTP服务器配置 ******
const String language = "zh-Hans";  // 请求语言
const String url_yiyan = "https://v1.hitokoto.cn/?encode=json&min_length=1&max_length=21";//一言获取地址
//****** WebServer设置 ******
ESP8266WebServer server(80); //建立网络服务器对象，该对象用于响应HTTP请求。监听端口（80）
ESP8266HTTPUpdateServer httpUpdater; //建立httpOTA对象
//****** STA设置
char sta_ssid[32] = {0};
char sta_password[64] = {0};
IPAddress dns1(114, 114, 114, 114);
IPAddress dns2(114, 114, 115, 115);
//****** AP设置
const char *ap_ssid = "ESP8266 E-Paper";
const char *ap_password = "333333333"; //无密码则为开放式网络 9个3
IPAddress local_IP(192, 168, 3, 3);
IPAddress gateway(192, 168, 3, 3);
IPAddress subnet(255, 255, 255, 0);

//****** 天气数据
struct ActualWeather
{
  char status_code[64];  // 错误代码
  char city[16];         // 城市名称
  char weather_name[16]; // 天气现象名称
  char weather_code[4];  // 天气现象代码
  char temp[5];          // 温度
  char last_update[25];  // 最后更新时间
};
ActualWeather actual;   // 创建结构体变量 目前的天气

struct FutureWeather
{
  char status_code[64];       // 错误代码

  char date0[14];             // 今天日期
  char date0_text_day[20];    // 白天天气现象名称
  char date0_code_day[4];     // 白天天气现象代码
  char date0_text_night[16];  // 晚上天气现象名称
  char date0_code_night[4];   // 晚上天气现象代码
  char date0_high[5];         // 最高温度
  char date0_low[5];          // 最低温度
  char date0_humidity[5];     // 相对湿度
  char date0_wind_scale[5];   // 风力等级

  char date1[14];             // 明天日期
  char date1_text_day[20];    // 白天天气现象名称
  char date1_code_day[4];     // 白天天气现象代码
  char date1_text_night[16];  // 晚上天气现象名称
  char date1_code_night[4];   // 晚上天气现象代码
  char date1_high[5];         // 最高温度
  char date1_low[5];          // 最低温度

  char date2[14];             // 后天日期
  char date2_text_day[20];    // 白天天气现象名称
  char date2_code_day[4];     // 白天天气现象代码
  char date2_text_night[16];  // 晚上天气现象名称
  char date2_code_night[4];   // 晚上天气现象代码
  char date2_high[5];         // 最高温度
  char date2_low[5];          // 最低温度
};
FutureWeather future; //创建结构体变量 未来天气

struct RiQi       // 日期 https://api.xygeng.cn/day
{
  int code;       // 错误代码
  char year[5];   // 年
  char month[3];  // 月
  char date[3];   // 日
  char day[12];   // 星期几
  char festival[64]; // 节日名
}; RiQi riqi; // 创建结构体变量 未来

struct LifeIndex // 生活指数
{
  char status_code[64];  // 错误代码
  char uvi[10];          // 紫外线指数
}; LifeIndex life_index; // 创建结构体变量 生活指数

struct Hitokoto  // 一言API
{
  char status_code[64]; //错误代码
  char hitokoto[70];
  //https://v1.hitokoto.cn?encode=json&charset=utf-8&min_length=1&max_length=21
  //https://pa-1251215871.cos-website.ap-chengdu.myqcloud.com/
};
Hitokoto yiyan; //创建结构体变量 一言

//****** EEPROM地址和定义 ******
#define eeprom_address0 0    //起始地址
struct EEPROMStruct
{
  uint8_t auto_state;        // 自动刷写eeprom状态 0-需要 1-不需要
  char  city[30];            // 城市
  char  weatherKey[24];      // 天气KEY
  boolean nightUpdata;       // 夜间更新 1-更新 0-不更新
  char  inAWord[67];         // 自定义一句话
  uint8_t inAWord_mod;       // 自定义一句话的模式 0-联网获取句子 1-自定义句子 2-天数倒计时 3-B站粉丝
  boolean batDisplayType;    // 电量显示类型 0-电压 1-百分比
  uint8_t runMode;           // 0-模式选择页面 1-天气 2-阅读 3-时钟 4-配网
  char txtNameLastTime[32];  // 上一次打开的txt文件名
  char customBmp[32];        // 要显示的自定义图片
  int16_t clockCompensate;   // 时钟手动补偿值
  float outputPower;         // 设置发射功率
  boolean setRotation;       // 屏幕旋转方向0-90度（1） 1-270度（3）
  boolean clock_display_state;     // 时钟模式是否开启日期显示
  boolean clock_calibration_state; // 时钟模式是否开启强制校准
  uint8_t clockJZJG;               // 时钟模式校准间隔 分钟
  boolean albumAuto;               // 相册自动动播放 0-关闭 1-开启
  boolean fastFlip;                // 快速翻页 0-关闭 1-开启
  uint8_t clockQSJG;               // 时钟模式全局刷新间隔
  boolean sdState;                 // 启用SD卡 1-启用 0-未启用
  boolean type_8025T;              // 8025t 的类型 0-BL(按顺序读取) 1-RX(读取的数据会偏移8位)
} eepUserSet;

#define eeprom_address1 sizeof(EEPROMStruct)    //起始地址
struct eepClock {
  char year[5];       // 年
  char month[3];      // 月
  char day[3];        // 日
  /*uint8_t hour;       // 时
    uint8_t minute;     // 分
    uint8_t seconds;    // 秒*/
  char week[12];      // 星期几
  char festival[64];  // 节日名
} eepUserClock;

//****** 一些变量 ******
size_t overtime = 0;
boolean displayRunModState = 0;  // 显示模式选择界面的状态 1-显示 2-不显示
boolean displayUpdate = 1;       // 模式选择界面更新发送 0-不更新 1-更新

boolean webDisplayQh_state = 0;    // 在配网切换屏幕的状态 0-未切换 1-切换完毕

//String webServer_news = "";     // web回传消息
//uint8_t client_count = 0;       // 连接服务器的超时计数,暂未使用
//uint8_t client_error = 0;       // 错误代码,暂未使用
int dht30_error = 0;            // dht30错误代码
float dht30_temp = 0.0;         // dht30温度
float dht30_humi = 0.0;         // dht30湿度
boolean bmp_state = 0;          // bmp文件是否存在，0-不存在 1-存在

boolean ClockSkipState = 0;
boolean sdInitOk = 0;          // SD卡挂载成功状态 1-成功 0-失败
//****** TXT阅读器相关 ******
File txtFile;                  // 本次打开的txt文件系统对象
File indexesFile;              // 本次打开的索引文件系统对象
String txtName[3] = {};        // 存储书本的名字,最多支持3本
size_t txtSize[3] = {0, 0, 0}; // 存储书本的大小
String indexesName = "";       // 创建索引文件名称，页数的位置从第二页开始记录，文件末尾倒数7个字节用来记录当前看到的页数
uint8_t txtNum = 0;            // 本次要打开的txt文件序号 0或1或2
uint32_t txt_zys = 0;          // 本次的txt总页数，由索引计算得出

boolean txtInitState = 1;        // TXT阅读器 初始化    1使能

uint8_t txtDisplayPage = 0;         // txt模式要显示的界面
#define txtFileDisplay         0    // 主界面，文字阅读界面
#define txtMainDisplay         1    // 主界面，文字阅读界面
#define txtMenuMainDisplay     2    // 菜单界面
#define txtMenukeyBoardDisplay 3    // 菜单的键盘界面
#define txtMenuClockDisplay    4    // 菜单的时钟校准界面

uint8_t txtFileMainCount = 0;  // TXT文件选择界面选框位置
uint8_t txtMenuCount = 0;      // TXT阅读菜单界面选框位置
int8_t keyBoardCount = 1;      // 键盘界面选框位置   0 1 2 3 4 5 6 7 8 9 < ok

uint16_t keyBoardNum = 0;      // 键盘的数

uint8_t PageChange = 0;        // 换页 0-无 1-下一页 2-上一页

uint8_t pageUpdataCount = 0;   // 页局刷计数
uint32_t pageTop = 0;          // 当前页首的光标位置
uint32_t pageCurrent = 0;      // 当前页

//RTC临时数据
uint32_t RTC_minute = 100;       // RTC数据-分钟
uint32_t RTC_hour = 100;         // RTC数据-小时
uint32_t RTC_seconds = 100;      // RTC数据-秒
uint32_t RTC_cq_cuont = 0;       // RTC数据-重启计数
uint32_t RTC_clockTime2 = 0;     // RTC数据-上次的时间
uint32_t RTC_wifi_ljcs = 0;      // RTC数据-wifi连接超时计数
uint32_t RTC_jsjs = 0;           // RTC数据-局刷计数
uint32_t RTC_ntpTimeError = 0;   // 获取时间超时 0-无 1-超时 2-wifi连接超时
uint32_t RTC_clock_code = 0;     // 时钟错误代码
uint32_t RTC_Bfen_code = 0;      // B粉错误代码
uint32_t RTC_follower = 0;       // B粉数量 //http://api.bilibili.com/x/relation/stat?vmid=16758526&jsonp=jsonp
uint32_t RTC_8025T_error = 0;    // BL8025T时钟芯片错误检测 0-正常 1-年错 2-月错 3-日错 4-时错 5-分错 6-秒错
uint32_t RTC_albumAutoOldWz = 0; // 随机轮播图片的上一次位置
uint32_t RTC_tqmskjxs = 1;       // 天气模式开机显示 0-不显示 其他-显示
uint32_t RTC_SDInitError = 0;    // SD挂载错误 0-无 1-错误
uint32_t clockTime0, clockTime1;
uint32_t clockTime3 = 0;

#define RTCdz_hour           0                      // RTC数据-地址 小时
#define RTCdz_minute         RTCdz_hour+1           // RTC数据-地址 分钟
#define RTCdz_seconds        RTCdz_minute+1         // RTC数据-地址 秒
#define RTCdz_cq_cuont       RTCdz_seconds+1        // RTC数据-地址 重启计数
#define RTCdz_clockTime2     RTCdz_cq_cuont+1       // RTC数据-地址 time2
#define RTCdz_wifi_ljcs      RTCdz_clockTime2+1     // RTC数据-地址 wifi连接超时计数
#define RTCdz_jsjs           RTCdz_wifi_ljcs+1      // RTC数据-地址 局刷计数
#define RTCdz_ntpTimeError   RTCdz_jsjs+1           // RTC数据-地址 NTP错误类型
#define RTCdz_clock_code     RTCdz_ntpTimeError+1   // RTC数据-地址 时钟错误代码
#define RTCdz_Bfen_code      RTCdz_clock_code+1     // RTC数据-地址 B粉错误代码
#define RTCdz_follower       RTCdz_Bfen_code+1      // RTC数据-地址 B粉数量
#define RTCdz_tm_error       RTCdz_follower+1       // RTC数据-地址 BL8025T时钟芯片错误检测
#define RTCdz_albumAutoOldWz RTCdz_tm_error+1       // RTC数据-地址 随机轮播图片的上一次位置
#define RTCdz_tqmskjxs       RTCdz_albumAutoOldWz+1 // RTC数据-地址 天气模式开机显示 0-不显示 其他-显示

void setup()
{
  clockTime0 = millis();

  WiFi.mode(WIFI_OFF);        //  设置工作模式

  if (getBatVolNew() <= 3.1) esp_sleep(0); //电量实在太低了

  delay(3);

  //Serial.begin(74880);
  DBG_OUTPUT_PORT.begin(74880);
  DBG_OUTPUT_PORT.setDebugOutput(false);

  auto_eeprom();

  ESP.wdtEnable(8000);     //使能软件看门狗的触发间隔

  not_updated_at_night();   //夜间不更新检测

  //******  电池电压获取和dht30数据获取 ******
  bat_vcc = getBatVolNew();
  get_dht30_data();

  //******  ntp时间服务初始化 ******
  timeClient.begin();

  //****** 屏幕初始化 ******
  //display.init();
  display.init(0, 0, 10, 0); // 串口使能 初始化完全刷新使能 复位时间 ret上拉使能
  xiaobian();                // 消除黑边
  if (eepUserSet.setRotation) display.setRotation(3);  // 设置方向
  else                        display.setRotation(1);
  u8g2Fonts.begin(display);                        // 将u8g2过程连接到Adafruit GFX
  u8g2Fonts.setFontMode(1);                        // 使用u8g2透明模式（这是默认设置）
  u8g2Fonts.setFontDirection(0);                   // 从左到右（这是默认设置）
  u8g2Fonts.setForegroundColor(heise);             // 设置前景色
  u8g2Fonts.setBackgroundColor(baise);             // 设置背景色
  u8g2Fonts.setFont(chinese_gb2312);

  //****** 进入选择模式判断 ******
  pinMode(key_yb, INPUT_PULLUP); // INPUT_PULLUP
  boolean KeyRunModState = 0;    // 模式选择界面 1-使能
  if (digitalRead(key_yb) == 0)
  {
    KeyRunModState = 1;
    Serial.println("进入模式选择界面");
  }
  //什么时候可以进入模式选择界面 按键按下和新开机
  if      (KeyRunModState == 1)     displayRunModState = 1;
  else if (eepUserSet.runMode == 0) displayRunModState = 1;

  Serial.println(" ");
  Serial.print("displayRunModState:"); Serial.println(displayRunModState);
  Serial.print("eepUserSet.runMode:"); Serial.println(eepUserSet.runMode);
  Serial.println(" ");

  //****** 文件系统初始化 ******
  fileSystemConfig.setAutoFormat(false);
  fileSystem->setConfig(fileSystemConfig);
  fsOK = fileSystem->begin();
  if (fsOK) Serial.println("LittleFS 启动成功");
  else  {
    display_bitmap_sleep("LittleFS 启动失败");
    Serial.println("LittleFS 未能成功启动");
  }

  //******  判断文件系统是否有自定义图片 ******
  if (strlen(eepUserSet.customBmp) > 0) bmp_state = 1;
  else {
    bmp_state = 0;
    if (eepUserSet.runMode == 5) eepUserSet.runMode = 1;
  }


  if (eepUserSet.runMode == 1)
    Serial.print("天气模式");
  else if (eepUserSet.runMode == 2)
    Serial.print("阅读模式");
  else if (eepUserSet.runMode == 3)
    Serial.print("时钟模式");
  else if (eepUserSet.runMode == 4)
    Serial.print("配网模式");
  Serial.println("");
  overtime = millis();

  //查看堆碎片
  //Serial.print("堆碎片度量："); Serial.println(ESP.getHeapFragmentation()); Serial.println(" ");
  //Serial.print("可用堆大小："); Serial.println(ESP.getFreeHeap()); Serial.println(" ");
}

void loop()
{
  // 0-模式选择页面 1-天气 2-阅读 3-时钟 4-配网
  if (displayRunModState) //模式选择界面
  {
    display_runMod();
    key_runMod();
  }
  else if (eepUserSet.runMode == 1) //1-天气
  {
    connectToWifi();          // 连接wifi
    display_setup();          // 显示连接WIFI界面
    GetData();                // 获取数据界面
    display_main();           // 显示主界面
    RTC_tqmskjxs = 0;         // 写0表示下次不需要开机显示
    ESP.rtcUserMemoryWrite(RTCdz_tqmskjxs, &RTC_tqmskjxs, sizeof(RTC_tqmskjxs));//天气模式开机显示
    esp_sleep(3600000);       // 休眠1个钟
    //esp_sleep(6000);          // 休眠6秒
  }
  else if (eepUserSet.runMode == 2) //2-阅读
  {
    TXT_Init(); //初始化
    if (txtDisplayPage == txtMainDisplay) display_txtRead(); //TXT 阅读界面
    else if (txtDisplayPage == txtFileDisplay) //txt文件选择菜单的按键
    {
      display_txtFliaMain();  //TXT 文件选择菜单
      key_txtFileMain();
    }
  }
  else if (eepUserSet.runMode == 3) //3-时钟
  {
    read_RTC_time();
    display_clock();
  }
  else if (eepUserSet.runMode == 4) //4-配网
  {
    RTC_jsjs = eepUserSet.clockQSJG;
    ESP.rtcUserMemoryWrite(RTCdz_jsjs, &RTC_jsjs, sizeof(RTC_jsjs));
    BW_refresh();//黑白全屏刷新一次
    while (eepUserSet.runMode == 4) peiwang_mod(); //配网模式
  }
  else if (eepUserSet.runMode == 5) //5-显示自定义图片
  {
    if (eepUserSet.albumAuto) //开启轮播，随机播放,10分钟一次
    {
      //检查有多少个BMP文件
      uint8_t fileCount = 0;
      Dir dir = LittleFS.openDir("");   // 建立"目录"对象
      while (dir.next())// dir.next()用于检查目录中是否还有"下一个文件"
      {
        ESP.wdtFeed();// 喂狗
        String name = dir.fileName();
        if (name.endsWith(".bmp")) fileCount++;
      }
      //将BMP文件名字收集起来
      //Serial.print("fileCount："); Serial.println(fileCount);
      String albumAutoName[fileCount];
      fileCount = 0;
      dir = LittleFS.openDir("");   // 建立"目录"对象
      while (dir.next())// dir.next()用于检查目录中是否还有"下一个文件"
      {
        ESP.wdtFeed();// 喂狗
        String name = dir.fileName();
        if (name.endsWith(".bmp"))
        {
          albumAutoName[fileCount] = name;
          //Serial.print("albumAutoName "); Serial.print(fileCount); Serial.print(":"); Serial.println(albumAutoName[fileCount]);
          fileCount++;
        }
      }
      // 读取上一次的图片位置
      ESP.rtcUserMemoryRead(RTCdz_albumAutoOldWz, &RTC_albumAutoOldWz, sizeof(RTC_albumAutoOldWz));
      uint8_t random1 = random(0, fileCount);
      if (RTC_albumAutoOldWz >= fileCount) //大于统计值就随机取一个值
      {
        RTC_albumAutoOldWz = random(0, fileCount); //随机数0-fileCount减1之间
        random1 = RTC_albumAutoOldWz;
      }
      else //随机取值不能等于上一次
      {
        while (random1 == RTC_albumAutoOldWz) random1 = random(0, fileCount);
      }
      char* albumAutoName_c = "";//此处的警告不要修复，不然会无限重启
      strcpy(albumAutoName_c, albumAutoName[random1].c_str()); //将string转换成char*
      drawBitmapFileSystem(albumAutoName_c, 0, 0, 1);
      RTC_albumAutoOldWz = random1;
      ESP.rtcUserMemoryWrite(RTCdz_albumAutoOldWz, &RTC_albumAutoOldWz, sizeof(RTC_albumAutoOldWz));
      esp_sleep(600000); //600秒，十分钟 600000
    }
    else
    {
      drawBitmapFileSystem(eepUserSet.customBmp, 0, 0, 1);
      esp_sleep(0);
    }
  }
}

void key_runMod() //运行模式选择的按键决策
{
  uint32_t ca_time = 0;    // 长按的对比时间
  boolean ca_state = 0;    // 长按的状态
  uint16_t ca_max = 500;   // 长按的时间 ms
  pinMode(key_zj, INPUT); //INPUT_PULLUP;
  if (digitalRead(key_yb) == 0)
  {
    // 按键滤波
    uint8_t key_lb_count = 0; // 按键滤波计数
    boolean key_lb_state = 0; // 按键滤波状态
    while (digitalRead(key_yb == 0) && key_lb_state == 0) {
      key_lb_count++;
      if (key_lb_count >= key_lb_max) key_lb_state = 1;
    }
    //确认操作长按按键3
    ca_time = millis();
    if (key_lb_state)
    {
      while (digitalRead(key_yb) == 0 && ca_state == 0) {
        if (millis() - ca_time > ca_max) ca_state = 1; //长按一定时间判断
        delay(1);//ESP.wdtFeed(); // 喂狗
      }
      if (ca_state == 1) //确定是长按
      {
        if ( eepUserSet.runMode != 4) //不进入配网的判断
        {
          // 如果是要进入阅读模式就需要清除上一次打开的txt文件名
          if (eepUserSet.runMode == 2) strcpy(eepUserSet.txtNameLastTime, String("").c_str());
          EEPROM.put(eeprom_address0, eepUserSet);
          EEPROM.commit(); //保存模式
          if (ca_state)
          {
            displayRunModState = 0; //退出模式选择界面
            //刷白表示已执行确认操作，并进入while等等按键释放
            displayShuaBai(); //刷白确认
            return;
          }
          else ESP.reset(); //重启
        }
        else //进入配网的判断
        {
          displayRunModState = 0; //退出模式选择界面
          return;
        }
      }
    }
    displayUpdate = 1;  //允许更新屏幕
    eepUserSet.runMode++;  //切换下一个选项
  }
  else if (digitalRead(key_zj) == 0)
  {
    // 按键滤波
    uint8_t key_lb_count = 0; // 按键滤波计数
    boolean key_lb_state = 0; // 按键滤波状态
    while (digitalRead(key_zj == 0) && key_lb_state == 0) {
      key_lb_count++;
      if (key_lb_count >= key_lb_max) key_lb_state = 1;
    }
    while (digitalRead(key_zj) == 0)  delay(1); //ESP.wdtFeed(); // 喂狗
    displayUpdate = 1; //允许更新屏幕
    eepUserSet.runMode--; //切换上一个选项
  }
  //数值限制，有自定义图片则可以选择5
  uint8_t runModeMax = 0;
  if (bmp_state) runModeMax = 5;
  else runModeMax = 4;
  if (eepUserSet.runMode > runModeMax) eepUserSet.runMode = 1;
  else if (eepUserSet.runMode < 1) eepUserSet.runMode = runModeMax;
}

void key_txtFileMain() //txt文件选择菜单的按键
{
  uint32_t ca_time = 0;   //长按的对比时间
  boolean ca_state = 0;   //长按的状态
  uint16_t ca_max = 500;  //长按的时间 ms
  pinMode(key_zj, INPUT); //INPUT_PULLUP;
  if (digitalRead(key_yb) == 0)
  {
    // 按键滤波
    uint8_t key_lb_count = 0; // 按键滤波计数
    boolean key_lb_state = 0; // 按键滤波状态
    while (digitalRead(key_yb == 0) && key_lb_state == 0) {
      key_lb_count++;
      if (key_lb_count >= key_lb_max) key_lb_state = 1;
    }
    //长按按键3
    ca_time = millis();
    if (key_lb_state)
    {
      while (digitalRead(key_yb) == 0 && ca_state == 0) {
        if (millis() - ca_time > ca_max) ca_state = 1; //长按一定时间判断
        delay(1);//ESP.wdtFeed(); // 喂狗
      }
      if (ca_state == 1)
      {
        //打开文件，名称长度为0，大小为0不打开
        if (txtFileMainCount == 1 && txtName[0].length() > 0 && txtSize[0] > 0)
        {
          txtNum = 0;
          txtDisplayPage = txtMainDisplay; // 切换至主阅读界面
          displayShuaBai();//刷白确认键
          return;
        }
        else if (txtFileMainCount == 3 && txtName[1].length() > 0 && txtSize[1] > 0)
        {
          txtNum = 1;
          txtDisplayPage = txtMainDisplay; // 切换至主阅读界面
          displayShuaBai();//刷白确认键
          return;
        }
        else if (txtFileMainCount == 5 && txtName[2].length() > 0 && txtSize[2] > 0)
        {
          txtNum = 2;
          txtDisplayPage = txtMainDisplay; // 切换至主阅读界面
          displayShuaBai();//刷白确认键
          return;
        }
        //删除文件和对应的索引，该位置无文件则不执行
        else if (txtFileMainCount == 2 && txtName[0].length() > 0)
        {
          displayShuaBai();//刷白确认键
          indexesName = txtName[0] + ".i";
          LittleFS.remove(txtName[0]);  //删除文件
          LittleFS.remove(indexesName); //删除索引
          ESP.reset();
        }
        else if (txtFileMainCount == 4 && txtName[1].length() > 0)
        {
          displayShuaBai();//刷白确认键
          indexesName = txtName[1] + ".i";
          LittleFS.remove(txtName[1]);  //删除文件
          LittleFS.remove(indexesName); //删除索引
          ESP.reset();
        }
        else if (txtFileMainCount == 6 && txtName[2].length() > 0)
        {
          displayShuaBai();//刷白确认键
          indexesName = txtName[2] + ".i";
          LittleFS.remove(txtName[2]);  //删除文件
          LittleFS.remove(indexesName); //删除索引
          ESP.reset();
        }
      }
    }
    displayUpdate = 1; //允许更新屏幕
    txtFileMainCount++;
  }
  else if (digitalRead(key_zj) == 0)
  {
    // 按键滤波
    uint8_t key_lb_count = 0; // 按键滤波计数
    boolean key_lb_state = 0; // 按键滤波状态
    while (digitalRead(key_zj == 0) && key_lb_state == 0) {
      key_lb_count++;
      if (key_lb_count >= key_lb_max) key_lb_state = 1;
    }
    while (digitalRead(key_zj) == 0)  delay(1); //ESP.wdtFeed(); // 喂狗
    displayUpdate = 1; //允许更新屏幕
    txtFileMainCount--;    //切换上一个选项
  }
  //数值限制
  if (txtFileMainCount > 6) txtFileMainCount = 1;
  else if (txtFileMainCount < 1) txtFileMainCount = 6;
}

//txt模式下的按键
void key_txtMode() //上下页切换
{
  uint32_t ca_time = 0;   //长按的对比时间
  boolean ca_state = 0;   //长按的状态
  uint16_t ca_max = 500;  //长按的时间 ms
  pinMode(key_zj, INPUT); //INPUT_PULLUP;
  if (digitalRead(key_yb) == 0)
  {
    // 按键滤波
    uint8_t key_lb_count = 0; // 按键滤波计数
    boolean key_lb_state = 0; // 按键滤波状态
    while (digitalRead(key_yb == 0) && key_lb_state == 0) {
      key_lb_count++;
      if (key_lb_count >= key_lb_max) key_lb_state = 1;
    }
    //长按按键3
    ca_time = millis();
    if (key_lb_state)
    {
      while (digitalRead(key_yb) == 0 && ca_state == 0) {
        if (millis() - ca_time > ca_max) ca_state = 1; //长按一定时间判断
        delay(1);//ESP.wdtFeed(); // 喂狗
      }
      if (ca_state == 1)
      {
        displayUpdate = 1;  //允许更新屏幕
        txtDisplayPage = txtMenuMainDisplay; //切换至主菜单
        PageChange = 0;
        return;
      }
      else if (pageCurrent < txt_zys) PageChange = 1; //发送下一页指令
    }
  }
  else if (digitalRead(key_zj) == 0)
  {
    // 按键滤波
    uint8_t key_lb_count = 0; // 按键滤波计数
    boolean key_lb_state = 0; // 按键滤波状态
    while (digitalRead(key_zj == 0) && key_lb_state == 0) {
      key_lb_count++;
      if (key_lb_count >= key_lb_max) key_lb_state = 1;
    }
    while (digitalRead(key_zj) == 0)  delay(1); //ESP.wdtFeed(); // 喂狗
    PageChange = 2; //发送上一页指令
  }
}

void key_txtMenu() // txt阅读器菜单按键决策
{
  uint32_t ca_time = 0;   //长按的对比时间
  boolean ca_state = 0;   //长按的状态
  uint16_t ca_max = 500;  //长按的时间 ms
  pinMode(key_zj, INPUT); //INPUT_PULLUP;
  if (digitalRead(key_yb) == 0)
  {
    // 按键滤波
    uint8_t key_lb_count = 0; // 按键滤波计数
    boolean key_lb_state = 0; // 按键滤波状态
    while (digitalRead(key_yb == 0) && key_lb_state == 0) {
      key_lb_count++;
      if (key_lb_count >= key_lb_max) key_lb_state = 1;
    }
    //长按按键3
    ca_time = millis();
    if (key_lb_state)
    {
      while (digitalRead(key_yb) == 0 && ca_state == 0) {
        if (millis() - ca_time > ca_max) ca_state = 1; //长按一定时间判断
        delay(1);//ESP.wdtFeed(); // 喂狗
      }
      if (ca_state == 1)
      {
        if (txtMenuCount == 1) // 继续-退出菜单
        {
          displayShuaBai();     // 刷白
          txtDisplayPage = txtMainDisplay; // 切换至主阅读界面
          pageCurrent += 1;     // 页数先加1，利用上一页来实现下一页到原来的页
          PageChange = 2;       // 发送上一页指令
          pageUpdataCount = 6;  // 清屏
          return;
        }
        else if (txtMenuCount == 2) display_bitmap_sleep("* 主动休眠 *");
        else if (txtMenuCount == 3) // 跳转指定页数
        {
          displayUpdate = 1;   // 允许更新屏幕
          txtDisplayPage = txtMenukeyBoardDisplay; // 切换至键盘显示界面
          keyBoardNum = pageCurrent;  //将当前页赋值到键盘的数
          keyBoardCount = 10;         //默认将光标设置在退格位置
          return;
        }
        else if (txtMenuCount == 4) //切换快速翻页
        {
          eepUserSet.fastFlip = !eepUserSet.fastFlip;
          EEPROM.put(eeprom_address0, eepUserSet);
          EEPROM.commit(); //保存
          displayUpdate = 1;
          return;
        }
        else if (txtMenuCount == 5) //返回至文件选择界面，直接清除上一次打开的txt文件名再重启即可
        {
          displayShuaBai(); //刷白
          strcpy(eepUserSet.txtNameLastTime, String("").c_str()); // 清除上一次打开的txt文件名
          EEPROM.put(eeprom_address0, eepUserSet);
          EEPROM.commit(); //保存
          //ESP.reset();
          txtDisplayPage = txtFileDisplay; // 切换文件选择界面
          pageUpdataCount = 6;  // 清屏
        }
        else if (txtMenuCount == 6) //校准时钟
        {
          displayUpdate = 1;
          txtDisplayPage = txtMenuClockDisplay; // 切换至时钟校准界面
          return;
        }
      }
    }
    txtMenuCount++;    // 切换下一个选项
    displayUpdate = 1; // 允许更新屏幕
  }
  else if (digitalRead(key_zj) == 0)
  {
    // 按键滤波
    uint8_t key_lb_count = 0; // 按键滤波计数
    boolean key_lb_state = 0; // 按键滤波状态
    while (digitalRead(key_zj == 0) && key_lb_state == 0) {
      key_lb_count++;
      if (key_lb_count >= key_lb_max) key_lb_state = 1;
    }
    while (digitalRead(key_zj) == 0)  delay(1); //ESP.wdtFeed(); // 喂狗
    txtMenuCount--;    // 切换上一个选项
    displayUpdate = 1; // 允许更新屏幕
  }
  //数值限制
  if (txtMenuCount > 6) txtMenuCount = 1;
  else if (txtMenuCount < 1) txtMenuCount = 6;
}

void key_txtKeyBoard() //阅读器数字键盘决策
{
  uint32_t ca_time = 0;   //长按的对比时间
  boolean ca_state = 0;   //长按的状态
  uint16_t ca_max = 500;  //长按的时间 ms
  pinMode(key_zj, INPUT); //INPUT_PULLUP;
  if (digitalRead(key_yb) == 0)
  {
    // 按键滤波
    uint8_t key_lb_count = 0; // 按键滤波计数
    boolean key_lb_state = 0; // 按键滤波状态
    while (digitalRead(key_yb == 0) && key_lb_state == 0) {
      key_lb_count++;
      if (key_lb_count >= key_lb_max) key_lb_state = 1;
    }
    //长按按键3
    ca_time = millis();
    if (key_lb_state)
    {
      while (digitalRead(key_yb) == 0 && ca_state == 0) {
        if (millis() - ca_time > ca_max) ca_state = 1; //长按一定时间判断
        delay(1);//ESP.wdtFeed(); // 喂狗
      }
      if (ca_state == 1)
      {
        if (keyBoardCount == 0)       keyBoardNum = keyBoardNum * 10;
        else if (keyBoardCount == 1)  keyBoardNum = keyBoardNum * 10 + 1;
        else if (keyBoardCount == 2)  keyBoardNum = keyBoardNum * 10 + 2;
        else if (keyBoardCount == 3)  keyBoardNum = keyBoardNum * 10 + 3;
        else if (keyBoardCount == 4)  keyBoardNum = keyBoardNum * 10 + 4;
        else if (keyBoardCount == 5)  keyBoardNum = keyBoardNum * 10 + 5;
        else if (keyBoardCount == 6)  keyBoardNum = keyBoardNum * 10 + 6;
        else if (keyBoardCount == 7)  keyBoardNum = keyBoardNum * 10 + 7;
        else if (keyBoardCount == 8)  keyBoardNum = keyBoardNum * 10 + 8;
        else if (keyBoardCount == 9)  keyBoardNum = keyBoardNum * 10 + 9;
        else if (keyBoardCount == 10) keyBoardNum = keyBoardNum / 10;
        else if (keyBoardCount == 11)     //确认跳转
        {
          displayShuaBai(); //刷白
          if (keyBoardNum == 0) return;
          if (keyBoardNum > txt_zys) keyBoardNum = txt_zys;// 数值限制
          //keyBoardState = 0;             // 退出键盘状态
          txtDisplayPage = txtMainDisplay; //切换至主阅读界面
          pageCurrent = keyBoardNum + 1;   // 页数先加1，利用上一页来实现
          PageChange = 2;                  // 发送上一页指令
          pageUpdataCount = 6;  // 清屏
        }
        else if (keyBoardCount == 12) //退出
        {
          displayShuaBai(); //刷白
          txtDisplayPage = txtMainDisplay; //切换至主阅读界面
          pageCurrent += 1;   // 页数先加1，利用上一页来实现
          PageChange = 2;     // 发送上一页指令
          pageUpdataCount = 6;  // 清屏
          return;
        }
        if (keyBoardNum > txt_zys) keyBoardNum = txt_zys;   // 数值限制
        displayUpdate = 1;                                  // 允许更新屏幕
        return;
      }
    }
    keyBoardCount++;    // 切换下一个选项
    displayUpdate = 1;  // 允许更新屏幕
  }
  else if (digitalRead(key_zj) == 0)
  {
    // 按键滤波
    uint8_t key_lb_count = 0; // 按键滤波计数
    boolean key_lb_state = 0; // 按键滤波状态
    while (digitalRead(key_zj == 0) && key_lb_state == 0) {
      key_lb_count++;
      if (key_lb_count >= key_lb_max) key_lb_state = 1;
    }
    while (digitalRead(key_zj) == 0)  delay(1); //ESP.wdtFeed(); // 喂狗
    keyBoardCount--;       // 切换上一个选项
    displayUpdate = 1;     // 允许更新屏幕
  }
  //数值限制
  if (keyBoardCount > 12) keyBoardCount = 0;
  else if (keyBoardCount < 0) keyBoardCount = 12;
}

void IRAM_ATTR ClockSkip()
{
  //Serial.println("时钟模式：跳过校准");
  ClockSkipState = 1;
  detachInterrupt(digitalPinToInterrupt(key_yb)); //取消外部按键中断
}
void IRAM_ATTR tickerTxtClock() //定时器任务-阅读模式时钟自加
{
  RTC_seconds++; //秒加
  if (RTC_seconds >= 60)
  {
    RTC_seconds = 0;
    RTC_minute++;
    if (RTC_minute >= 60)
    {
      RTC_minute = 0;
      RTC_hour++;
      if (RTC_hour >= 24)
      {
        RTC_hour = 0;
        riqi_js(); //日期自加计算
      }
    }
  }
}

//****** EDIT 文件管理器 ******
void replyOK() {
  server.send(200, FPSTR(TEXT_PLAIN), "");
}
void replyOKWithMsg(String msg) {
  server.send(200, FPSTR(TEXT_PLAIN), msg);
}
void replyNotFound(String msg) {
  server.send(404, FPSTR(TEXT_PLAIN), msg);
}
void replyBadRequest(String msg) {
  DBG_OUTPUT_PORT.println(msg);
  server.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}
void replyServerError(String msg) {
  DBG_OUTPUT_PORT.println(msg);
  server.send(500, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

////////////////////////////////
// 请求处理程序

/*
   返回FS类型、状态和大小信息
*/
void handleStatus()
{
  DBG_OUTPUT_PORT.println("handleStatus");
  FSInfo fs_info;
  String json;
  json.reserve(128);

  json = "{\"type\":\"";
  json += fsName;
  json += "\", \"isOk\":";
  if (fsOK)
  {
    fileSystem->info(fs_info);
    json += F("\"true\", \"totalBytes\":\"");
    json += fs_info.totalBytes;
    json += F("\", \"usedBytes\":\"");
    json += fs_info.usedBytes;
    json += "\"";
  }
  else
  {
    json += "\"false\"";
  }
  json += F(",\"unsupportedFiles\":\"");
  json += unsupportedFiles;
  json += "\"}";

  server.send(200, "application/json", json);
}


/*
  返回“dir”查询字符串参数指定的目录中的文件列表。
  还演示了分块响应的使用。
*/
void handleFileList()
{
  if (!fsOK)  //文件系统错误
  {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  if (!server.hasArg("dir"))
  {
    return replyBadRequest(F("DIR ARG MISSING "));//DIR ARG丢失
  }

  String path = server.arg("dir");
  if (path != "/" && !fileSystem->exists(path))
  {
    return replyBadRequest("BAD PATH");
  }

  DBG_OUTPUT_PORT.println(String("handleFileList: ") + path);
  Dir dir = fileSystem->openDir(path);
  path.clear();

  // 使用HTTP/1.1分块响应以避免生成巨大的临时字符串
  if (!server.chunkedResponseModeStart(200, "text/json"))
  {
    server.send(505, F("text/html"), F("需要依赖 HTTP1.1"));
    return;
  }

  // 每行使用相同的字符串
  String output;
  output.reserve(64);
  while (dir.next())
  {
    if (output.length())
    {
      // 从上一次迭代发送字符串
      // 作为HTTP块
      server.sendContent(output); //发送内容
      output = ',';
    }
    else
    {
      output = '[';
    }

    output += "{\"type\":\"";
    if (dir.isDirectory())
    {
      output += "dir";
    }
    else
    {
      output += F("file\",\"size\":\"");
      output += dir.fileSize();
    }

    output += F("\",\"name\":\"");
    // 始终返回不带前导的名称 "/"
    if (dir.fileName()[0] == '/')
    {
      output += &(dir.fileName()[1]);
    }
    else
    {
      output += dir.fileName();
    }

    output += "\"}";
  }

  // 发送最后一个字符串
  output += "]";
  server.sendContent(output);          // 发送内容
  server.chunkedResponseFinalize();    // 分块响应完成
}


/*
   从文件系统读取给定的文件，并将其流回到客户端
*/
bool handleFileRead(String path)
{
  //DBG_OUTPUT_PORT.println(String("handleFileRead: ") + path);
  if (!fsOK) {
    replyServerError(FPSTR(FS_INIT_ERROR));
    return true;
  }

  if (path.endsWith("/")) {
    path += "index.htm";
  }

  String contentType;
  if (server.hasArg("download")) {
    contentType = F("application/octet-stream");
  } else {
    contentType = mime::getContentType(path);
  }

  if (!fileSystem->exists(path)) {
    // 找不到文件，请尝试gzip版本
    path = path + ".gz";
  }
  if (fileSystem->exists(path)) {
    File file = fileSystem->open(path, "r");
    if (server.streamFile(file, contentType) != file.size()) {
      //DBG_OUTPUT_PORT.println("Sent less data than expected!");
    }
    file.close();
    return true;
  }

  return false;
}


/*
  由于某些FS（例如LittleFS）在删除最后一个子文件夹时会删除父文件夹，
  返回仍然存在的最近父级的路径
*/
String lastExistingParent(String path) {
  while (!path.isEmpty() && !fileSystem->exists(path)) {
    if (path.lastIndexOf('/') > 0) {
      path = path.substring(0, path.lastIndexOf('/'));
    } else {
      path = String();  // 无斜杠=>顶部文件夹不存在
    }
  }
  DBG_OUTPUT_PORT.println(String("Last existing parent: ") + path);
  return path;
}

/*
   处理新文件的创建/重命名
   Operation      | req.responseText
   ---------------+--------------------------------------------------------------
   Create file    | parent of created file
   Create folder  | parent of created folder
   Rename file    | parent of source file
   Move file      | parent of source file, or remaining ancestor
   Rename folder  | parent of source folder
   Move folder    | parent of source folder, or remaining ancestor
*/
void handleFileCreate() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  String path = server.arg("path");
  if (path.isEmpty()) {
    return replyBadRequest(F("PATH ARG MISSING"));
  }

  if (path == "/") {
    return replyBadRequest("BAD PATH");
  }
  if (fileSystem->exists(path)) {
    return replyBadRequest(F("PATH FILE EXISTS"));
  }

  String src = server.arg("src");
  if (src.isEmpty()) {
    // 未指定源：创建
    DBG_OUTPUT_PORT.println(String("handleFileCreate: ") + path);
    if (path.endsWith("/")) {
      // 创建文件夹
      path.remove(path.length() - 1);
      if (!fileSystem->mkdir(path)) {
        return replyServerError(F("MKDIR FAILED"));
      }
    } else {
      // 创建一个文件
      File file = fileSystem->open(path, "w");
      if (file) {
        file.write((const char *)0);
        file.close();
      } else {
        return replyServerError(F("CREATE FAILED"));
      }
    }
    if (path.lastIndexOf('/') > -1) {
      path = path.substring(0, path.lastIndexOf('/'));
    }
    replyOKWithMsg(path);
  } else {
    // 指定的源：重命名
    if (src == "/") {
      return replyBadRequest("BAD SRC");
    }
    if (!fileSystem->exists(src)) {
      return replyBadRequest(F("SRC FILE NOT FOUND"));
    }

    DBG_OUTPUT_PORT.println(String("handleFileCreate: ") + path + " from " + src);

    if (path.endsWith("/")) {
      path.remove(path.length() - 1);
    }
    if (src.endsWith("/")) {
      src.remove(src.length() - 1);
    }
    if (!fileSystem->rename(src, path)) {
      return replyServerError(F("RENAME FAILED"));
    }
    replyOKWithMsg(lastExistingParent(src));
  }
}


/*
  删除按给定路径设计的文件或文件夹。
  如果是文件，请将其删除。
  如果是文件夹，请先删除所有嵌套内容，然后再删除文件夹本身
  重要提示：通常不建议在嵌入式设备上使用递归，否则会导致崩溃（堆栈溢出错误）。
  这种用法只是为了演示，如果文件系统嵌套较深，FSBrowser可能会崩溃。
  请不要在生产系统上执行此操作。
*/
void deleteRecursive(String path) {
  File file = fileSystem->open(path, "r");
  bool isDir = file.isDirectory();
  file.close();

  // 如果是普通文件，请将其删除
  if (!isDir) {
    fileSystem->remove(path);
    return;
  }

  // 否则，请先删除其内容
  Dir dir = fileSystem->openDir(path);

  while (dir.next()) {
    deleteRecursive(path + '/' + dir.fileName());
  }

  // 然后删除文件夹本身
  fileSystem->rmdir(path);
}


/*
   处理文件删除请求
   Operation      | req.responseText
   ---------------+--------------------------------------------------------------
   Delete file    | parent of deleted file, or remaining ancestor
   Delete folder  | parent of deleted folder, or remaining ancestor
*/
void handleFileDelete() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }

  String path = server.arg(0);
  if (path.isEmpty() || path == "/") {
    return replyBadRequest("BAD PATH");
  }

  DBG_OUTPUT_PORT.println(String("handleFileDelete: ") + path);
  if (!fileSystem->exists(path)) {
    return replyNotFound(FPSTR(FILE_NOT_FOUND));
  }
  deleteRecursive(path);

  replyOKWithMsg(lastExistingParent(path));
}

/*
   处理文件上载请求
*/
void handleFileUpload() {
  if (!fsOK) {
    return replyServerError(FPSTR(FS_INIT_ERROR));
  }
  if (server.uri() != "/edit") {
    return;
  }

  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) //上传_文件_开始
  {
    String filename = upload.filename;
    // 确保路径始终以“/”开头
    if (!filename.startsWith("/"))
    {
      filename = "/" + filename;
    }
    DBG_OUTPUT_PORT.println(String("handleFileUpload Name: ") + filename);
    fsUploadFile = fileSystem->open(filename, "w");
    if (!fsUploadFile)
    {
      return replyServerError(F("创建失败"));
    }
    DBG_OUTPUT_PORT.println(String("Upload: START, filename: ") + filename);
  }
  else if (upload.status == UPLOAD_FILE_WRITE) //上传_文件_写入
  {
    if (fsUploadFile)
    {
      size_t bytesWritten = fsUploadFile.write(upload.buf, upload.currentSize); //写入当前大小
      if (bytesWritten != upload.currentSize)
      {
        return replyServerError(F("写入失败"));
      }
    }
    //DBG_OUTPUT_PORT.println(String("Upload: WRITE, Bytes: ") + upload.currentSize);
  }
  else if (upload.status == UPLOAD_FILE_END) //上传_文件_结束
  {
    if (fsUploadFile)
    {
      fsUploadFile.close();
    }
    DBG_OUTPUT_PORT.println(String("Upload: END, Size: ") + upload.totalSize);
  }
}

/*
   特定处理程序返回索引。/edit文件夹中的htm（或gzip版本）。
  如果文件不存在，但设置了“包含\回退\索引\ HTM”标志，则回退到版本
  嵌入在程序代码中。
  否则，将使用包含调试信息的404页面失败
*/
void handleGetEdit()
{
  overtime = millis(); //重置配网超时时间
  if (handleFileRead(F("/edit/index.htm"))) return;
  replyNotFound(FPSTR(FILE_NOT_FOUND));
}

//****** EDIT 文件管理器 ******
/*
  Serial.println("按下右边");
  Serial.print("右边按键状态："); Serial.println(digitalRead(key_yb));
  Serial.print("中间按键状态："); Serial.println(digitalRead(key_yb));
  Serial.println(" ");

  ESP.rtcUserMemoryRead(0, &data, sizeof(data));//向地址0写入data
  Serial.println(" ");
  Serial.print("读取 TRC地址0数据："); Serial.println(data); Serial.println(" ");
  data = 1;
  ESP.rtcUserMemoryWrite(0, &data, sizeof(data));//向地址0写入data
  Serial.print("data写入："); Serial.println(data); Serial.println(" ");
  ESP.rtcUserMemoryRead(0, &data, sizeof(data));//读取地址0赋值到data
  Serial.print("读取 RTC地址0数据："); Serial.println(data); Serial.println(" ");
  Serial.print("堆碎片度量："); Serial.println(ESP.getHeapFragmentation()); Serial.println(" ");
    //ESP.wdtFeed();//喂狗
*/
