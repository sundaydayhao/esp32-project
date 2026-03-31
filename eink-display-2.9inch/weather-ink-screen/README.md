## 已停维护
## 重要修复：
### 持续添加更多功能 [跳转下载页](https://gitee.com/Lichengjiez/weather-ink-screen/releases)
### 硬件V2.3X需要将SD的CS的引脚独立出来，需要将按键3的引脚更改到GPIO3(即8266的RX引脚，不影响烧录)
### 最新版硬件为V2.41已经修复SD卡容易掉的问题，还掉就是卡不兼容了
### [方法：将按键串联的10K电阻改为10R-15R，并接到RX引脚上](https://gitee.com/Lichengjiez/weather-ink-screen/wikis/%E6%9B%B4%E6%94%B9%E6%8C%89%E9%94%AE3%E5%BC%95%E8%84%9A%E6%8C%87%E5%8D%97?sort_id=5424954)
### 有问题先看[Wiki](https://gitee.com/Lichengjiez/weather-ink-screen/wikis/%E7%83%A7%E5%BD%95%E8%AF%B4%E6%98%8E)
### 新版固件不再合并文件系统，需要手动烧录，地址为0X200000
#### ******************************************************* 
#### 请勿用于大规模商业用途，禁止售卖资料！
#### TXT版本源码已放出，代码很乱，勿吐槽！
#### 开源版本已添加自动识别时钟芯片、去除扫描、时钟跳过等一部分最新的功能
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/%E9%80%8F%E6%98%8E%E6%8E%A2%E7%B4%A2%E7%89%882.jpg)
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/%E9%80%8F%E6%98%8E%E6%8E%A2%E7%B4%A2%E7%89%88.jpg)
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/V12-4.jpg) 
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/V12-0.jpg) 
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/V12-2.jpg) 
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/qzg3.jpg) 
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/013-09.jpg) 

## 最新留言
* 新增可读取SD卡内的TXT文件，txt文件管理器
* 最新开源PCB项目链接[https://oshwhub.com/jie326513988/SDka-mo-shui-ping-yue-du-qi](https://oshwhub.com/jie326513988/SDka-mo-shui-ping-yue-du-qi)
* 新版本对SD卡兼容性较差，请到推荐的店铺购买SD卡，并用指定软件格式化

### 最新版PCB介绍V2.32
* 测试可用的屏幕为A01（排线标A01），T5D/T5（排线标Z10）
* 前置光LED恒流驱动、温湿度芯片、时钟芯片、1000万寿命按键，功能无阉割
* V2.32比V2.31多了一颗滤波电容，在前置光LED驱动电路处，以解决LED频闪的问题。
* 休眠电流0.026-0.04ma，WIFI工作电流120-70ma，WIFI不工作电流25-17ma，瞬态电流500ma
* 修复原装RX8025T无法读取问题，现还支持BL8025T、拆机RX8025T
* 温湿度芯片可以用进口SHT30、国产GXHT30（对湿度感应较为敏感）
* 无时钟芯片、无温湿度芯片也可使用
* ESD芯片-SMS05C
    * SRV05-4为双向不适合此电路，其他双向ESD芯片也不行
    * 因为双向ESD芯片在不插USB时会有电漏到ch340的开关MOS，导致ch340工作浪费电
* 性能更佳封装更小的LDO-ME6210A33M3G
* 16MB FLASH替换型号为25Q128JVSQ(16M)
* 加长焊盘，物件间距合理，不再手残
* 必须插电池并打开拨动开关进行烧录程序，不支持USB&锂电池切换电源
* 硬件参考：
    * [DUCK硬件](http://oshwhub.com/duck/esp8266-weather-station-epaper)
    * [半糖硬件](https://oshwhub.com/HalfSweet/29-EPaper-Thermo-hygrometer) 

### PCBV2.32特殊物料
* [锂电池303450/500mah](https://item.taobao.com/item.htm?spm=a1z09.2.0.0.145f2e8dJ0uiJd&id=528235852444&_u=rmddnvb1cc7)
* [2颗自攻螺丝1.2*5](https://detail.tmall.com/item.htm?id=535439814796&spm=a1z09.2.0.0.145f2e8dJ0uiJd&_u=rmddnvbe5fd)
* [1个凯华鼠标静音按键（6X6X4.3）](https://item.taobao.com/item.htm?spm=a1z09.2.0.0.6c722e8dqXyF11&id=651521806450&_u=rmddnvba1e5)
* [2.9寸前置光墨水屏,已停产](https://item.taobao.com/item.htm?spm=a1z09.2.0.0.7f4c2e8dqZeRtS&id=661665269358&_u=rmddnvb56de)
* [2G Micro SD卡](https://detail.tmall.com/item.htm?id=618912179633&spm=a1z09.2.0.0.1b272e8d02UaaJ&_u=emddnvb59fd&sku_properties=5919063:6536025)

### 按键操作逻辑
* 正视图从左到右依次为 按键1 按键2 按键3 (按键3-GPIO5 按键2-GPIO0)
* 按住按键3不放，再按复位按键（按键1），即可进入模式选择界面
* 所有界面的按键操作逻辑为：
    * a.单独短按为切换选项
    * b.长按按键3为确认操作或调出菜单（原组合按键取消）
* ~~按键2不可按得太频繁，不能在屏幕刷新的时候按，会导致屏幕死机，原因是按键2与屏幕刷新共用一个io口~~ （新版本已修复）

### 注意事项，请耐心看完
* 仅支持2.9寸墨水屏
* 使用Arduino开发，使用到的库GxEPD2、U8g2_for_Adafruit_GFX、NTPClient、ArduinoJson、ESP_EEPROM
* 使用心知天气个人免费版KEY（20次/分钟），需要自己去申请，然后在配网界面输入即可
* 提供适合3D打印的外壳文件
* 旧版本串口不正常解决方案
    * 磁珠改为0R电阻或直连
    * ESD芯片换成SMS05C等其他单向ESD芯片或者不焊
    * 确保PCB要求的100uf钽电容有焊接上
* 原版U8g2_for_Adafruit_GFX库无法使用大字库，故更改了库，自行到码云或群里下载
* 其他库均可在库管理器下载的到
* 无法连接wifi可能是被路由器拉黑或网络差，天线附近需要净空不能有飞线，电池挡住天线也可能会有影响
* 无法获取天气信息请检查城市名是否填对，免费用户只能查看到地级市
* 误低压休眠的请检查电池测量电路是否正常，电池电压是否大于3.25V（搭板的玩家自己给A0加上分压电路接上5V，分压后不能超过1V，否则烧ADC）
* 如无法连接8266的热点或无法打开配网页，请检查手机是否开启了智能选网模式
* ~~电池可以用：303450-500mah（适配本项目隐藏电池的背透外壳），602650-650mah（适配本项目外壳），601550-450mah（适配本项目的背透外壳），902030-500mah（DUCK&半糖的外壳）~~（旧版本）
* [GxEPD2](https://github.com/ZinggJM/GxEPD2)，[该库适配大多数大连佳显屏幕](https://goodlcd.taobao.com/shop/view_shop.htm?spm=a230r.1.14.39.7a293567D3cz3Y&user_number_id=151859855)
* [U8g2_for_Adafruit_GFX](https://github.com/olikraus/U8g2_for_Adafruit_GFX)
* [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
* [NTPClient](https://github.com/arduino-libraries/NTPClient)
* [ESP_EEPROM](https://github.com/jwrw/ESP_EEPROM)
* [ClosedCube_SHT31D](https://github.com/closedcube/ClosedCube_SHT31D_Arduino)

### 已知BUG
* ~~开机载入数据有小几率会重启系统，EXCCAUSE Code(3),加载或存储期间的处理器内部物理地址或数据错误？~~（很久未出现了）
* ~~文件管理器有几率抽风，重启系统即可~~  （已修复）
* ~~无法建立索引检查TXT文本结尾不能有换行~~ （已修复）
* ~~16MB的固件最大只支持9.99MB的TXT文本~~ （已修复）
* 在配网页面连接无效的的WIFI会卡一段时间，有相应提示。可能是硬件问题，无法同时进行STA和AP的收发？等待提示连接失败即可操作其他。
* 进入配网模式有几率重启，多试几次就好。
* 插上SD卡休眠功耗稍微多一点点，2G-1ma 32G-14ma
* 对不同牌子的SD卡兼容性较差
* ~~因有些模式需要先扫描再连接，所以会导致隐藏的WIFI无法连接，下一版本移除扫描~~（已修复）
* ~~硬件时钟的手动补偿算法好像不失效~~（已修复）
* 未插SD卡并开启SD功能时，有极小概率会影响屏幕刷新，现象为天气模式和时钟模式会有奇怪的横线。

### 功能简介
* 天气模式
    * 天气实况、未来2天天气
    * 紫外线强度、室外环境湿度、风力等级
    * 中间显示一句话，网络获取或自定义
    * 电量显示，电压或百分比
    * 室内温湿度显示（需硬件支持sht30芯片）
    * 自定义夜间不更新/更新
* 阅读模式
    * TXT文件管理器，可读取SD卡内的文件（需要先到配网模式启用SD卡功能）
    * 支持打开文件夹，最多10层
    * 支持100MB以内的文件
    * 支持横竖切换，但需要重建索引
    * 自带可用空间2.5MB，可扩展2GBMicroSD卡
    * 使用索引方式，准确计算页数，可任意跳转页（缺点首次打开需要花时间建立索引）
    * 记忆功能，自动恢复上一次看的书籍和页数
    * 电量显示
    * 时间显示，有误时钟芯片都可
* 时钟模式
    * 超大数字显示，不再眼瞎
    * 支持RX8025T/BL8025T时钟芯片
    * 自定义显示模式，日期/简洁
    * 自定义校准间隔
    * 自定义是否开启强制校准
    * 自定义离线校准补偿
* 配网模式
    * 自动、手动选择配置网络
    * 配置天气KEY和城市
    * 上传文件
    * 文件管理器
    * 设置自定义图片
    * 丰富的设置项
    * 屏幕实时消息回传提示
    * 预留OTA接口（暂时无用）
* 自定义图片模式
    * 显示自定义的bmp图片，需到配网-文件管理器启用
    * 随机播放
* 天数倒计时
* B站粉丝显示
* 所有模式下低电量会提示并永久休眠，小于等于3.3V

![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/%E8%B6%85%E8%AF%A6%E7%BB%868266%E7%83%A7%E5%BD%95%E6%95%99%E7%A8%8B.jpg)
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/peizhi.png)
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/16mbpeizhi.png)
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/wenjianguanliqi.jpeg) 
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/pw%20(1).jpg)
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/pw%20(2).jpg)
![](https://gitee.com/Lichengjiez/weather-ink-screen/raw/master1/%E5%9B%BE%E7%89%87/dianliu.jpg) 