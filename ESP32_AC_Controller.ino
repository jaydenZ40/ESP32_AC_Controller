#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

const char* ssid     = "ZHAO-2G";
const char* password = "zhao6263806577";

const int HEATPIN = 18; //  connect to white
const int COOLPIN = 19; //  connect to yellow
const int DHTPIN = 4;

WiFiServer server(80);

#define DHTTYPE  DHT22
DHT_Unified dht(DHTPIN, DHTTYPE);
uint32_t delayMS;

float temp = 77.0f;
float humidity = 50.0f;
float targetTemp = 24.0f;
float maxDiff = 3.5f; // run AC when |temp - targetTemp| > maxDiff

bool isFirstRunning = true;
bool isCooling = false;
bool isHeating = false;

int workHour = 0;
int workMin = 0;
int minLeft = 0;

unsigned long startTime = 0;

void setup() {
  Serial.begin(115200);

  pinMode(HEATPIN, OUTPUT);
  pinMode(COOLPIN, OUTPUT);

  delay(10);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED){
    delay(500);
  }
    
  Serial.println(WiFi.localIP());
  server.begin();
  
  dht.begin();
  sensor_t sensor;
  delayMS = sensor.min_delay / 1000;
}

void loop() {
  // Get info from DHT Sensor
  delay(delayMS);
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (!isnan(event.temperature)) {
    temp = event.temperature * 1.8f + 32;
  }
  dht.humidity().getEvent(&event);
  if (!isnan(event.relative_humidity)) {
    humidity = event.relative_humidity;
  }

  // listen and get string for incoming clients
  WiFiClient client = server.available();
  if (client){
    String currentLine = "";
    while (client.connected()){
      if (client.available()) {
        char c = client.read();
        if (c == '\n'){
          if (currentLine.length() == 0){
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            client.print("<meta charset='UTF-8'>");
            client.print("<style> body {background-color: #ccffff;} </style>");
            client.print("<h3>点击<a href='/N'><span style='color: #ff0000;'> 这里</span> </a>停止所有工作</h3>");
            client.print("当前室内温度:&nbsp;");
            if (temp >= 79){
              client.print("<span style='color: #ff0000;'>");
            }
            else if (temp <= 71){
              client.print("<span style='color: #00ffff;'>");
            }
            else {
              client.print("<span style='color: #00ff00;'>");
            }
            client.print(temp);
            client.print("&deg;F (");
            client.print((temp - 32) / 1.8f);
            client.print("&deg;C)</span>, 湿度:&nbsp;");
            client.print(humidity);
            client.print("%<br><br>");

            client.println("<FORM ACTION='/A' method=get >");
            client.print("<p style='padding-left: 30px;'>输入预设温度:&nbsp;");
            client.print("<select name='TargetTemp' size='1'><option>68</option><option>69</option><option>70");
            client.print("</option><option>71</option><option>72</option><option>73</option><option>74");
            client.print("</option><option selected='selected'>75</option><option>76</option><option>77");
            client.print("</option><option>78</option></select> &deg;F</p><br>");

            client.print("<p style='padding-left: 30px;'>输入工作时长:&nbsp;");
            client.print("<select name='WorkHour' size='1'><option>0</option><option>1</option><option>2</option>");
            client.print("<option>3</option><option>4</option><option>5</option><option selected='selected'>6</option>");
            client.print("<option>7</option><option>8</option><option>9</option></select>");
            client.print(" 小时&nbsp;");
            client.print("<select name='WorkMin' size='1'><option selected='selected'>00</option><option>05</option><option>10</option>");
            client.print("<option>15</option><option>20</option><option>25</option><option>30</option><option>35</option>");
            client.print("<option>40</option><option>45</option><option>50</option><option>55</option></select>");
            client.print(" 分钟</p>");

            client.print("<p style='padding-left: 30px;'>输入启动温差():&nbsp;<INPUT TYPE=TEXT NAME='maxDiff' VALUE='4.0' SIZE='15' MAXLENGTH='3'> &deg;F<br>");
            client.println("<br><p style='padding-left: 100px;'><INPUT TYPE=SUBMIT NAME='submit' VALUE='OK'>");
            client.println("</form>");

            //  show working information
            if (isCooling){
              client.print("<br><span style='color: #00ffff;'><h3>空调已启动: 制冷中...<h3>");
              client.print("预设温度: ");
              client.print(targetTemp);
              client.print("<br>制冷剩余时间: ");
            }
            if (isHeating){
              client.print("<br><span style='color: #ff0000;'><h3>空调已启动: 加热中...<h3>");
              client.print("预设温度: ");
              client.print(targetTemp);
              client.print("<br>加热剩余时间: ");
            }
            if (isCooling || isHeating){
              client.print(minLeft / 60);
              client.print(" 小时, ");
              client.print(minLeft % 60);
              client.println("分钟");
              client.print("<br>总工作时间: ");
              client.print(workHour);
              client.print("小时, ");
              client.print(workMin);
              client.print("分钟, </span>");
            }

            // The HTTP response ends with another blank line:
            client.println();
            // break out of the while loop:
            break;
          }
          else {  // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        }
        else if (c != '\r') {
          currentLine += c;      // add it to the end of the currentLine
        }

        if (currentLine.endsWith("GET /N")) { //  close everything
          CloseAll();
        }
        else if (currentLine.endsWith("submit=OK")){
          CloseAll();
          delay(100);
          RunAC(currentLine);
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("Client Disconnected.");
  }

  CoolingUpdate();
  HeatingUpdate();
}

void CloseAll(){
  isFirstRunning = true;
  isCooling = false;
  isHeating = false;

  workHour = 0;
  workMin = 0;
  minLeft = 0;

  digitalWrite(COOLPIN, LOW);
  digitalWrite(HEATPIN, LOW);
}

void RunAC(const String currentLine){ // sample string: http://192.168.1.169/A?TargetTemp=75&WorkHour=6&WorkMin=00&coolPeriod=2.0&submit=OK
  int index = currentLine.indexOf("Temp") + 5;
  targetTemp = (currentLine[index] - '0') * 10 + (currentLine[index + 1] - '0') * 1;
  //Serial.print("target Temperature: ");
  //Serial.println(targetTemp);

  index = currentLine.indexOf("Hour") + 5;
  workHour = (currentLine[index] - '0');

  index = currentLine.indexOf("Min") + 4;
  workMin = (currentLine[index] - '0') * 10 + (currentLine[index + 1] - '0') * 1;

  minLeft = workHour * 60 + workMin;
  //Serial.print("Remain working time in minutes: ");
  //Serial.println(minLeft);

  index = currentLine.indexOf("maxDiff") + 8;
  maxDiff = (currentLine[index]- '0') ;

  if (currentLine[index + 2] > '0' && currentLine[index + 2] < '9'){
    maxDiff += (currentLine[index + 2] - '0') * 0.1f;
  }
  //Serial.print("maxDiff: ");
  //Serial.println(maxDiff);

  if (targetTemp > temp){
    isHeating = true;
  }
  else if (targetTemp < temp){
    isCooling = true;
  }
  startTime = millis();
}

void CoolingUpdate(){
  if (isCooling){
    if (targetTemp >= temp){  // reach the desire temperature
      digitalWrite(COOLPIN, LOW);
      //Serial.println("Cooling Off");
    }
    else if (temp - targetTemp > maxDiff || isFirstRunning){
    digitalWrite(COOLPIN, HIGH);
    //Serial.println("Cooling On");
    isFirstRunning = false;
    }
    else {
      TimeUpdate();
    }
  }
}

void HeatingUpdate(){
  if (!isHeating){ 
    if (targetTemp <= temp){  // reach the desire temperature
      digitalWrite(HEATPIN, LOW);
      //Serial.println("Heating Off");
    }
    else if (targetTemp - temp > maxDiff || isFirstRunning){
    digitalWrite(HEATPIN, HIGH);
    //Serial.println("Heating On");
    isFirstRunning = false;
    }
    else {
      TimeUpdate();
    }
  }
}

void TimeUpdate(){
  if (minLeft <= 0){
    CloseAll();
  }

  if (millis() - startTime >= 60000){    
     minLeft--;
     startTime = millis();
  }
  if (millis() - startTime < 0){  //  ignore the tiny error within a minute when millis overflows
    startTime = millis();
  }
}
