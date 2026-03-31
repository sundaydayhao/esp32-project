String sendHTML_main() //文件固件丢失提示界面，备用页面#include <u8g2_fonts.h>
{
  String webpage_html;
  webpage_html += F("<!DOCTYPE html>");
  webpage_html += F("<html lang='zh-CN'>");
  webpage_html += F("<head>");
  webpage_html += F("<title>天气墨水屏</title>");
  webpage_html += F("<meta charset='utf-8'>");
  webpage_html += F("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  webpage_html += F("</head>");
  webpage_html += F("<body>");
  webpage_html += F("<h3>糟糕！无法找到该页面，可能原因如下：</h3>");
  webpage_html += F("<p>1.文件系统未配置或网页丢失<br>2.文件系统不兼容，应为LittleFS<br>3.没有该页面<br>4.文件系统与程序固件不匹配<p><br>");
  webpage_html += F("<p>如需上传固件，请确保8266和设备在同一WIFI网络下！</p>");
  webpage_html += F("<p>注意电量，以免在上传过程丢失固件！</p>");
  webpage_html += "<p>需要版本：" + version + "-" + version1 + "</p>";
  webpage_html += "当前电压：" + String(getBatVolNew()) + " V<br>";
  webpage_html += "<p>http://" + WiFi.localIP().toString() + "/update</p>";
  webpage_html += "<a href=\"http://" + WiFi.localIP().toString() + "/update\">";
  webpage_html += F("<button type='button'>点击进入更新页面</button>");
  webpage_html += F("</a></body></html>");

  return webpage_html;
}
