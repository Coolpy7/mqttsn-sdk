#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
SoftwareSerial mySerial(7, 8); // RX, TX 不需要转换

//byte reg = 0;//1

String pid;
String subRegAddr;
String regInfoTopic = String("$SYS/v1/http/post/m/reg");

uint16_t reginfo_msgid;
uint16_t reginfo_topicid;
uint16_t regbind_msgid;
uint16_t regbind_topicid;
uint16_t subbind_msgid;
uint16_t subbind_topicid;
void setup() {
  pid = String("6666888");
  subRegAddr = String("m/reg/") + pid;

  randomSeed(analogRead(0));
  Serial.begin(9600);
  mySerial.begin(9600);
  while (!mySerial) {
    ;
  }

  reginfo_msgid = random(1, 65535);
  regbind_msgid = random(1, 65535);
  regdv(regInfoTopic, reginfo_msgid);
}

struct Header {
  uint8_t length;
  uint8_t msgType;
};

union Flags {
  uint8_t byte;
  struct
  {
    bool dup             : 1;
    uint8_t qos          : 2;
    bool retain          : 1;
    bool will            : 1;
    bool cleanSession    : 1;
    uint8_t topicIdType  : 2;
  } bits;
};

struct Regack {
  Header header;
  uint16_t topicId;
  uint16_t msgId;
  uint8_t returnCode;
};

struct Suback {
  Header header;
  Flags flags;
  uint16_t topicId;
  uint16_t msgId;
  uint8_t returnCode;
};

struct Publish {
  Header header;
  Flags flags;
  uint16_t topicId;
  uint16_t msgId;
  uint8_t data[0];
};

void snReader(byte *sn) {
  if (sn[1] == 0x0B) {//regack
    Regack &p = *(Regack *)sn;
    if (p.msgId == reginfo_msgid && p.returnCode == 0x00) {
      reginfo_topicid = p.topicId;
      //订阅绑定设备订阅地址
      subbind_msgid = random(1, 65535);
      sub(subRegAddr, subbind_msgid);
    } 
  } else if (sn[1] == 0x13) {//suback
    Suback &p = *(Suback *)sn;
    if (subbind_msgid == p.msgId && p.returnCode == 0x00) {
      subbind_topicid = p.topicId;
      //发送待注册设备信息到服务器
      DynamicJsonBuffer jsonBuffer;
      JsonObject& root = jsonBuffer.createObject();
      root["pid"] = pid;
      root["cb"] = subRegAddr;
      root["uiid"] = 1;//界面id，当应用被发布到平台即可指定ui操作 0为禁用值
      String output;
      root.printTo(output);
      pub(output.c_str(), reginfo_topicid);
    }
  } else if (sn[1] == 0x0c) {//pub packet
    Publish &p = *(Publish *)sn;
    if (subbind_topicid == p.topicId) { //判断是否注册绑定包
      int pub_len = sn[0] - 7;
      String payload = String();
      for (int i = 0; i < pub_len; i++) {
        char c = p.data[i];
        payload += c;
      }
      DynamicJsonBuffer jsonBuffer;
      JsonObject& root = jsonBuffer.parseObject(payload.c_str());
      if (!root.success()) {
        Serial.println("parseObject() failed");
        return;
      }
      const char* Chipid = root["Pid"];
      Serial.println(Chipid);
    }
  }
}

void sub(String topic, uint16_t msgid) {
  int ptlen = 5 + topic.length();
  byte cbuf[ptlen];
  byte *p = cbuf;
  *p++ = ptlen;
  *p++ = 0x12;
  byte flags = 0;
  flags = bitWrite(flags, 7, 1);
  flags = bitWrite(flags, 6, 0);
  flags = bitWrite(flags, 5, 0);
  flags = bitWrite(flags, 4, 0);
  flags = bitWrite(flags, 3, 0);
  flags = bitWrite(flags, 2, 0);
  flags = bitWrite(flags, 1, 0);
  flags = bitWrite(flags, 0, 0);
  *p++ = flags;
  *((uint16_t *)p) = msgid; p += 2;
  memcpy(p, topic.c_str(), topic.length());
  mySerial.write(cbuf, *cbuf);
}

void pub(const char* postval, uint16_t topicid) {
  int ptlen = 7 + strlen(postval);
  byte cbuf[ptlen];
  byte *p = cbuf;
  *p++ = ptlen;
  *p++ = 0x0C;
  *p++ = 0x00;
  *((uint16_t *)p) = topicid; p += 2;
  *((uint16_t *)p) = 0x0000; p += 2;
  memcpy(p, postval, strlen(postval));
  mySerial.write(cbuf, *cbuf);
}

void regdv(String topic, uint16_t msgid) {
  int ptlen = 6 + topic.length();
  byte cbuf[ptlen];
  byte *p = cbuf;
  *p++ = ptlen;
  *p++ = 0x0A;
  *((uint16_t *)p) = 0x0000; p += 2;
  *((uint16_t *)p) = msgid; p += 2;
  memcpy(p, topic.c_str(), topic.length());
  mySerial.write(cbuf, *cbuf);
}

byte *buf;
byte *cluser = NULL , *end_point;
uint16_t len = 0;
byte buf_temp;
int is_first = 1;
int is_start = 0;

void loop() {
  int bufsize = mySerial.available();
  if (bufsize > 0) {
    for (int i = 0; i < bufsize; i++) {
      byte c = mySerial.read();
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
          snReader(buf);
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

//永久存储写
void eepWrite(int offset, String str) {
  EEPROM.write(offset, str.length());
  ++offset;
  for (int i = 0; i < str.length(); i++) {
    EEPROM.write(offset + i, (byte)str.charAt(i));
  }
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
