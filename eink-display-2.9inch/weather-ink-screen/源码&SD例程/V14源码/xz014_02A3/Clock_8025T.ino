boolean ClockReadCheck() //读取时钟数据是否正常
{
  Wire.begin(13, 14);
  tmElements_t tm2;
  tm2 = rtc.read(eepUserSet.type_8025T);    // 读取时钟芯片数据
  display.init(0, 0, 10, 1);
  //char s[20];
  //sprintf(s, "%d/%d/%d %d:%d:%d", tmYearToCalendar(tm2.Year), tm2.Month, tm2.Day, tm2.Hour, tm2.Minute, tm2.Second);
  //Serial.println(s);
  if (tmYearToCalendar(tm2.Year) < 2020 || tmYearToCalendar(tm2.Year) > 2099) return 0;
  else if (tm2.Month <= 0 || tm2.Month > 12) return 0;
  else if (tm2.Day <= 0 || tm2.Day > 31) return 0;
  else if (tm2.Hour < 0 || tm2.Hour > 24) return 0;
  else if (tm2.Minute < 0 || tm2.Minute > 60) return 0;
  else if (tm2.Second < 0 || tm2.Second > 60) return 0;
  else return 1;

  return 0;
}

//  boolean type_8025T = 0; 8025t 的类型 0-BL(按顺序读取) 1-RX(读取的数据会偏移8位)
boolean ClockChipCheck() //往指定地址写入一个数据，再判断是否能读出来以确定时钟芯片正常不正常
{
 tmElements_t tm1;
  tmElements_t tm2;
  tmElements_t tm3;
  tm1.Year = 33;
  tm1.Month = 3;
  tm1.Day = 3;
  tm1.Hour = 3;
  tm1.Minute = 3;
  tm1.Second = 3;
  Wire.begin(13, 14);
  rtc.write(tm1);      // 写入时钟芯片数据
  delay(1);
  tm2 = rtc.read(0);    // 读取时钟芯片数据
  tm3 = rtc.read(1);    // 读取时钟芯片数据
  //char s[20];
  //sprintf(s, "%d/%d/%d %d:%d:%d", tm2.Year, tm2.Month, tm2.Day, tm2.Hour, tm2.Minute, tm2.Second);
  /*if (tm1.Year != tm2.Year)return 0;
    else if (tm1.Month != tm2.Month)return 0;
    else if (tm1.Day != tm2.Day)return 0;
    else if (tm1.Hour != tm2.Hour)return 0;
    else if (tm1.Minute != tm2.Minute)return 0;
    else if (tm1.Second != tm2.Second)return 0;*/
  if ((tm1.Year == tm2.Year) && (tm1.Month == tm2.Month) && (tm1.Hour == tm2.Hour) && (tm1.Minute == tm2.Minute) && (tm1.Second == tm2.Second))
  {
    eepUserSet.type_8025T = 0;
    return 1;
  }
  else if ((tm1.Year == tm3.Year) && (tm1.Month == tm3.Month) && (tm1.Hour == tm3.Hour) && (tm1.Minute == tm3.Minute) && (tm1.Second == tm3.Second))
  {
    eepUserSet.type_8025T = 1;
    return 1;
  }
  else return 0;

  return 0;
}

void getClockChipToRTC() //获取时钟芯片数据并赋值至RTC数据区
{
  tmElements_t tm2;
  Wire.begin(13, 14); // 初始化I2C
  tm2 = rtc.read(eepUserSet.type_8025T);   // 读取时钟芯片数据
  RTC_hour = tm2.Hour;
  RTC_minute = tm2.Minute;
  RTC_seconds = tm2.Second;
  ESP.rtcUserMemoryWrite(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
  ESP.rtcUserMemoryWrite(RTCdz_minute, &RTC_minute, sizeof(RTC_minute));
  ESP.rtcUserMemoryWrite(RTCdz_seconds, &RTC_seconds, sizeof(RTC_seconds));

  char s[20];
  sprintf(s, "%d/%d/%d %d:%d:%d", tmYearToCalendar(tm2.Year), tm2.Month, tm2.Day, tm2.Hour, tm2.Minute, tm2.Second);
  Serial.println(s);
  //在0点0分并且校准失败时，计算今天是星期几
  if (tm2.Hour == 0 && tm2.Minute == 0 && (RTC_ntpTimeError == 2 || RTC_ntpTimeError == 3))
  {
    String xqj = week_calculate(tmYearToCalendar(tm2.Year), tm2.Month, tm2.Day);

    strcpy(eepUserClock.week, xqj.c_str());
    EEPROM.put(eeprom_address1, eepUserClock);
    EEPROM.commit(); //保存
    //Serial.print("星期计算："); Serial.println(eepUserClock.week);
  }
  strcpy(eepUserClock.year, String(tmYearToCalendar(tm2.Year)).c_str());  // 年
  strcpy(eepUserClock.month, String(tm2.Month).c_str());                  // 月
  strcpy(eepUserClock.day, String(tm2.Day).c_str());                      // 日
}

void ManualCompensation8025T() //手动补偿算法
{
  WifiShutdown(); //正真的关闭WIFI
  if (eepUserSet.clockCompensate < 1000) //小于1000ms，进行累计
    eepUserSet.clockCompensate += eepUserSet.clockCompensate;
  if (eepUserSet.clockCompensate >= 1000) //当补偿值累计大于等于1000MS，就修改时钟芯片的秒寄存器
  {
    int16_t jzz = eepUserSet.clockCompensate / 1000; //计算校准值   如：1200/1000 =1
    int16_t tg_miao = jzz * 3; //跳过的秒
    eepUserSet.clockCompensate = eepUserSet.clockCompensate - (jzz * 1000);  //计算剩余值留到下次用 如：1200- （1*1000） = 200
    int8_t bch_miao;
    //修正时钟数据
    tmElements_t tm2;
    Wire.begin(13, 14); // 初始化I2C
    tm2 = rtc.read(eepUserSet.type_8025T);   // 读取时钟芯片数据
    while (tm2.Second > 60 - tg_miao || tm2.Second < tg_miao) //跳过一些可能会误操作的秒数
    {
      delay(tg_miao); //等待到可以操作秒
      tm2 = rtc.read(eepUserSet.type_8025T); // 读取时钟芯片数据
    }
    bch_miao = tm2.Second - jzz;             // 计算补偿后的秒值
    tm2 = rtc.read(eepUserSet.type_8025T);   // 重新读取时钟芯片数据，因为期间分钟和小时可能更新了
    tm2.Second = bch_miao;                   // 修改补偿后的秒
    rtc.write(tm2);                          // 写入时钟芯片数据
  }
  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commit(); //保存
}
