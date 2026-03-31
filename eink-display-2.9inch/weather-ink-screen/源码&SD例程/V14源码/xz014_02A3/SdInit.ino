void sdcs_init() //SD卡cs引脚初始化
{
  if (eepUserSet.sdState && sdInitOk)
  {
    sdBeginCheck();
  }
}

//RTC_SDInitError用于检查挂载失败
//SD卡挂载失败会导致软看门狗重启
boolean sdBeginCheck() //SD挂载检查
{
  ESP.wdtDisable();  //停用看门狗
  SDFS.end();
  if (SD.begin(SD_CS, SPI_SPEED))
  {
    Serial.println("SD卡挂载成功");
    ESP.wdtEnable(8000); //启用看门狗
    sdInitOk = 1;
    return 1;
  }
  else
  {
    Serial.println("无法挂载SD卡");
    ESP.wdtEnable(8000); //启用看门狗
    sdInitOk = 0;
    return 0;
  }
  return 0;
}
