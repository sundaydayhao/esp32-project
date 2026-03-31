void display_runMod()
{
  //超时检测
  if (millis() - overtime > 300000) display_bitmap_sleep("* 模式选择超时 *");

  uint16_t x1 = 60, x2 = 217;            // 字
  uint16_t x3 = x1 - 52, x4 = x2 - 54;   // 图
  uint16_t y1 = 30, y2 = 110, y5 = 70;   // 字
  uint16_t y3 = y1 - 28, y4 = y2 - 32;   // 图

  if (displayUpdate == 0) return;
  displayUpdate = 0;

  FixedRefresh(); //屏幕初始化及定次刷新

  u8g2Fonts.setFont(chinese_gb2312);
  display.setPartialWindow(0, 0, display.width(), display.height()); //设置局部刷新窗口
  display.firstPage();
  do
  {
    display.drawInvertedBitmap(x3, y3, Bitmap_tianqiMod, 45, 45, heise);
    u8g2Fonts.setCursor(x1, y1);
    u8g2Fonts.print("天气模式");

    display.drawInvertedBitmap(x4, y3, Bitmap_yueduMod, 45, 45, heise);
    u8g2Fonts.setCursor(x2, y1);
    u8g2Fonts.print("阅读模式");

    display.drawInvertedBitmap(x3, y4, Bitmap_shizhongMod, 45, 45, heise);
    u8g2Fonts.setCursor(x1, y2);
    u8g2Fonts.print("时钟模式");

    display.drawInvertedBitmap(x4, y4, Bitmap_peiwangMod, 45, 45, heise);
    u8g2Fonts.setCursor(x2, y2);
    u8g2Fonts.print("配网模式");

    if (bmp_state) //显示自定义图片选项
    {
      display.drawInvertedBitmap(x1 - 18, y5 - 14, Bitmap_zdy, 16, 16, heise);
      u8g2Fonts.setCursor(x1, y5);
      u8g2Fonts.print("自定义图片");
    }

    u8g2Fonts.setFont(u8g2_font_baby_tr);
    u8g2Fonts.setCursor(x2 - 38, y2 - 7);
    u8g2Fonts.print(version);
    u8g2Fonts.setCursor(x2 - 38, y2 + 4);
    u8g2Fonts.print(version1);
    //0-模式选择页面 1-天气 2-阅读 3-时钟 4-配网
    if (eepUserSet.runMode == 1)
    {
      display.drawLine(x1 - 2, y1 + 2, x1 + 60, y1 + 2, 0); //画线
      display.drawCircle(x1 + 62, y1 - 2, 4, 0);            //画圈
    }
    else if (eepUserSet.runMode == 2)
    {
      display.drawLine(x2 - 2, y1 + 2, x2 + 60, y1 + 2, 0);
      display.drawCircle(x2 + 62, y1 - 2, 4, 0);
    }
    else if (eepUserSet.runMode == 3)
    {
      display.drawLine(x1 - 2, y2 + 2, x1 + 60, y2 + 2, 0);
      display.drawCircle(x1 + 62, y2 - 2, 4, 0);
    }
    else if (eepUserSet.runMode == 4)
    {
      display.drawLine(x2 - 2, y2 + 2, x2 + 60, y2 + 2, 0);
      display.drawCircle(x2 + 62, y2 - 2, 4, 0);
    }
    else if (eepUserSet.runMode == 5)
    {
      display.drawLine(x1 - 1, y5 + 2, x1 + 74, y5 + 2, 0);
      display.drawCircle(x1 + 76, y5 - 2, 4, 0);
    }
  }
  while (display.nextPage());
  //display.hibernate();  //display.powerOff(); //仅关闭电源
  //Serial.print("displayRunModState:"); Serial.println(displayRunModState);
  //Serial.print("eepUserSet.runMode:"); Serial.println(eepUserSet.runMode);
}
