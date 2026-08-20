#include <WiFi.h>
#include <WebServer.h>

// L298N引脚定义
#define IN1 20
#define IN2 21
#define ENA 18

// AP热点配置（手机搜这个WiFi）
const char* ap_ssid = "ESP32_Motor_Car";
const char* ap_password = "12345678";

WebServer server(80);

// 网页前端界面
const char* html_page = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>电机控制器</title>
<style>
body{font-size:30px;text-align:center;margin-top:80px;background:#111;color:#fff}
button{padding:25px 60px;margin:15px;font-size:28px;border:none;border-radius:12px;}
#btn_f{background:#28a745;color:white;}
#btn_r{background:#007bff;color:white;}
#btn_s{background:#dc3545;color:white;}
</style>
</head>
<body>
<h2>ESP32 电机控制面板</h2>
<br>
<button id="btn_f" onclick="fetch('/forward')">▶ 正转</button>
<br>
<button id="btn_r" onclick="fetch('/reverse')">◀ 反转</button>
<br>
<button id="btn_s" onclick="fetch('/stop')">■ 刹车停机</button>
</body>
</html>
)HTML";

// 正转
void handle_forward(){
  digitalWrite(IN1,HIGH);
  digitalWrite(IN2,LOW);
  analogWrite(ENA,160); //PWM速度 0‑255
  server.send(200,"text/plain","forward ok");
}

//反转
void handle_reverse(){
  digitalWrite(IN1,LOW);
  digitalWrite(IN2,HIGH);
  analogWrite(ENA,160);
  server.send(200,"text/plain","reverse ok");
}

//刹车停机
void handle_stop(){
  digitalWrite(IN1,LOW);
  digitalWrite(IN2,LOW);
  server.send(200,"text/plain","stop ok");
}

//主页
void handle_root(){
  server.send(200,"text/html",html_page);
}

void setup() {
  Serial.begin(115200);
  pinMode(IN1,OUTPUT);
  pinMode(IN2,OUTPUT);
  pinMode(ENA,OUTPUT);
  //上电默认停机
  digitalWrite(IN1,LOW);
  digitalWrite(IN2,LOW);

  //开启AP热点模式
  WiFi.softAP(ap_ssid,ap_password);
  IPAddress ap_ip = WiFi.softAPIP();
  Serial.print("ESP32热点IP地址：");
  Serial.println(ap_ip); //固定是 192.168.4.1

  //网页路由绑定
  server.on("/",handle_root);
  server.on("/forward",handle_forward);
  server.on("/reverse",handle_reverse);
  server.on("/stop",handle_stop);
  server.begin();
}

void loop() {
  server.handleClient();
}
