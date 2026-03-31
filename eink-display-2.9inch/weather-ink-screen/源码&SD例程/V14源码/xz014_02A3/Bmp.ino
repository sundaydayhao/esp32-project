//请注意，BMP位图是在屏幕物理方向的物理位置绘制的
static const uint16_t input_buffer_pixels = 50; // 可能会影响性能

static const uint16_t max_row_width = 400;      // for up to 6" display 1448x1072
static const uint16_t max_palette_pixels = 300; // for depth <= 8

uint8_t input_buffer[3 * input_buffer_pixels];        // up to depth 24
uint8_t output_row_mono_buffer[max_row_width / 8];    // buffer for at least one row of b/w bits
uint8_t output_row_color_buffer[max_row_width / 8];   // buffer for at least one row of color bits
uint8_t mono_palette_buffer[max_palette_pixels / 8];  // palette buffer for depth <= 8 b/w
uint8_t color_palette_buffer[max_palette_pixels / 8]; // palette buffer for depth <= 8 c/w
uint16_t rgb_palette_buffer[max_palette_pixels];      // palette buffer for depth <= 8 for buffered graphics, needed for 7-color display

void drawBitmapFileSystem(const char *filename, int16_t x, int16_t y, bool with_color)
{
  File file; //创建文件对象file
  bool valid = false;  // 要处理的有效格式
  bool flip = true;    // 位图自下而上存储
  uint32_t startTime = millis();
  if ((x >= display.epd2.WIDTH) || (y >= display.epd2.HEIGHT)) {
    display_bitmap_sleep("图片太大");
  }
  Serial.println();
  Serial.print("正在加载图像 ");
  Serial.println(filename);
  String bmpname = filename;
  file = LittleFS.open(filename, "r");   //r只读 r+可读可写
  if (!file)
  {
    display.init();
    file.close();
    strcpy(eepUserSet.customBmp, String("").c_str()); // 清除数据
    EEPROM.put(eeprom_address0, eepUserSet);
    EEPROM.commit(); //保存
    /*display.fillScreen(heise);  // 填充屏幕
    display.display(1);         // 显示缓冲内容到屏幕，用于全屏缓冲
    display.fillScreen(baise);
    display.display(1);*/
    Serial.println("图片不存在");
    display_bitmap_sleep(bmpname + "不存在");
    return;
  }

  // 解析BMP头
  if (read16(file) == 0x4D42) // BMP签名
  {
    uint32_t fileSize = read32(file);     // 文件大小   4字节
    uint32_t creatorBytes = read32(file); // 创建者字节 保留属性，都为0
    uint32_t imageOffset = read32(file);  // 图像数据的开始 表示DIB数据区在bmp文件中的位置偏移量
    uint32_t headerSize = read32(file);   // 标题大小 指定此结构体的长度
    uint32_t width  = read32(file);       // 图像宽度
    uint32_t height = read32(file);       // 图像高度
    uint16_t planes = read16(file);       // 平面数，为1
    uint16_t depth = read16(file);        // 采用颜色位数，可以是1，2，4，8，16，24，新的可以是32
    uint32_t format = read32(file);       // 表示图片的压缩属性 bmp图片是不压缩的，等于0，所以这里为0x00000000
    if ((planes == 1) && ((format == 0) || (format == 3))) // 未压缩处理, 565 同样
    {
      Serial.print("文件大小： "); Serial.println(fileSize);
      Serial.print("图像偏移："); Serial.println(imageOffset);
      Serial.print("标题大小："); Serial.println(headerSize);
      Serial.print("位深："); Serial.println(depth);
      Serial.print("图像大小：");
      Serial.print(width);
      Serial.print('x');
      Serial.println(height);
      // BMP行填充为4字节边界（如果需要）
      uint32_t rowSize = (width * depth / 8 + 3) & ~3;
      if (depth < 8) rowSize = ((width * depth + 8 - depth) / 8 + 3) & ~3;
      if (height < 0)
      {
        height = -height;
        flip = false;
      }
      uint16_t w = width;
      uint16_t h = height;
      if ((x + w - 1) >= display.epd2.WIDTH)  w = display.epd2.WIDTH  - x;
      if ((y + h - 1) >= display.epd2.HEIGHT) h = display.epd2.HEIGHT - y;
      if (w <= max_row_width) // 直接绘图处理
      {
        valid = true;
        uint8_t bitmask = 0xFF;
        uint8_t bitshift = 8 - depth;
        uint16_t red, green, blue;
        bool whitish, colored;
        if (depth == 1) with_color = false;
        if (depth <= 8)
        {
          if (depth < 8) bitmask >>= depth;
          //file.seekSet(54); //palette is always @ 54
          file.seek(imageOffset - (4 << depth), SeekSet); //转换成8266文件系统的偏移api
          for (uint16_t pn = 0; pn < (1 << depth); pn++)
          {
            blue  = file.read();
            green = file.read();
            red   = file.read();
            file.read();
            //Serial.print(red); Serial.print(" "); Serial.print(green); Serial.print(" "); Serial.println(blue);
            whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80); // whitish
            colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0)); // reddish or yellowish?
            if (0 == pn % 8) mono_palette_buffer[pn / 8] = 0;
            mono_palette_buffer[pn / 8] |= whitish << pn % 8;
            if (0 == pn % 8) color_palette_buffer[pn / 8] = 0;
            color_palette_buffer[pn / 8] |= colored << pn % 8;
          }
        }
        //display.clearScreen();
        uint32_t rowPosition = flip ? imageOffset + (height - h) * rowSize : imageOffset;
        for (uint16_t row = 0; row < h; row++, rowPosition += rowSize) // for each line
        {
          uint32_t in_remain = rowSize;
          uint32_t in_idx = 0;
          uint32_t in_bytes = 0;
          uint8_t in_byte = 0; // for depth <= 8
          uint8_t in_bits = 0; // for depth <= 8
          uint8_t out_byte = 0xFF; // white (for w%8!=0 border)
          uint8_t out_color_byte = 0xFF; // white (for w%8!=0 border)
          uint32_t out_idx = 0;
          file.seek(rowPosition, SeekSet); //转换成8266文件系统的偏移api
          for (uint16_t col = 0; col < w; col++) // for each pixel
          {
            // Time to read more pixel data?
            if (in_idx >= in_bytes) // ok, exact match for 24bit also (size IS multiple of 3)
            {
              in_bytes = file.read(input_buffer, in_remain > sizeof(input_buffer) ? sizeof(input_buffer) : in_remain);
              in_remain -= in_bytes;
              in_idx = 0;
            }
            switch (depth)
            {
              case 24:
                blue = input_buffer[in_idx++];
                green = input_buffer[in_idx++];
                red = input_buffer[in_idx++];
                whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80); // whitish
                colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0)); // reddish or yellowish?
                break;
              case 16:
                {
                  uint8_t lsb = input_buffer[in_idx++];
                  uint8_t msb = input_buffer[in_idx++];
                  if (format == 0) // 555
                  {
                    blue  = (lsb & 0x1F) << 3;
                    green = ((msb & 0x03) << 6) | ((lsb & 0xE0) >> 2);
                    red   = (msb & 0x7C) << 1;
                  }
                  else // 565
                  {
                    blue  = (lsb & 0x1F) << 3;
                    green = ((msb & 0x07) << 5) | ((lsb & 0xE0) >> 3);
                    red   = (msb & 0xF8);
                  }
                  whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80); // whitish
                  colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0)); // reddish or yellowish?
                }
                break;
              case 1:
              case 4:
              case 8:
                {
                  if (0 == in_bits)
                  {
                    in_byte = input_buffer[in_idx++];
                    in_bits = 8;
                  }
                  uint16_t pn = (in_byte >> bitshift) & bitmask;
                  whitish = mono_palette_buffer[pn / 8] & (0x1 << pn % 8);
                  colored = color_palette_buffer[pn / 8] & (0x1 << pn % 8);
                  in_byte <<= depth;
                  in_bits -= depth;
                }
                break;
            }
            if (whitish)
            {
              // keep white
            }
            else if (colored && with_color)
            {
              out_color_byte &= ~(0x80 >> col % 8); // colored
            }
            else
            {
              out_byte &= ~(0x80 >> col % 8); // black
            }
            if ((7 == col % 8) || (col == w - 1)) // write that last byte! (for w%8!=0 border)
            {
              output_row_color_buffer[out_idx] = out_color_byte;
              output_row_mono_buffer[out_idx++] = out_byte;
              out_byte = 0xFF; // white (for w%8!=0 border)
              out_color_byte = 0xFF; // white (for w%8!=0 border)
            }
          } // end pixel
          uint16_t yrow = y + (flip ? h - row - 1 : row);
          display.writeImage(output_row_mono_buffer, output_row_color_buffer, x, yrow, w, 1);
        } // end line
        Serial.print("耗时 "); Serial.print(millis() - startTime); Serial.println(" ms");
        display.refresh(); //刷新
      }
    }
  }
  file.close();
  if (!valid)
  {
    Serial.println("图像格式不能处理");
    file.close();
    display.init();
    display_partialLine(6, bmpname);
    display_bitmap_sleep("图像格式不能处理");
  }
}

uint16_t read16(File& f)
{
  // BMP数据存储在little endian中，与Arduino相同。
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB 最低有效位 最右侧
  ((uint8_t *)&result)[1] = f.read(); // MSB 最高有效位 最左侧
  return result;
}

uint32_t read32(File& f)
{
  // BMP数据存储在little endian中，与Arduino相同。
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}
