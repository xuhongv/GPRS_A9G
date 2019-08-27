[安信可GPRS模块A9g学习之路](https://github.com/xuhongv/GPRS_A9G)
=====



这个是我安信可GPRS模组片上(SoC)开发SDK C语言版的一些学习历程，A9G模块内核是基于RDA8955芯片的。


## (一) 硬件

### 1. A9: GPRS 模块

![](./doc/assets/A9.png)</br>

##### 特征

  * 32位内核，主频高达312MHz，4k指令缓存，4k数据缓存
  * 多达29个GPIO（两个GPIO作为下载口）
  * 实时时钟、闹钟
  * 1个USB1.1接口
  * 2个带流控的UART接口（+1个下载调试串口）
  * 2个SPI接口
  * 3个I<sup>2</sup>C接口
  * 1个SDMMC控制器（接口）
  * 2个10位ADC接口
  * 32Mb(4MB) SPI NOR Flash
  * 32Mb(4MB) DDR PSRAM
  * 8kHz、13Bits/sample ADC mic
  * 48kHz、16bits/sample DAC Audio
  * 电源管理单元：锂电池充电管理、集成DC-DC及LDOs、可变化的IO电压
  * 18.8 x 19.2 mm SMD封装
  * 四频GSM/GPRS（800/900/1800/1900MHz)
  * 语音通话
  * 短信服务

### 2. A9G: GPRS+GPS+BDS模块
 
![](./doc/assets/A9G.png)</br>

##### 特征

  * A9所有特征
  * 集成GPS+BDS(内部和GPRS串口2连接)

### 3. A9/A9G GPRS(+GPS+BDS) 开发板

![](./doc/assets/A9G_dev.png)</br>

A9/A9G开发板，方便开发和调试


##### pudding开发板引脚图

![](./doc/assets/pudding_pin.png)</br>

>  RDA8955芯片或者其相关模块理论上也可使用本SDK


## (二) SDK


#### 获得SDK


##### 1. 下载代码

```
git clone https://github.com/Ai-Thinker-Open/GPRS_C_SDK.git --recursive
```
---
##### 2. 检查代码完整性

下载完后请检查目录`platform/csdk`目录写是否包含`debug`、`release`目录。
如果没有，则是下载方式错误，请仔细阅读第一步下载正确的文件


## (官网文档) 开发文档


**文档地址： [GPRS C SDK 在线文档](https://ai-thinker-open.github.io/GPRS_C_SDK_DOC/zh/)**


