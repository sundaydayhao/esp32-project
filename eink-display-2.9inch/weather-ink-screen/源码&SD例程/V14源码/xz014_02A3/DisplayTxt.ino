boolean indexesFailState = 0; // 索引校验失败状态 0-正常 1-失败
void TXT_Init() //txt初始化,检查上一次打开的txt文件
{
  if (txtInitState)
  {
    txtInitState = 0;
    uint8_t txt_i = 0;
    // 显示目录中文件名以及文件大小
    Dir dir = LittleFS.openDir("");  // 建立“目录”对象
    while (dir.next())                // dir.next()用于检查目录中是否还有“下一个文件”
    {
      String fileName = dir.fileName(); //文件名
      size_t fileSize = dir.fileSize(); //文件大小
      //Serial.print("文件:"); Serial.print(fileName); Serial.print("，大小:"); Serial.println(fileSize);
      if (fileName.endsWith(".txt")) // 检测TXT文件，将名字赋值给txtName
      {
        //最多支持3个文件，头3个
        if (txt_i < 3)
        {
          txtName[txt_i] = fileName;  // 获取名字
          txtSize[txt_i] = fileSize;  // 获取大小
          Serial.print("文件:"); Serial.print(fileName); Serial.print("，大小:"); Serial.println(fileSize);
          //如果检测到上次打开的文件名存在，就将本次打开的TXT文件序号记录下来，并自动跳转到上一次打开的位置
          if (strlen(eepUserSet.txtNameLastTime) > 0)
          {
            const char* character = txtName[txt_i].c_str();  //String转换char
            if (strcmp(eepUserSet.txtNameLastTime, character) == 0) txtNum = txt_i;
            txtDisplayPage = txtMainDisplay; //显示主界面
            //pageUpdataCount = 6;
            Serial.print(txtNum); Serial.print("上一次打开的文件名:"); Serial.println(eepUserSet.txtNameLastTime);
          }
          txt_i++;
        }
        //Serial.print("txtName[0]长度："); Serial.println(txtName[0].length());
        //Serial.print("txtName[1]长度："); Serial.println(txtName[1].length());
        //Serial.print("txtName[2]长度："); Serial.println(txtName[2].length());
      }
    }

    Serial.println("文件列表输出完毕");
    Serial.println("");
    /*for (uint8_t i = 0; i < 3; i++) {
      if (txtName[i].length() > 1) Serial.print("检测到TXT文件："); Serial.println(txtName[i]);
      }*/
  }
}

void display_txtRead() //TXT 阅读显示界面
{
  indexesName = txtName[txtNum] + ".i";            //定义索引文件名字格式 xxx.txt.i
  indexesFile = LittleFS.open(indexesName, "r");   //打开索引文件
  txtFile = LittleFS.open(txtName[txtNum], "r");   //打开txt文件，只限中文路径和名称

  //检查是否正常打开
  if (txtFile != 1) {
    //Serial.print(txtName); Serial.println(" 打开失败");
    display_partialLine(1, txtName[txtNum] + " 打开失败");
    esp_sleep(0);
    return;
  }
  //Serial.print(txtName[txtNum]); Serial.println(" 打开成功");
  if (indexesFile != 1) {
    Serial.println("检测到为首次打开，正在创建索引文件，请耐心等待...");
    display_txt_total_page_count(); //执行创建索引程序
  }
  indexesFile.close();

  //查看索引末14-1的位数据
  String txt_syys_str = "";  // 索引记录的页数
  String txt_sydx_str = "";  // 索引记录的文件大小

  indexesFile = LittleFS.open(indexesName, "r");   // 打开索引文件

  txt_zys = (indexesFile.size() / 7) - 1;     // 获取总页数

  indexesFile.seek(14, SeekEnd);    // 从末尾开始偏移14位

  for (uint8_t i = 0; i < 7; i++)  // 获取索引末14-8位，记录txt文件大小用
  {
    char c = indexesFile.read();
    txt_sydx_str += c;
  }
  uint32_t txt_sydx_uint32 = atol(txt_sydx_str.c_str()); // 转换成int格式

  for (uint8_t i = 0; i < 7; i++)  // 获取索引末7-1位，记录上一次打开的页数
  {
    char c = indexesFile.read();
    txt_syys_str += c;
  }
  uint32_t txt_syys_uint32 = atol(txt_syys_str.c_str()); // 转换成int格式

  pageCurrent = txt_syys_uint32;                         // 索引末7-1位转换后的值就是当前页

  indexesFile.close();

  Serial.print("索引末14-8位："); Serial.println(txt_sydx_uint32);
  Serial.print("索引末7-1位："); Serial.println(txt_syys_uint32);
  Serial.print("总页数：");   Serial.println(txt_zys);

  //检查索引末14-8位的数据是否与TXT文件大小一致
  if (txt_sydx_uint32 != txtFile.size()) {
    Serial.println("索引末14-8位的数据与当前打开的TXT文件大小不一致，重新创建索引！");
    indexesFailState = 1;
    LittleFS.remove(indexesName);          //删除无效的索引
    display_txt_total_page_count();        //执行创建索引程序
    pageCurrent = 0;
  }
  else Serial.println("校验成功，索引末14-8位的数据与当前打开的TXT文件大小一致");

  //第0页则直接开始，发送下一页指令即可
  if (pageCurrent == 0) PageChange = 1;
  else {
    pageCurrent += 1;
    PageChange = 2; //第2页以上则利用上一页命令来偏移到指定位置，但要加多一页，发送上一页指令
  }

  //存储这次打开的文件名到eeprom中,String转char[]
  uint8_t y;
  for (y = 0; y < txtName[txtNum].length(); y++) {
    eepUserSet.txtNameLastTime[y] = txtName[txtNum][y];
  }
  eepUserSet.txtNameLastTime[y] = '\0';
  EEPROM.put(eeprom_address0, eepUserSet);
  EEPROM.commit(); //保存
  //防进入时按键3没有松开导致误执行上一页程序，这会导致无法回到上次阅读的位置
  while (digitalRead(key_yb) == 0) ESP.wdtFeed();// 喂狗
  delay(100);
  while (digitalRead(key_yb) == 0) ESP.wdtFeed();// 喂狗

  pageUpdataCount = 6;

  //**************** 正式看书程序 ****************
  overtime = millis(); //记录时间，超时休眠用
  while (txtDisplayPage != txtFileDisplay)
  {
    ESP.wdtFeed();       // 喂狗
    get_bat_vcc();
    if (bat_vcc <= BatLow)                 display_bitmap_sleep("* 电量过低 *");
    else if (millis() - overtime > 600000) display_bitmap_sleep("* 阅读超时 *");

    if (txtDisplayPage == txtMenuMainDisplay)        // 主菜单显示和按键决策txtMenuState
    {
      display_txtMenu();
      key_txtMenu();
    }
    else if (txtDisplayPage == txtMenukeyBoardDisplay)  // 键盘显示和按键决策 keyBoardState
    {
      display_keyBoard();
      key_txtKeyBoard();
    }
    else if ( txtDisplayPage == txtMenuClockDisplay)
      display_txtClockCalibrate();

    else if (txtDisplayPage == txtMainDisplay)  //阅读界面的按键决策，显示界面在loop
      key_txtMode();      // 上下页切换按键决策

    //显示
    if (PageChange == 1) //下一页
    {
      FixedRefresh(); //屏幕初始化及定次刷新
      /*Serial.print("当前页："); Serial.println(pageCurrent);
        Serial.print("读取 "); Serial.print(txtName); Serial.println("...");
        Serial.print("等待读取的字节数：");
        Serial.println(txtFile.available());
        Serial.print("下一页已执行，目前页首位置：");
        Serial.println(txtFile.position());*/
      String txt[8 + 1] = {};  // 0-7行为一页 共8行
      int8_t line = 0;         // 当前行
      char c;                  // 中间数据
      uint16_t en_count = 0;   // 统计ascii和ascii扩展字符 1-2个字节
      uint16_t ch_count = 0;   // 统计中文等 3个字节的字符
      uint8_t line_old = 0;    //记录旧行位置
      boolean hskgState = 1;   //行首4个空格检测 0-检测过 1-未检测
      while (line < 8)
      {
        overtime = millis(); //重置超时时间
        ESP.wdtFeed();          // 喂狗

        if (line_old != line) //行首4个空格检测状态重置
        {
          line_old = line;
          hskgState = 1;
        }

        c = txtFile.read();   // 读取一个字节

        while (c == '\n' && line <= 7) // 检查换行符,并将多个连续空白的换行合并成一个
        {
          // 检测到首行并且为空白则不需要插入换行
          if (line == 0) //等于首行，并且首行不为空，才插入换行
          {
            if (txt[line].length() > 0) line++; //换行
            else txt[line].clear();
          }
          else //非首行的换行检测
          {
            //连续空白的换行合并成一个
            if (txt[line].length() > 0) line++;
            else if (txt[line].length() == 0 && txt[line - 1].length() > 0) line++;
            /*else if (txt[line].length() == 1 && txt[line - 1].length() == 1) hh = 0;*/
          }

          if (line <= 7)c = txtFile.read();
          en_count = 0;
          ch_count = 0;
        }

        if (c == '\t') //检查水平制表符 tab
        {
          if (txt[line].length() == 0) txt[line] += "    "; //行首的一个水平制表符 替换成4个空格
          else                         txt[line] += "       ";//非行首的一个水平制表符 替换成7个空格
        }
        else if ((c >= 0 && c <= 31) || c == 127) //检查没有实际显示功能的字符
        {
          //ESP.wdtFeed();  // 喂狗
        }
        else txt[line] += c;
        //检查字符的格式 + 数据处理 + 长度计算
        boolean asciiState = 0;
        byte a = B11100000;
        byte b = c & a;

        if (b == B11100000) //中文等 3个字节
        {
          ch_count ++;
          c = txtFile.read();
          txt[line] += c;
          c = txtFile.read();
          txt[line] += c;
        }
        else if (b == B11000000) //ascii扩展 2个字节
        {
          en_count += 14;
          c = txtFile.read();
          txt[line] += c;
        }
        else if (c == '\t') //水平制表符，代替两个中文位置，14*2
        {
          if (txt[line] == "    ") en_count += 20; //行首，因为后面会检测4个空格再加8所以这里是20
          else en_count += 28; //非行首
        }
        else if (c >= 0 && c <= 255)
        {
          en_count += getCharLength(c) + 1;
          asciiState = 1;
        }

        uint16_t StringLength = en_count + (ch_count  * 14);

        if (StringLength >= 260 && hskgState) //检测到行首的4个空格预计的长度再加长一点
        {
          if (txt[line][0] == ' ' && txt[line][1] == ' ' &&
              txt[line][2] == ' ' && txt[line][3] == ' ') {
            en_count += 8;
          }
          hskgState = 0;
        }

        /*if (line == 4)
          {
          Serial.println("");
          Serial.println(txt[line]);
          Serial.print("ch_count:"); Serial.println(ch_count);
          Serial.print("en_count:"); Serial.println(en_count);
          Serial.print("预计像素长度:"); Serial.println(StringLength);
          Serial.print("实际像素长度:"); Serial.println(u8g2Fonts.getUTF8Width(txt[line].c_str()));
          }*/

        if (StringLength >= 283) //检查是否已填满屏幕 283
        {
          //Serial.println("");
          //Serial.print("行"); Serial.print(line); Serial.print(" 预计像素长度:"); Serial.println(StringLength);
          //Serial.print("行"); Serial.print(line); Serial.print(" 实际像素长度:"); Serial.println(u8g2Fonts.getUTF8Width(txt[line].c_str()));
          if (asciiState == 0) //最后一个字符是中文，直接换行
          {
            line++;
            en_count = 0;
            ch_count = 0;
          }
          else if (StringLength >= 286) //286 最后一个字符不是中文，在继续检测
          {

            char t = txtFile.read();
            txtFile.seek(-1, SeekCur); //往回移
            int8_t cz =  294 - StringLength;
            int8_t t_length = getCharLength(t);
            /*Serial.print("字符t:"); Serial.println(t);
              Serial.print("字符t:"); Serial.println(t, HEX);
              Serial.print("t长度:"); Serial.println(t_length);
              Serial.print("差值:"); Serial.println(cz);*/
            byte a = B11100000;
            byte b = t & a;
            if (b == B11100000 || b == B11000000) //中文 ascii扩展
            {
              line++;
              en_count = 0;
              ch_count = 0;
              //Serial.println("测试2");
            }
            else if (t_length > cz)
            {
              line++;
              en_count = 0;
              ch_count = 0;
              //Serial.println("测试3");
            }

          }
        }
      }
      //for (uint8_t i = 0; i < 8; i++) Serial.println(txt[i]); //串口输出内容
      display_TxtOnePage(txt); //显示一页txt
      pageCurrent++;  //当前页加1

      //清空内容
      /*for (uint8_t i = 0; i < 9; i++) txt[i].clear();
        line = 0;
        en_count = 0;
        ch_count = 0;*/
      PageChange = 0; //换页命令归零

      //将当前页存入索引文件的末尾七位
      uint32_t yswz_uint32 = pageCurrent;
      String yswz_str = "";
      if (yswz_uint32 >= 1000000)     yswz_str += String(yswz_uint32);
      else if (yswz_uint32 >= 100000) yswz_str += String("0") + String(yswz_uint32);
      else if (yswz_uint32 >= 10000)  yswz_str += String("00") + String(yswz_uint32);
      else if (yswz_uint32 >= 1000)   yswz_str += String("000") + String(yswz_uint32);
      else if (yswz_uint32 >= 100)    yswz_str += String("0000") + String(yswz_uint32);
      else if (yswz_uint32 >= 10)     yswz_str += String("00000") + String(yswz_uint32);
      else                            yswz_str += String("000000") + String(yswz_uint32);

      indexesFile = LittleFS.open(indexesName, "r+");   //打开索引文件，可读可写
      indexesFile.seek(7, SeekEnd); //从末尾开始偏移7位
      indexesFile.print(yswz_str);  //写入数据
      indexesFile.close();
    }

    shang_yi_ye(); //上一页

  }
  txtFile.close();
}

void shang_yi_ye() //上一页,计算上一页页首的坐标，在发送下一页指令
{
  if (PageChange == 2)
  {
    uint32_t gbwz = 0;    //计算上一页的页首位置
    String gbwz_str = ""; //光标位置String
    if (pageCurrent == 1 || pageCurrent == 0) //第0,1页不允许上一页
    {
      PageChange = 0;
      return;
    }
    else if (pageCurrent == 2)
    {
      txtFile.seek(0, SeekSet);  // 偏移 第二页的上一页的页首位置就是0
      PageChange = 1;            // 发送下一页指令
    }
    else
    {
      //Serial.print("当前页1："); Serial.println(pageCurrent);
      //计算上一页的页首位置
      //因为第一页不需要记录所以要减1，因为我要的是上一页所以再减1
      gbwz = (pageCurrent - 2) * 7 - 7;
      //Serial.print("gbwz："); Serial.println(gbwz);
      //打开索引，寻找上一页的页首位置
      indexesFile = LittleFS.open(indexesName, "r");
      indexesFile.seek(gbwz, SeekSet);
      //获取索引的数据
      for (uint8_t i = 0; i < 7; i++)
      {
        char c = indexesFile.read();
        gbwz_str += c;
      }
      uint32_t gbwz_uint32 = atol(gbwz_str.c_str()); //装换成int格式
      indexesFile.close();
      txtFile.seek(gbwz_uint32, SeekSet); //索引的数据就是TXT文件的偏移量
      //Serial.print("偏移量："); Serial.println(gbwz_uint32);
      PageChange = 1; //发送下一页指令
    }
    pageCurrent -= 2; //屏幕读取后会加1页，所以减2
    //Serial.print("当前页2："); Serial.println(pageCurrent);
  }
}

void display_TxtOnePage(String *zfc) //发送一页TXT信息到屏幕 0-7行
{
  //u8g2Fonts.setFont(chinese_gb2312);
  display.setPartialWindow(0, 0, 296, 128);
  display.firstPage();
  do
  {
    for (uint8_t i = 0; i < 8; i++)
    {
      //Serial.print(line); Serial.print(" 预计像素长度(0):"); Serial.println(StringLength);
      //Serial.print(line); Serial.print(" 实际像素长度(0):"); Serial.println(u8g2Fonts.getUTF8Width(txt[line].c_str()));
      //检测到0-3都为空格时，将该行X坐标后移，以对齐

      //Serial.print("首行0："); Serial.println(zfc[i][0], HEX);
      //Serial.print("首行1："); Serial.println(zfc[i][1], HEX);
      /*Serial.print(i); Serial.print("行0："); Serial.println(zfc[i][0], HEX);
        Serial.print(i); Serial.print("行1："); Serial.println(zfc[i][1], HEX);
        Serial.print(i); Serial.print("行2："); Serial.println(zfc[i][2], HEX);
        Serial.print(i); Serial.print("行3："); Serial.println(zfc[i][3], HEX);
        Serial.print(i); Serial.print("行4："); Serial.println(zfc[i][4], HEX);
        Serial.print(i); Serial.print("行5："); Serial.println(zfc[i][5], HEX);*/
      uint8_t offset = 0; //缩减偏移量
      if (zfc[i][0] == 0x20) //检查首行是否为半角空格 0x20
      {
        //继续检测后3位是否为半角空格，检测到连续的4个半角空格，偏移12个像素
        if (zfc[i][1] == 0x20 && zfc[i][2] == 0x20 && zfc[i][3] == 0x20)
          offset = 12;
      }
      else if (zfc[i][0] == 0xE3 && zfc[i][1] == 0x80 && zfc[i][2] == 0x80) //检查首行是否为全角空格 0x3000 = E3 80 80
      {
        //继续检测后2位是否为全角空格，检测到连续的2个全角空格，偏移2个像素
        if (zfc[i][3] == 0xE3 && zfc[i][4] == 0x80 && zfc[i][5] == 0x80)
          offset = 2;
      }

      u8g2Fonts.setCursor(2 + offset, i * 16 + 13);
      u8g2Fonts.print(zfc[i]);
      //const char* zifuchuan = zfc[i].c_str();
      //Serial.print(zfc[i]);
      //Serial.print("  字符长度："); Serial.print(zfc[i].length());
      //Serial.print("  像素长度："); Serial.println(u8g2Fonts.getUTF8Width(zifuchuan));
    }
  }
  while (display.nextPage());
  delay(3);
  display.powerOff(); //仅关闭屏幕电源
  //display.hibernate();  //关闭电源屏幕并进入深度睡眠

  /*for (char i = 0; i < 255; i++)
    {
    String i_str = (String)i;
    const char* zf = i_str.c_str();
    Serial.print(zf); Serial.println(u8g2Fonts.getUTF8Width(zf));
    }*/
}

void display_txt_total_page_count()     //TXT 总页数计算并创建索引文件
{
  display.init(0, 0, 10, 0);   //串口使能 初始化完全刷新使能 复位时间 ret上拉使能
  display.fillScreen(heise);  // 填充屏幕
  display.display(1);         // 显示缓冲内容到屏幕，用于全屏缓冲
  display.fillScreen(baise);
  display.display(1);

  String txt[8 + 1] = {};  // 0-7行为一页 共8行
  int8_t line = 0;         // 当前行
  char c;                  // 中间数据
  uint16_t en_count = 0;   // 统计ascii和ascii扩展字符 1-2个字节
  uint16_t ch_count = 0;   // 统计中文等 3个字节的字符
  uint8_t line_old = 0;    //记录旧行位置
  boolean hskgState = 0;   //行首4个空格检测 0-检测过 1-未检测

  uint32_t pageCount = 1;      // 页数计数
  boolean line0_state = 1;     // 每页页首记录状态位
  uint32_t yswz_count = 0;      // 待写入文件统计
  String yswz_str = "";        // 待写入的文件
  uint32_t txtTotalSize = txtFile.size(); //记录该TXT文件的大小，插入到索引的倒数14-8位

  Serial.print("读取 "); Serial.print(txtName[txtNum]); Serial.println("...");
  Serial.print("建立的索引名称："); Serial.println(indexesName);
  Serial.print("等待读取的字节数：");
  Serial.println(txtFile.available());
  Serial.println("正在计算总页数...");

  uint32_t time1 = millis();
  if (indexesFailState) display_partialLine(1, "文件有变，需重新创建索引");
  else                  display_partialLine(1, "检测到为首次打开，正在创建索引文件");

  display_partialLine(2, "请耐心等待...");
  if (indexesName.length() > 31) {
    display_partialLine(6, "创建失败，TXT文件名称过长");
    display_bitmap_sleep(" ");
  }

  while (txtFile.available())
  {
    ESP.wdtFeed();       // 喂狗

    if (line_old != line) //行首4个空格检测状态重置
    {
      line_old = line;
      hskgState = 1;
    }

    if (line0_state == 1 && line == 0 && pageCount > 1)
    {
      line0_state = 0;
      uint32_t yswz_uint32 = txtFile.position(); //获取当前位置 yswz=页数位置
      //页数位置编码处理
      if (yswz_uint32 >= 1000000) yswz_str += String(yswz_uint32);
      else if (yswz_uint32 >= 100000)yswz_str += "0" + String(yswz_uint32);
      else if (yswz_uint32 >= 10000)yswz_str += "00" + String(yswz_uint32);
      else if (yswz_uint32 >= 1000)yswz_str += "000" + String(yswz_uint32);
      else if (yswz_uint32 >= 100)yswz_str += "0000" + String(yswz_uint32);
      else if (yswz_uint32 >= 10)yswz_str += "00000" + String(yswz_uint32);
      else yswz_str += "000000" + String(yswz_uint32);
      yswz_count++;
      if (yswz_count == 1000) //每500页控制屏幕显示一下当前进度
      {
        indexesFile = LittleFS.open(indexesName, "a"); //在索引文件末尾追加内容
        indexesFile.print(yswz_str); //将待写入的缓存 写入索引文件中
        indexesFile.close();

        yswz_str = "";   // 待写入文件清空
        yswz_count = 0;  // 待写入计数清空

        //计算剩余量,进度条
        uint32_t shengyu_int = txtTotalSize - txtFile.available();
        float shengyu_float = (float(shengyu_int) / float(txtTotalSize)) * 100.0;
        display_partialLine(3, String(shengyu_float) + "%");
        //Serial.println("写入索引文件");
      }
      //Serial.print("第"); Serial.print(pageCount); Serial.print("页，页首位置："); Serial.println(yswz_uint32);
    }

    c = txtFile.read();   // 读取一个字节
    while (c == '\n' && line <= 7) // 检查换行符,并将多个连续空白的换行合并成一个
    {
      // 检测到首行并且为空白则不需要插入换行
      if (line == 0) //等于首行，并且首行不为空，才插入换行
      {
        if (txt[line].length() > 0) line++; //换行
        else txt[line].clear();
      }
      else //非首行的换行检测
      {
        //连续空白的换行合并成一个
        if (txt[line].length() > 0) line++;
        else if (txt[line].length() == 0 && txt[line - 1].length() > 0) line++;
        /*else if (txt[line].length() == 1 && txt[line - 1].length() == 1) hh = 0;*/
      }
      if (line <= 7)c = txtFile.read();
      en_count = 0;
      ch_count = 0;
    }
    if (c == '\t') //检查水平制表符 tab
    {
      if (txt[line].length() == 0) txt[line] += "    "; //行首的一个水平制表符 替换成4个空格
      else                         txt[line] += "       ";//非行首的一个水平制表符 替换成7个空格
    }
    else if ((c >= 0 && c <= 31) || c == 127) //检查没有实际显示功能的字符
    {
      //ESP.wdtFeed();  // 喂狗
    }
    else txt[line] += c;

    //检查字符的格式 + 数据处理 + 长度计算
    boolean asciiState = 0;
    byte a = B11100000;
    byte b = c & a;

    if (b == B11100000) //中文等 3个字节
    {
      ch_count ++;
      c = txtFile.read();
      txt[line] += c;
      c = txtFile.read();
      txt[line] += c;
    }
    else if (b == B11000000) //ascii扩展 2个字节
    {
      en_count += 14;
      c = txtFile.read();
      txt[line] += c;
    }
    else if (c == '\t') //水平制表符，代替两个中文位置，14*2
    {
      if (txt[line] == "    ") en_count += 20; //行首，因为后面会检测4个空格再加8所以这里是20
      else en_count += 28; //非行首
    }
    else if (c >= 0 && c <= 255)
    {
      en_count += getCharLength(c) + 1; //getCharLength=获取ascii字符的像素长度
      asciiState = 1;
    }

    uint16_t StringLength = en_count + (ch_count  * 14); //一个中文14个像素长度

    if (StringLength >= 260 && hskgState) //检测到行首的4个空格预计的长度再加长一点
    {
      if (txt[line][0] == ' ' && txt[line][1] == ' ' &&
          txt[line][2] == ' ' && txt[line][3] == ' ') {
        en_count += 8;
      }
      hskgState = 0;
    }

    if (StringLength >= 283) //283个像素检查是否已填满屏幕 ，填满一行
    {
      if (asciiState == 0)
      {
        line++;
        en_count = 0;
        ch_count = 0;
      }
      else if (StringLength >= 286)
      {
        char t = txtFile.read();
        txtFile.seek(-1, SeekCur); //往回移
        int8_t cz =  294 - StringLength;
        int8_t t_length = getCharLength(t);
        byte a = B11100000;
        byte b = t & a;
        if (b == B11100000 || b == B11000000) //中文 ascii扩展
        {
          line++;
          en_count = 0;
          ch_count = 0;
        }
        else if (t_length > cz)
        {
          line++;
          en_count = 0;
          ch_count = 0;
        }
      }
    }
    if (line == 8)
    {
      line0_state = 1;
      pageCount++;
      line = 0;
      en_count = 0;
      ch_count = 0;
      for (uint8_t i = 0; i < 9; i++) txt[i].clear();
    }
  }

  //剩余的字节写入索引文件，并在末尾加入文件大小校验位14-8 页数记录位7-1
  uint32_t size_uint32 = txtTotalSize; //获取当前TXT文件的大小
  String size_str = "";
  //TXT文件大小编码处理
  if (size_uint32 >= 1000000) size_str += String(size_uint32);
  else if (size_uint32 >= 100000)size_str += String("0") + String(size_uint32);
  else if (size_uint32 >= 10000)size_str += String("00") + String(size_uint32);
  else if (size_uint32 >= 1000)size_str += String("000") + String(size_uint32);
  else if (size_uint32 >= 100)size_str += String("0000") + String(size_uint32);
  else if (size_uint32 >= 10)size_str += String("00000") + String(size_uint32);
  else size_str += String("000000") + String(size_uint32);

  if (yswz_count != 0)  //还有剩余页数就在末尾加入 剩余的页数+文件大小位+当前位置位（初始0）
  {
    indexesFile = LittleFS.open(indexesName, "a");
    indexesFile.print(yswz_str + size_str + "0000000");
  }
  else  //没有剩余页数了就在末尾加入文件大小位+当前位置位
  {
    indexesFile = LittleFS.open(indexesName, "a");
    indexesFile.print(size_str + "0000000");
  }

  uint32_t indexes_size = indexesFile.size();
  Serial.print("索引文件大小："); Serial.println(indexes_size);
  Serial.print("yswz_count："); Serial.println(yswz_count);
  Serial.print("pageCount："); Serial.println(pageCount);

  // 校验索引是否正确建立
  // 算法：一页为7个字节（从第二页开始记录所以要总页数-1），加上文件大小位7个字节，加上当前页数位7个字节
  // 所以为：7*((总页数-1)+1+1))
  if (indexes_size == 7 * ((pageCount - 1) + 1 + 1))
  {
    Serial.println("校验通过，索引文件有效");
    display_partialLine(3, "100.00%");
    display_partialLine(4, "校验通过，索引文件有效");
  }
  else
  {
    Serial.println("校验失败，索引文件无效，请重新创建");
    display_partialLine(7, "索引文件创建失败，校验失败或空间不足");
    LittleFS.remove(indexesName); //删除无效的索引
    display_bitmap_sleep(" "); //休眠
  }

  display_partialLine(5, "索引文件大小：" + byteConversion(indexes_size));

  indexesFile.close();

  yswz_str = "";
  yswz_count = 0;

  txtFile.close();
  txtFile = LittleFS.open(txtName[txtNum], "r");

  uint32_t time3 = millis() - time1;
  Serial.print("计算完毕："); Serial.print(pageCount); Serial.println("页");
  Serial.print("耗时："); Serial.print(time3); Serial.println("毫秒");
  display_partialLine(6, "耗时：" + String(float(time3) / 1000.0) + "秒");
  delay(500);
  line = 0;
  en_count = 0;
  ch_count = 0;
  for (uint8_t i = 0; i < 9; i++) txt[i].clear();
}

boolean clockInitState = 0;
void display_txtMenu() //主菜单
{
  if (displayUpdate == 0) return;
  displayUpdate = 0;

  float dqy_bfb = ((float)pageCurrent / (float)txt_zys) * 100.0; //当前页，百分比
  String dqy_str = "你已看了" + String(dqy_bfb) + "%，当前第" + String(pageCurrent) + "页，共" + String(txt_zys) + "页";
  const char* dqy_c = dqy_str.c_str();                      // String转换char
  uint16_t zf_width = u8g2Fonts.getUTF8Width(dqy_c);        // 获取字符的长度
  uint16_t dqy_x = (display.width() / 2) - (zf_width / 2);  // 计算字符居中的X坐标（屏幕宽度/2 - 字符宽度/2）

  tmElements_t tm1;
  String newTime1, newTime2;
  if (ClockReadCheck())
  {
    Wire.begin(13, 14);
    tm1 = rtc.read(eepUserSet.type_8025T);      // 读取时钟芯片数据
    newTime1 = String(tmYearToCalendar(tm1.Year)) + "/" + String(tm1.Month) + "/" + String(tm1.Day);
    newTime2 = String(tm1.Hour) + ":" + String(tm1.Minute) + ":" + String(tm1.Second);
  }
  else if (clockInitState == 0)
  {
    newTime1 = "时间";
    newTime2 = "无数据";
  }
  else if (clockInitState == 1)
  {
    newTime1 = String(eepUserClock.year) + "/" + String(eepUserClock.month) + "/" + String(eepUserClock.day);
    newTime2 = String(RTC_hour) + ":" + String(RTC_minute) + ":" + String(RTC_seconds);
    //newTime2 = String(RTC_hour) + ":" + String(RTC_minute);
  }

  display.init(0, 0, 10, 0);
  display.setPartialWindow(dqy_x - 5, 0, zf_width + 10, 128);
  display.firstPage();
  do
  {
    //外围圆角框
    display.drawRoundRect(dqy_x - 3, 0, zf_width + 6, 127, 5, 0);
    //电压显示
    float batVCC = getBatVolNew();
    uint8_t bat_bfb = getBatVolBfb(batVCC);
    if (bat_bfb > 100) bat_bfb = 100;
    display.drawInvertedBitmap(zf_width + dqy_x - 20, 2, Bitmap_dlsd, 19, 31, heise);
    u8g2Fonts.setCursor(zf_width + dqy_x - 25, 46);
    u8g2Fonts.print(batVCC);
    if (bat_bfb > 99)u8g2Fonts.setCursor(zf_width + dqy_x - 27, 62);
    else if (bat_bfb > 9)u8g2Fonts.setCursor(zf_width + dqy_x - 23, 62);
    else if (bat_bfb > 0)u8g2Fonts.setCursor(zf_width + dqy_x - 18, 62);
    u8g2Fonts.print(String(bat_bfb) + "%");
    //功能项
    u8g2Fonts.setCursor(134, 18 + 21 * 0);
    u8g2Fonts.print("继续");

    u8g2Fonts.setCursor(134, 18 + 21 * 1);
    u8g2Fonts.print("休眠");

    u8g2Fonts.setCursor(134, 18 + 21 * 2);
    u8g2Fonts.print("跳转");

    u8g2Fonts.setCursor(100, 18 + 21 * 3);
    if (eepUserSet.fastFlip)u8g2Fonts.print("快速翻页：开启");
    else u8g2Fonts.print("快速翻页：关闭");

    u8g2Fonts.setCursor(108, 18 + 21 * 4);
    u8g2Fonts.print("返回选择界面");

    u8g2Fonts.setCursor(dqy_x, 18 + 21 * 5); //显示当期阅读进度
    u8g2Fonts.print(dqy_str);

    // 时钟显示
    u8g2Fonts.setCursor(dqy_x, 18 + 21 * 0); //显示当期阅读进度
    u8g2Fonts.print(newTime1);
    u8g2Fonts.setCursor(dqy_x, 18 + 21 * 1); //显示当期阅读进度
    u8g2Fonts.print(newTime2);
    u8g2Fonts.setCursor(dqy_x, 18 + 21 * 2);
    u8g2Fonts.print("校准");

    if (txtMenuCount == 1)      display.drawRoundRect(134 - 3, 18 - 16, 33, 20, 5, 0);
    else if (txtMenuCount == 2) display.drawRoundRect(134 - 3, 39 - 16, 33, 20, 5, 0);
    else if (txtMenuCount == 3) display.drawRoundRect(134 - 3, 60 - 16, 33, 20, 5, 0);
    else if (txtMenuCount == 4) display.drawRoundRect(100 - 3, 81 - 16, 102, 20, 5, 0);
    else if (txtMenuCount == 5) display.drawRoundRect(108 - 3, 102 - 16, 89, 20, 5, 0);

    else if (txtMenuCount == 6) display.drawRoundRect(dqy_x - 2, 60 - 16, 32, 20, 5, 0);
  }
  while (display.nextPage());
  display.powerOff();
}

void display_keyBoard() //TXT阅读器菜单的数字键盘
{
  if (displayUpdate == 0) return;
  displayUpdate = 0;
  //u8g2Fonts.setFontMode(1);
  display.init(0, 0, 10, 0);
  display.setPartialWindow(76, 16, 153, 48);
  display.firstPage();
  do
  {
    uint8_t num_x = 80;
    uint8_t num_y = 42;
    uint8_t num_x1 = 10;
    display.drawRoundRect(77, 17, 152, 47, 5, 0); //圆角外框

    u8g2Fonts.setCursor(num_x, num_y);
    u8g2Fonts.print("0 1 2 3 4 5 6 7 8 9 < OK X");

    u8g2Fonts.setCursor(134, 60);
    u8g2Fonts.print("跳转");

    u8g2Fonts.setCursor(168, 60);
    u8g2Fonts.print(keyBoardNum);

    if (keyBoardCount == 0)       u8g2Fonts.setCursor(num_x - 1, num_y - 12);       //0
    else if (keyBoardCount == 1)  u8g2Fonts.setCursor(num_x + num_x1 * 1 - 1, num_y - 12); //1
    else if (keyBoardCount == 2)  u8g2Fonts.setCursor(num_x + num_x1 * 2, num_y - 12); //2
    else if (keyBoardCount == 3)  u8g2Fonts.setCursor(num_x + num_x1 * 3 + 2, num_y - 12); //3
    else if (keyBoardCount == 4)  u8g2Fonts.setCursor(num_x + num_x1 * 4 + 3, num_y - 12); //4
    else if (keyBoardCount == 5)  u8g2Fonts.setCursor(num_x + num_x1 * 5 + 3, num_y - 12); //5
    else if (keyBoardCount == 6)  u8g2Fonts.setCursor(num_x + num_x1 * 6 + 5, num_y - 12); //6
    else if (keyBoardCount == 7)  u8g2Fonts.setCursor(num_x + num_x1 * 7 + 6, num_y - 12); //7
    else if (keyBoardCount == 8)  u8g2Fonts.setCursor(num_x + num_x1 * 8 + 7, num_y - 12); //8
    else if (keyBoardCount == 9)  u8g2Fonts.setCursor(num_x + num_x1 * 9 + 8, num_y - 12); //9
    else if (keyBoardCount == 10) u8g2Fonts.setCursor(num_x + num_x1 * 10 + 9, num_y - 12); //<
    else if (keyBoardCount == 11) u8g2Fonts.setCursor(num_x + num_x1 * 11 + 14, num_y - 12); //OK
    else if (keyBoardCount == 12) u8g2Fonts.setCursor(num_x + num_x1 * 13 + 10, num_y - 12); //X
    u8g2Fonts.print("*");
  }
  while (display.nextPage());
  //display.powerOff();
}

void zhidingDisplay(uint16_t x , uint16_t y, String t) //指定刷新位置
{
  display.setPartialWindow(76, 16, 153, 48);
  display.firstPage();
  do
  {
    display.drawRoundRect(77, 17, 152, 47, 5, 0); //圆角外框
    u8g2Fonts.setCursor(x, y);
    u8g2Fonts.print(t);
  }
  while (display.nextPage());
  display.powerOff(); //关闭屏幕电源
}
void txtClockCalibrateOff() //时钟停止
{
  displayUpdate = 1;//允许更新屏幕
  txtDisplayPage = txtMenuMainDisplay; //切换至主菜单
  timeClient.end();
  WifiShutdown(); //正真的关闭WIFI
}
void display_txtClockCalibrate() //时钟校准
{
  if (displayUpdate == 0) return;
  displayUpdate = 0;
  display.init(0, 0, 10, 0);
  //display.setPartialWindow(76, 16, 153, 48);

  connectToWifi();
  uint8_t wifi_count = 0;
  while (!WiFi.isConnected()) //wifiMulti.run() != WL_CONNECTED
  {
    zhidingDisplay(80, 48, "连接 " + WiFi.SSID() + " " + String(wifi_count));
    //Serial.println("连接WiFi中");
    delay(1000);
    wifi_count++;
    if (wifi_count > 15)
    {
      zhidingDisplay(80, 48, "连接 " + WiFi.SSID() + " 失败");
      delay(1000);
      return txtClockCalibrateOff();
    }
  }

  zhidingDisplay(80, 48, "获取日期");
  if (ParseRiQi(callHttps("https://api.xygeng.cn/day"), &riqi))
  {
    RTC_clock_code = 200; // 错误代码
    ESP.rtcUserMemoryWrite(RTCdz_clock_code, &RTC_clock_code, sizeof(RTC_clock_code));
    strcpy(eepUserClock.year, riqi.year);         // 年
    strcpy(eepUserClock.month, riqi.month);       // 月
    strcpy(eepUserClock.day, riqi.date);          // 日
    strcpy(eepUserClock.week, riqi.day);          // 星期几
    strcpy(eepUserClock.festival, riqi.festival); // 节日名
    EEPROM.put(eeprom_address1, eepUserClock);
    EEPROM.commit(); //保存
    EEPROM.get(eeprom_address1, eepUserClock);
    zhidingDisplay(80, 48, "获取日期：成功");
  }
  else {
    zhidingDisplay(80, 48, "获取日期：失败 " + String(riqi.code));
    delay(600);
    return txtClockCalibrateOff();
  }

  uint8_t update_count = 0;
  timeClient.begin();
  while (timeClient.update() == 0 && update_count < 10)
  {
    zhidingDisplay(80, 48, "获取网络时间中");
    //Serial.println("获取网络时间中");
    update_count++;
    delay(900);
    if (update_count == 3)      timeClient.setPoolServerName("s2k.time.edu.cn");
    else if (update_count == 5) timeClient.setPoolServerName("1d.time.edu.cn");
    else if (update_count == 7) timeClient.setPoolServerName("s1c.time.edu.cn");
    else if (update_count == 9) timeClient.setPoolServerName("ntp.sjtu.edu.cn");
  }
  if (update_count >= 13)
  {
    zhidingDisplay(80, 48, "获取网络时间：失败");
    delay(600);
    return txtClockCalibrateOff();
  }
  else
  {
    zhidingDisplay(80, 48, "获取网络时间：成功");

    RTC_hour = timeClient.getHours();
    RTC_minute = timeClient.getMinutes();
    RTC_seconds = timeClient.getSeconds();

    ESP.rtcUserMemoryWrite(RTCdz_minute, &RTC_minute, sizeof(RTC_minute));
    ESP.rtcUserMemoryWrite(RTCdz_hour, &RTC_hour, sizeof(RTC_hour));
    ESP.rtcUserMemoryWrite(RTCdz_seconds, &RTC_seconds, sizeof(RTC_seconds));

    //将时间数据写入BL8025T时钟芯片并检查数据完整性
    tmElements_t tm;
    tm.Year = CalendarYrToTm(atoi(eepUserClock.year));
    tm.Month = atoi(eepUserClock.month);
    tm.Day = atoi(eepUserClock.day);
    tm.Hour = RTC_hour;
    tm.Minute = RTC_minute;
    tm.Second = RTC_seconds;
    if (tmYearToCalendar(tm.Year) < 2020 || tmYearToCalendar(tm.Year) > 2099) RTC_8025T_error = 1;
    else if (tm.Month <= 0 || tm.Month > 12) RTC_8025T_error = 2;
    else if (tm.Day <= 0 || tm.Day > 31) RTC_8025T_error = 3;
    else if (tm.Hour < 0 || tm.Hour > 24) RTC_8025T_error = 4;
    else if (tm.Minute < 0 || tm.Minute > 60) RTC_8025T_error = 5;
    else if (tm.Second < 0 || tm.Second > 60) RTC_8025T_error = 6;
    else RTC_8025T_error = 0;
    if (RTC_8025T_error == 0)
    {
      Wire.begin(13, 14); // 初始化I2C引脚
      rtc.write(tm);      // 写入时钟芯片数据
    }
    else {
      zhidingDisplay(80, 48, "时钟数据出错：" + String(RTC_8025T_error));
      delay(600);
      return txtClockCalibrateOff();
    }

    //检查BL8025T时钟芯片读取数据是否有错
    Wire.begin(13, 14); // 初始化I2C引脚
    tm = rtc.read(eepUserSet.type_8025T);    // 读取时钟芯片数据
    display.init(0, 0, 10, 0);
    if (tmYearToCalendar(tm.Year) < 2020 || tmYearToCalendar(tm.Year) > 2099) RTC_8025T_error = 1;
    else if (tm.Month <= 0 || tm.Month > 12) RTC_8025T_error = 2;
    else if (tm.Day <= 0 || tm.Day > 31) RTC_8025T_error = 3;
    else if (tm.Hour < 0 || tm.Hour > 24) RTC_8025T_error = 4;
    else if (tm.Minute < 0 || tm.Minute > 60) RTC_8025T_error = 5;
    else if (tm.Second < 0 || tm.Second > 60) RTC_8025T_error = 6;
    else RTC_8025T_error = 0;
    if (RTC_8025T_error == 0)
    {
      zhidingDisplay(80, 48, "已写入时钟芯片");
      delay(600);
      return txtClockCalibrateOff();
    }
    else
    {
      //txtClock.attach(59.7, tickerTxtClock); //启动软件定时器
      if (ITimer.attachInterruptInterval(1000 * 1000, tickerTxtClock)) //1秒 硬件定时器
      {
        clockInitState = 1; //标记初始化成功
        zhidingDisplay(80, 48, "定时器启动成功");
      }
      else zhidingDisplay(80, 48, "定时器启动失败");
      delay(600);
      txtClockCalibrateOff();
      return;
    }
  }
}

void display_txtFliaMain() //txt文件选择菜单
{
  /*struct FSInfo {
    size_t totalBytes;    // 整个文件系统的大小
    size_t usedBytes;     // 文件系统所有文件占用的大小
    size_t blockSize;     // LittleFS块大小
    size_t pageSize;      // LittleFS逻辑页数大小
    size_t maxOpenFiles;  // 能够同时打开的文件最大个数
    size_t maxPathLength; // 文件名最大长度（包括一个字节的字符串结束符）
    };*/

  if (millis() - overtime > 300000) display_bitmap_sleep("* 文件选择超时 *");

  if (displayUpdate == 0) return;
  displayUpdate = 0;

  FixedRefresh(); //屏幕初始化及定次刷新

  boolean info_state;            // 获取文件系统参数状态 0-失败 1-成功

  FSInfo p;                      // 创建结构体
  info_state = LittleFS.info(p); // 获取的值放在结构体中

  uint32_t sy_zj = p.totalBytes - p.usedBytes; //剩余空间，字节

  float sy_bfb = ((float)sy_zj / (float)p.totalBytes) * 100.0; //剩余空间，百分比

  String info_zfc = "剩余:" + String(sy_bfb) + "%，" + byteConversion(sy_zj) + "（" +
                    String(p.usedBytes) + "/" + String(p.totalBytes) + "）";

  String txtName_zfc[3] = {""};


  for (uint8_t i = 0; i < 3; i++)
  {
    if (txtName[i].length() > 0)
    {
      txtName_zfc[i] = String(i + 1) + "." + txtName[i] + "  " + byteConversion(txtSize[i]);
    }
    else txtName_zfc[i] = String(i + 1) + "." + "无";
  }

  display.setPartialWindow(0, 0, 296, 128);
  display.firstPage();
  do
  {
    if (info_state) {
      display.drawInvertedBitmap(1, 1, Bitmap_txtMain, 32, 32, heise);
      u8g2Fonts.setCursor(32, 15);
      u8g2Fonts.print("阅读模式");
      u8g2Fonts.setCursor(32, 31);
      u8g2Fonts.print("请选择文件 仅支持UTF-8 10MB以内");

      for (uint8_t i = 0; i < 3; i++) {
        u8g2Fonts.setCursor(3,  21 * i + 52);
        u8g2Fonts.print(txtName_zfc[i]);
        u8g2Fonts.setCursor(230, 21 * i + 52);
        u8g2Fonts.print("打开");
        u8g2Fonts.setCursor(266, 21 * i + 52);
        u8g2Fonts.print("删除");
      }

      u8g2Fonts.setCursor(5, 114);
      u8g2Fonts.print(info_zfc);
      uint32_t sy_map = 289 * (sy_bfb / 100); //计算实心进度条的长度 剩余的字节占比百分比
      //Serial.print("sy_map："); Serial.println(sy_map);
      display.drawRoundRect(3, 117, 292, 10, 3, 0);     //圆角外框空心
      display.fillRoundRect(3, 117, sy_map, 10, 3, 0);  //圆角内框实心

      //圆角外框空心
      if (txtFileMainCount == 1)      display.drawRoundRect(230 - 3, 52 - 16, 33, 20, 5, 0);
      else if (txtFileMainCount == 2) display.drawRoundRect(266 - 3, 52 - 16, 33, 20, 5, 0);

      else if (txtFileMainCount == 3) display.drawRoundRect(230 - 3, 73 - 16, 33, 20, 5, 0);
      else if (txtFileMainCount == 4) display.drawRoundRect(266 - 3, 73 - 16, 33, 20, 5, 0);

      else if (txtFileMainCount == 5) display.drawRoundRect(230 - 3, 94 - 16, 33, 20, 5, 0);
      else if (txtFileMainCount == 6) display.drawRoundRect(266 - 3, 94 - 16, 33, 20, 5, 0);
    }
    else {
      u8g2Fonts.setCursor(108, 127);
      u8g2Fonts.print("获取info失败");
    }
  }
  while (display.nextPage());
  //display.hibernate();
  display.powerOff(); //仅关闭电源
}

int8_t getCharLength(char zf) //获取ascii字符的长度
{
  if (zf == 0x20) return 4;      //空格
  else if (zf == '!') return 4;
  else if (zf == '"') return 5;
  else if (zf == '#') return 5;
  else if (zf == '$') return 6;
  else if (zf == '%') return 7;
  else if (zf == '&') return 7;
  else if (zf == '\'') return 3;
  else if (zf == '(') return 5;
  else if (zf == ')') return 5;
  else if (zf == '*') return 7;
  else if (zf == '+') return 7;
  else if (zf == ',') return 3;
  else if (zf == '.') return 3;

  else if (zf == '1') return 5;
  else if (zf == ':') return 4;
  else if (zf == ';') return 4;
  else if (zf == '@') return 9;

  else if (zf == 'A') return 8;
  else if (zf == 'D') return 7;
  else if (zf == 'G') return 7;
  else if (zf == 'H') return 7;
  else if (zf == 'I') return 3;
  else if (zf == 'J') return 3;
  else if (zf == 'M') return 8;
  else if (zf == 'N') return 7;
  else if (zf == 'O') return 7;
  else if (zf == 'Q') return 7;
  else if (zf == 'T') return 7;
  else if (zf == 'U') return 7;
  else if (zf == 'V') return 7;
  else if (zf == 'W') return 11;
  else if (zf == 'X') return 7;
  else if (zf == 'Y') return 7;
  else if (zf == 'Z') return 7;

  else if (zf == '[') return 5;
  else if (zf == ']') return 5;
  else if (zf == '`') return 5;

  else if (zf == 'c') return 5;
  else if (zf == 'f') return 5;
  else if (zf == 'i') return 1;
  else if (zf == 'j') return 2;
  else if (zf == 'k') return 5;
  else if (zf == 'l') return 2;
  else if (zf == 'm') return 9;
  else if (zf == 'o') return 7;
  else if (zf == 'r') return 4;
  else if (zf == 's') return 5;
  else if (zf == 't') return 4;
  else if (zf == 'v') return 7;
  else if (zf == 'w') return 9;
  else if (zf == 'x') return 5;
  else if (zf == 'y') return 7;
  else if (zf == 'z') return 5;

  else if (zf == '{') return 5;
  else if (zf == '|') return 4;
  else if (zf == '}') return 5;

  else if ((zf >= 0 && zf <= 31) || zf == 127) return -1; //没有实际显示功能的字符

  else return 6;
}
