#include <ArduinoJson.h>
#include <Ticker.h>
#include <Esp.h>
#include <IPAddress.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266Client.h>

String pid;
// create a UDP client
ESP8266Client udpclient("192.168.1.1", 1884, UDP);//这是假的初始化信息，真实ip及端口会自动取得
void onDataCb(ESP8266Client& ct, char *data, unsigned short length);
void onReconnectCb(ESP8266Client& client, sint8 err);
byte *buf;
byte *cluser = NULL , *end_point;
uint16 len = 0;
byte buf_temp;
int is_first = 1;
int is_start = 0;

//时钟
Ticker flipper;
int resetCounter = 0;

const int  buttonPin = D3;
//闪灯提示 1.系统正常情况下常关 2.smartconfig进行中常开
//3.smartconfig完成后常关 4.连接主wifi时1秒闪烁
//5.闪烁30秒后转为常开 6.5秒开5秒关为连接MQTT服务中
const int  ledPin = D0;
//初始化永久存储起始位
byte wifioffset = 0;//1
byte ssidoffset = 1;//30
byte pwdoffset = 31;//30
byte hasSvoffset = 61;//1
byte SvIpoffset = 62;//15
byte SvTcpoffset = 78;//5
byte SvTlsoffset = 83;//5
byte SvAgoffset = 88;//5
byte reg = 93;//1

//UDP
WiFiUDP Udp;
unsigned int localUdpPort = 13623;  // local port to listen on
char incomingPacket[60];  // buffer for incoming packets

void setup() {
  pid = String(ESP.getChipId());
  
  pinMode(buttonPin, INPUT);
  pinMode(ledPin, OUTPUT);
  EEPROM.begin(512);

  Serial.begin(9600);
  WiFi.mode(WIFI_STA);
  Serial.println();

  //读取到初始化信息启动
  if (EEPROM.read(wifioffset) == 0) {
    //启动esptouch及airkiss功能
    Serial.println("SmartConfig");
    WiFi.beginSmartConfig();
    //阻塞等待smartconfig完成
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
      if (WiFi.smartConfigDone()) {
        Serial.println("completed");
        WiFi.stopSmartConfig();
        //记录新ssid及密码
        EEPROM.write(wifioffset, 1);
        EEPROM.commit();
        eepWrite(ssidoffset, WiFi.SSID().c_str());
        eepWrite(pwdoffset, WiFi.psk().c_str());
        //启动系统
        restart();
      }
    }
  } else {
    //使用永久存储中的主路由信号
    Serial.println(String("SSID:") + eepRead(ssidoffset).c_str());
  }

  //时钟频率1秒
  flipper.attach(1, flip);
  Serial.print("Connecting");
  //开始连接主路由
  WiFi.begin(eepRead(ssidoffset).c_str(), eepRead(pwdoffset).c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    //闪烁led
    digitalWrite(ledPin, !digitalRead(ledPin));
  }
  //正常连接后进入发现服务端所在位置
  //Serial.println(WiFi.subnetMask());
  if (EEPROM.read(hasSvoffset) == 0) {
    //发现Coolpy6服务器
    Udp.begin(localUdpPort);
    Serial.printf("IP %s, UDP port %d\n", WiFi.localIP().toString().c_str(), localUdpPort);
  } else {
    String ipStr = eepRead(SvIpoffset);
    byte ip[4];
    parseBytes(ipStr.c_str(), '.', ip, 4, 10);
    IPAddress svip = IPAddress(ip[0], ip[1], ip[2], ip[3]);
    int port = eepRead(SvAgoffset).toInt();
    //Serial.println(svip.toString().c_str());
    //Serial.println(port,DEC);
    //初始化MQTT客户端
    udpclient.setAddress(svip);
    udpclient.setPort(port);
    // set callback functions
    udpclient.onData(onDataCb);
    udpclient.onReconnect(onReconnectCb);
  }
}

void loop() {
  //等待cp6服务器广播服务
  if (EEPROM.read(hasSvoffset) == 0) {
    int packetSize = Udp.parsePacket();
    if (packetSize)
    {
      //接收广播消息发现服务
      int len = Udp.read(incomingPacket, 60);
      if (len > 0)
      {
        incomingPacket[len] = 0;
      }
      IPAddress rip = Udp.remoteIP();
      DynamicJsonBuffer jsonBuffer;
      JsonObject& root = jsonBuffer.parseObject(incomingPacket);
      if (!root.success()) {
        Serial.println("parseObject() failed");
        return;
      }
      eepWrite(SvIpoffset, rip.toString());
      const char* tcp = root["tcp"];
      eepWrite(SvTcpoffset, String(tcp));
      const char* tls = root["tls"];
      eepWrite(SvTlsoffset, String(tls));
      const char* ag = root["ag"];
      eepWrite(SvAgoffset, String(ag));
      EEPROM.write(hasSvoffset, 1);
      EEPROM.commit();
      const char* v = root["v"];
      Serial.printf("ip: %s v: %s\n", rip.toString().c_str(), v);
      delay(1000);
      //启动系统
      restart();
    }
  }
  //MQTT自动重连服务器
  if (EEPROM.read(hasSvoffset) == 1) {
    if (!udpclient.isConnected()) {
      if (udpclient.connect()) {
        //注册到sn网关，此步完成即下属所有子网节点直接开始注册、pub、sub操作即可
        int client_id_len = pid.length();
        int ptlen = client_id_len + 6;
        byte cbuf[ptlen];
        byte *p = cbuf;
        *p++ = ptlen;
        *p++ = 0x04;
        *p++ = 0x0c;
        *p++ = 0x01;
        *p++ = 0x00;
        *p++ = 0x1e; 
        memcpy(p, pid.c_str(), client_id_len);
        sint8 res = udpclient.send(cbuf, *cbuf);
        if (res != ESPCONN_OK) {
          Serial.print("error sending: ");
          Serial.println(res);
        }else{
          Serial.println("ok");
        }
      }
    }

    int bufsize = Serial.available();
    if (bufsize > 0) {
      for (int i = 0; i < bufsize; i++) {
        byte c = Serial.read();
        if (cluser == NULL) {
          if (len == 0) {
            is_start = 0;
            is_first = 1;
            len = c;
          }
          if (len == 0x01) {
            if (is_start) {
              if (is_first) {
                buf_temp = c;
                is_first = 0;
              } else {
                len = buf_temp;
                len <<= 8;
                len += c;
                cluser = buf = (byte*) malloc(sizeof(byte) * len);
                end_point = buf + len;
                *cluser++ = 0x01;
                *cluser++ = buf_temp;
                *cluser++ = c;
              }
            }
          } else {
            cluser = buf = (byte*) malloc(sizeof(byte) * len);
            end_point = buf + len;
            *cluser++ = c;
          }
        } else {
          *cluser++ = c;
          if (cluser == end_point) {
            if (udpclient.isConnected()) {
              sint8 res = udpclient.send(buf, end_point - buf);
              if (res != ESPCONN_OK) {
                Serial.print("error sending: ");
                Serial.println(res);
              }
            } else {
              Serial.println("udp error");
            }
            free(buf);
            len = 0;
            cluser = NULL;
          }
        }
        is_start = 1;
      }
    }
    delay(1);
  }
}

void onDataCb(ESP8266Client& ct, char *data, unsigned short length)
{
  Serial.write(data, length);
}

void onReconnectCb(ESP8266Client& client, sint8 err)
{
  Serial.print("reconnect CB: ");
  Serial.println(espErrorToStr(err));
  Serial.println(espErrorDesc(err));
}

void flip()
{
  //按住Flash按扭5秒后重置系统
  if (digitalRead(buttonPin) == LOW) {
    ++resetCounter;
    Serial.println(resetCounter);
  } else {
    if (resetCounter != 0) {
      resetCounter = 0;
    }
  }
  if (resetCounter == 5)
  {
    digitalWrite(ledPin, HIGH);
    for (int i = 0; i < 512; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.end();
    restart();
  }
}

void restart() {
  WiFi.disconnect();
  ESP.restart();
}

//永久存储写
void eepWrite(int offset, String str) {
  EEPROM.write(offset, str.length());
  ++offset;
  for (int i = 0; i < str.length(); i++) {
    EEPROM.write(offset + i, (byte)str.charAt(i));
  }
  EEPROM.commit();
}

//永久存储读
String eepRead(int offset) {
  String str = String();
  int len = EEPROM.read(offset);
  ++offset;
  for (int i = 0; i < len; i++) {
    str += (char)EEPROM.read(offset + i);
  }
  return str;
}

//ip地址转换器
//const char* ipStr = "50.100.150.200";
//byte ip[4];
//parseBytes(ipStr, '.', ip, 4, 10);
//mac
//const char* macStr = "90-A2-AF-DA-14-11";
//byte mac[6];
//parseBytes(macStr, '-', mac, 6, 16);
void parseBytes(const char* str, char sep, byte* bytes, int maxBytes, int base) {
  for (int i = 0; i < maxBytes; i++) {
    bytes[i] = strtoul(str, NULL, base);
    str = strchr(str, sep);
    if (str == NULL || *str == '\0') {
      break;
    }
    str++;
  }
}

