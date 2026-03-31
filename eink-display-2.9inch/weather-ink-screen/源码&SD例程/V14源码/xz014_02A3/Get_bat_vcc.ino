//采集电池电压
//采用时间平均滤波
uint8_t bat_vcc_count = 0;      //采样计数
#define bat_vcc_cycs 3          //采样次数
#define bat_vcc_cysj 3000       //采样时间间隔，ms
float bat_adc_new = 0.0;
uint32_t bat_vcc_time_old = 0.0;

void get_bat_vcc() //时间平均滤波的电压
{
  if (millis() - bat_vcc_time_old >= bat_vcc_cysj)
  {
    pinMode(bat_switch_pin, OUTPUT);
    digitalWrite(bat_switch_pin, 1);
    bat_vcc_count++;
    bat_adc_new += analogRead(bat_vcc_pin);
    if (bat_vcc_count >= bat_vcc_cycs)
    {
      bat_vcc = (bat_adc_new / bat_vcc_cycs) * 0.0009765625 * 5.607; //电池电压系数
      bat_adc_new = 0.0;
      bat_vcc_count = 0;
    }
    digitalWrite(bat_switch_pin, 0); //关闭电池测量
    //pinMode(bat_switch_pin, INPUT);  //改为输入状态避免漏电
    bat_vcc_time_old = millis();
  }
}
float getBatVolNew() //即时的电压
{
  pinMode(bat_switch_pin, OUTPUT);
  digitalWrite(bat_switch_pin, 1);
  delay(1);
  float vcc_cache = 0.0;
  for (uint8_t i = 0; i < 30; i++)
  {
    //delay(1);
    vcc_cache += analogRead(bat_vcc_pin) * 0.0009765625 * 5.607;
  }
  digitalWrite(bat_switch_pin, 0); //关闭电池测量
  pinMode(bat_switch_pin, INPUT);  //改为输入状态避免漏电
  return (vcc_cache / 30);
}

double getBatVolBfb(double batVcc) //获取电压的百分比，经过换算并非线性关系
{
  double bfb = 0.0;
  //y = 497.50976 x4 - 7,442.07254 x3 + 41,515.70648 x2 - 102,249.34377 x + 93,770.99821
  bfb = 497.50976 * batVcc * batVcc * batVcc * batVcc
        - 7442.07254 * batVcc * batVcc * batVcc
        + 41515.70648 * batVcc * batVcc
        - 102249.34377 * batVcc
        + 93770.99821;
  if (bfb > 100) bfb = 100.0;
  else if (bfb < 0) bfb = 3.0;
  return bfb;
}
void get_dht30_data()
{
  Serial.print("电池电压："); Serial.println(getBatVolNew());
  Serial.println("********** DHT30 **********");
  pinMode(bat_switch_pin, OUTPUT);
  digitalWrite(bat_switch_pin, 1);
  delay(1);
  Wire.begin(13, 14);

  sht3xd.begin(0x44); // I2C address: 0x44 or 0x45

  Serial.print("序列号 #");  Serial.println(sht3xd.readSerialNumber());

  /*if (sht3xd.periodicStart(SHT3XD_REPEATABILITY_HIGH, SHT3XD_FREQUENCY_10HZ) != SHT3XD_NO_ERROR) {
    Serial.println("[发生错误] 无法启动周期模式");
    }
    printResult("DHT30-周期模式", sht3xd.periodicFetchData());*/

  printResult("DHT30-ClockStrech模式", sht3xd.readTempAndHumidity(SHT3XD_REPEATABILITY_LOW, SHT3XD_MODE_CLOCK_STRETCH, 50));
  //printResult("Pooling Mode", sht3xd.readTempAndHumidity(SHT3XD_REPEATABILITY_HIGH, SHT3XD_MODE_POLLING, 50));
  digitalWrite(bat_switch_pin, 0); //关闭
  pinMode(bat_switch_pin, INPUT);  //改为输入状态避免漏电
  Serial.println("**************************");
  Serial.println(" ");
}

void printResult(String text, SHT31D result) //
{
  if (result.error == SHT3XD_NO_ERROR) {
    Serial.print(text);
    Serial.print(": 温度=");
    Serial.print(result.t);
    Serial.print("C, 湿度=");
    Serial.print(result.rh);
    Serial.println("%");
    dht30_temp = result.t;
    dht30_humi = result.rh;
  }
  else {
    Serial.print(text);
    Serial.print(": [发生错误] 代码为 #");
    Serial.println(result.error);
    dht30_error = result.error;
  }
}
