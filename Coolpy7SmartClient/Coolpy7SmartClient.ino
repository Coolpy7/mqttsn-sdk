#include <ArduinoJson.h>
#include <Ticker.h>
#include <Esp.h>
#include <IPAddress.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>

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

//MQTT
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[128];

//UDP
WiFiUDP Udp;
unsigned int localUdpPort = 13623;  // local port to listen on
char incomingPacket[60];  // buffer for incoming packets

String pid;
String subRegAddr;
bool isdone;

void setup() {
  isdone = false;
  pid = String(ESP.getChipId());
  subRegAddr = String("m/reg/") + pid;

  pinMode(buttonPin, INPUT);
  pinMode(ledPin, OUTPUT);
  EEPROM.begin(512);

  Serial.begin(9600);

  //读取到初始化信息启动
  if (EEPROM.read(wifioffset) == 0) {
    smartConfig();
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
    //发现Coolpy7服务器
    Udp.begin(localUdpPort);
    Serial.printf("IP %s, UDP port %d\n", WiFi.localIP().toString().c_str(), localUdpPort);
  } else {
    String ipStr = eepRead(SvIpoffset);
    byte ip[4];
    parseBytes(ipStr.c_str(), '.', ip, 4, 10);
    IPAddress svip = IPAddress(ip[0], ip[1], ip[2], ip[3]);
    int port = eepRead(SvTcpoffset).toInt();
    //Serial.println(svip.toString().c_str());
    //Serial.println(port,DEC);
    //初始化MQTT客户端
    client.setServer(svip, port);
    client.setCallback(callback);
  }
}


void smartConfig()
{
  WiFi.mode(WIFI_STA);
  Serial.println("\r\nCoolpy7 Smartconfig Start");
  WiFi.beginSmartConfig();
  while (1)
  {
    Serial.print(".");
    delay(500);
    if (WiFi.smartConfigDone())
    {
      Serial.println("SmartConfig Success");
      //记录新ssid及密码
      EEPROM.write(wifioffset, 1);
      EEPROM.commit();
      eepWrite(ssidoffset, WiFi.SSID().c_str());
      eepWrite(pwdoffset, WiFi.psk().c_str());
      break;
    }
  }
}

void loop() {
  //等待cp7服务器广播服务
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
    if (!client.connected()) {
      reconnect();
    } else {
      client.loop();
      delay(2);
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.println("MQTT connection...");
    if (EEPROM.read(reg) == 0) {
      if (client.connect(pid.c_str(), pid.c_str(), "Coolpy6@2017")) {
        //发送待注册设备信息到服务器
        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        //root["mac"] = WiFi.macAddress();
        root["pid"] = pid;
        //root["dtype"] = 1;//1开关，2可调值控制器，3自定义指令控制器，4数值传感器，5GPS传感器，6图片传感器，7自定义数据传感器
        root["cb"] = subRegAddr;
        root["uiid"] = 1;//界面id，当应用被发布到平台即可指定ui操作 0为禁用值
        String output;
        root.printTo(output);
        client.publish("$SYS/v1/http/post/m/reg", output.c_str());
        client.subscribe(subRegAddr.c_str(), 0);
        Serial.println("reg on");
      }
    } else {
      //      if (client.connect("admin", "admin", "admin")) {
      //        digitalWrite(ledPin, HIGH);
      //        Serial.println("connected");
      //
      //        client.publish("outTopic", "hello world");
      //        client.subscribe("inTopic");
      //      }
    }
    if (!client.connected()) {
      digitalWrite(ledPin, !digitalRead(ledPin));
      delay(3000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) == subRegAddr) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payload);
    if (!root.success()) {
      Serial.println("parseObject() failed");
      return;
    }
    const char* Chipid = root["Pid"];
    Serial.println(Chipid);
  }
  //if (payload[0] == (byte)'y') {
  //绑定设备激活成功
  // EEPROM.write(reg, 1);
  // EEPROM.commit();
  // restart();
  // }
  //}
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
