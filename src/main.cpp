#include <Arduino.h>
#include <WiFi.h>
#include <ESP8266FtpServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <ElegantOTA.h>

#define CPU0  0
#define CPU1  1

#define T_WIFIConn_CPU      CPU1
#define T_WIFIConn_PRIOR    0
#define T_WIFIConn_STACK    2048

#define T_WEBServer_CPU     CPU1
#define T_WEBServer_PRIOR   0
#define T_WEBServer_STACK   16384

#define T_FTP_CPU           CPU1
#define T_FTP_PRIOR         0
#define T_FTP_STACK         8192

#define T_PZEM_CPU          CPU0
#define T_PZEM_PRIOR        1
#define T_PZEM_STACK        4096

#define AP_SSID             "PMeter"
#define AP_PASS             "0123456789"
#define FTP_USER            "esp32"
#define FTP_PASS            "esp32"

TaskHandle_t th_WiFiConn, th_WEBServer, th_TFTServer, th_PZEMPoll;
QueueHandle_t qh_PZEMData, qh_ResetPZEMEnergy;

#define swRxD 23
#define swTxD 18
SoftwareSerial swSerial(swRxD, swTxD);

#define PARAM_ADDR  (0)
struct Param {
  uint64_t FuseMac;         
  char ssid[20];
  char pass[20];    
} Param;  

typedef struct {
  uint32_t iVoltage;
  uint32_t iCurrent;
  uint32_t iPower;
  uint32_t iEnergy;
  uint32_t iFrequency;
  uint32_t iPowerFactor;
  uint32_t iVoltageMin;
  uint32_t iVoltageMax;
} PZEMData;

WebServer server;
const char cch_web_user[] = "admin";
const char cch_web_pass[] = "admin";
bool b_PZEMRst = false;
bool b_PZEMEmul = false;

// Заголовки функций
/* Задачи */
void task_WiFiConn(void *param);
void task_WEBServer(void *param);
void task_FTPSrv(void *param);
void task_PZEMPoll(void *param);

/* Обработчики WEB-запросов */
void h_Website();    
void h_wifi_param();
void h_pushButt();  
void h_WebRequests();       
void h_XML();

/* Прочие функции */
void print_task_header(String taskName);
void print_task_state(String taskName, int state);
void save_param();
void eeprom_init();
void ap_config();

uint16_t GetCRC16(uint8_t* bufData, uint16_t sizeData);
bool pzem_poll(PZEMData* pd);
void pzem_send(uint8_t* txData, uint8_t sizeData);
bool pzem_receive(uint8_t* rxData, uint8_t sizeData);
void pzem_reset_energy();
void print_pzemData(PZEMData* pd);

void setup() {
  Serial.begin(115200);
  Serial.println(String("CPU frequncy: ") + ets_get_cpu_frequency());
  Serial.println();
  qh_PZEMData = xQueueCreate(1, sizeof(PZEMData));
  qh_ResetPZEMEnergy = xQueueCreate(1, sizeof(b_PZEMRst));
  xTaskCreatePinnedToCore(task_WiFiConn, "WiFi Connect", T_WIFIConn_STACK, NULL, T_WIFIConn_PRIOR, &th_WiFiConn, T_WIFIConn_CPU);
}

void loop() {
  vTaskDelete(NULL);
}

void task_WiFiConn(void *param) {
  print_task_header("Wi-Fi Connect");
  uint8_t WiFiConnTimeOut = 50;
  Serial.println();

  int eSize = sizeof(Param);
  EEPROM.begin(eSize);
  EEPROM.get(PARAM_ADDR, Param);
  if(Param.FuseMac != ESP.getEfuseMac()) {
    eeprom_init();
    ap_config();
  }
  else {
    Serial.println(F("Wi-Fi STA mode"));
    WiFi.mode(WIFI_STA);
    Serial.print(F("MAC address: ")); Serial.println(WiFi.macAddress());
    Serial.print(F("STA SSID: ")); Serial.println(Param.ssid);
    Serial.print(F("STA PASS: ")); Serial.println(Param.pass);
    WiFi.begin(Param.ssid, Param.pass);
    while (WiFi.status() != WL_CONNECTED) {      
      delay(190);
      Serial.print(".");
      if(--WiFiConnTimeOut == 0) break;
    }
    if(WiFiConnTimeOut == 0) {
      Serial.println("");
      Serial.println(F("Wi-Fi connected timeout"));
      ap_config();
    }
    else {
      Serial.println("");
      Serial.print(F("STA connected to: ")); 
      Serial.println(WiFi.SSID());
      IPAddress myIP = WiFi.localIP();
      Serial.print(F("Local IP address: ")); Serial.println(myIP);
      WiFi.enableAP(false);
    }
  }  

  if(SPIFFS.begin(true)) {
    Serial.println(F("SPIFFS opened!"));
    float spiffsUsedP = SPIFFS.usedBytes() / (SPIFFS.totalBytes() / 100.0);
    Serial.print(F("SPIFFS total bytes: ")); Serial.println(SPIFFS.totalBytes());
    Serial.print(F("SPIFFS used bytes: ")); Serial.print(SPIFFS.usedBytes()); Serial.print(F("(")); 
    Serial.print(spiffsUsedP); Serial.println(F("%)"));
  }
  
  xTaskCreatePinnedToCore(task_WEBServer, "WEB Server", T_WEBServer_STACK, NULL, T_WEBServer_PRIOR, &th_WEBServer, T_WEBServer_CPU);
  vTaskDelay(pdMS_TO_TICKS(10));

  xTaskCreatePinnedToCore(task_FTPSrv, "FTP Server", T_FTP_STACK, NULL, T_FTP_PRIOR, &th_TFTServer, T_FTP_CPU);
  vTaskDelay(pdMS_TO_TICKS(10));

  xTaskCreatePinnedToCore(task_PZEMPoll, "PZEM polling", T_PZEM_STACK, NULL, T_PZEM_PRIOR, &th_PZEMPoll, T_PZEM_CPU);
  vTaskDelay(pdMS_TO_TICKS(10));  

  for(;;) {
    yield();
    vTaskDelete(NULL);
  }
}

void task_WEBServer(void *param) {
  print_task_header("WEB Server");

  // Регистрация обработчиков
  server.on(F("/"), h_Website);    
  server.on(F("/wifi_param"), h_wifi_param);
  server.on(F("/pushButt"), HTTP_PUT, h_pushButt);    
  server.onNotFound(h_WebRequests);       
  server.on(F("/xml"), HTTP_PUT, h_XML);

  ElegantOTA.begin(&server); 

  server.begin();  

  for (;;) {
    yield();
    /* Обработка запросов HTML клиента */
    server.handleClient();
    vTaskDelay(1);
  }
}

void task_FTPSrv(void *param) {
  print_task_header("FTP Server");
  FtpServer ftpSrv;
  const char cch_ftpUser[] = FTP_USER;
  const char cch_ftpPass[] = FTP_PASS;
  ftpSrv.begin(cch_ftpUser, cch_ftpPass); 
  for(;;) {
    yield();
    ftpSrv.handleFTP();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void task_PZEMPoll(void *param) {
  print_task_header("PZEM polling");

  swSerial.begin(9600);
  
  PZEMData pd;

  for(;;) {
    yield();
    if(pzem_poll(&pd)) {
      xQueueSend(qh_PZEMData, &pd, 0);
    }
    BaseType_t ret;
    ret = xQueueReceive(qh_ResetPZEMEnergy, &b_PZEMRst, 0);
    if(ret == pdPASS) {
      b_PZEMRst = false;
      pzem_reset_energy();
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/* Функция формирования строки XML данных */
String build_XML() {
  static String xmlStr;
  BaseType_t ret;
  PZEMData gPD;
  ret = xQueueReceive(qh_PZEMData, &gPD, 0);
  if(ret == pdPASS) {
    //print_pzemData(&gPD);    

    xmlStr = F("<?xml version='1.0'?>");
    xmlStr += F("<xml>");

    xmlStr += F("<vMin>");
    xmlStr += gPD.iVoltageMin;
    xmlStr += F("</vMin>");

    xmlStr += F("<vMax>");
    xmlStr += gPD.iVoltageMax;
    xmlStr += F("</vMax>");

    xmlStr += F("<voltage>");
    xmlStr += gPD.iVoltage;
    xmlStr += F("</voltage>");

    xmlStr += F("<current>");
    xmlStr += gPD.iCurrent;
    xmlStr += F("</current>");

    xmlStr += F("<power>");
    xmlStr += gPD.iPower;
    xmlStr += F("</power>");

    xmlStr += F("<energy>");
    xmlStr += gPD.iEnergy;
    xmlStr += F("</energy>");

    xmlStr += F("<pf>");
    xmlStr += gPD.iPowerFactor;
    xmlStr += F("</pf>");

    xmlStr += F("<freq>");
    xmlStr += gPD.iFrequency;
    xmlStr += F("</freq>");

    xmlStr += F("</xml>");   
  }     
  return xmlStr;
}

void setMinMaxVoltage(PZEMData pd) {
  if(pd.iVoltage < pd.iVoltageMin) {
    pd.iVoltageMin = pd.iVoltage;
  }
  if(pd.iVoltage > pd.iVoltageMax) {
    pd.iVoltageMax = pd.iVoltage;
  }
}

bool loadFromSpiffs(String path) {
  Serial.print(F("Request File: "));
  Serial.println(path);
  String dataType = F("text/plain");
  if(path.endsWith(F("/"))) path += F("index.html");
  if(path.endsWith(F(".src"))) path = path.substring(0, path.lastIndexOf(F(".")));
  else if(path.endsWith(F(".html"))) dataType = F("text/html");
  else if(path.endsWith(F(".htm"))) dataType = F("text/html");
  else if(path.endsWith(F(".css"))) dataType = F("text/css");
  else if(path.endsWith(F(".js"))) dataType = F("application/javascript");
  else if(path.endsWith(F(".png"))) dataType = F("image/png");
  else if(path.endsWith(F(".gif"))) dataType = F("image/gif");
  else if(path.endsWith(F(".jpg"))) dataType = F("image/jpeg");
  else if(path.endsWith(F(".ico"))) dataType = F("image/x-icon");
  else if(path.endsWith(F(".xml"))) dataType = F("text/xml");
  else if(path.endsWith(F(".pdf"))) dataType = F("application/pdf");
  else if(path.endsWith(F(".zip"))) dataType = F("application/zip");

  if(dataType != "text/xml") {
    vTaskSuspend(th_PZEMPoll);
    vTaskSuspend(th_TFTServer);
  }

  File dataFile = SPIFFS.open(path.c_str(), "r");
  Serial.print(F("Load File: "));
  Serial.println(dataFile.name());
  if (server.hasArg(F("download"))) dataType = F("application/octet-stream");
  if (server.streamFile(dataFile, dataType) != dataFile.size()) {}
  dataFile.close();

  vTaskResume(th_PZEMPoll);
  vTaskResume(th_TFTServer);

  return true;
}

/* Обработчик/handler запроса XML данных */
void h_XML() {
  server.send(200, F("text/xml"), build_XML());
}

void h_wifi_param() {
  Serial.println(F("h_wifi_param"));
  if(!server.authenticate(cch_web_user, cch_web_pass)) return server.requestAuthentication();
  String ssid = server.arg(F("wifi_ssid"));
  String pass = server.arg(F("wifi_pass"));
  Serial.print(F("Wi-Fi SSID: ")); Serial.println(ssid);
  Serial.print(F("Wi-Fi pass: ")); Serial.println(pass);
  server.send(200, F("text/html"), F("Wi-Fi setting is updated, module will be rebooting..."));
  delay(100);
  WiFi.softAPdisconnect(true);
  delay(100);
  strcpy(Param.ssid, ssid.c_str());
  strcpy(Param.pass, pass.c_str());
  save_param();
  Serial.print(F("Param SSID: ")); Serial.println(Param.ssid);
  Serial.print(F("Param pass: ")); Serial.println(Param.pass);
  WiFi.mode(WIFI_STA);
  WiFi.begin(Param.ssid, Param.pass);
  delay(500);
  SPIFFS.end();
  Serial.println(F("Resetting ESP..."));
  ESP.restart();
}

void h_pushButt() {
  Serial.println(F("h_pushButt"));
  //print_remote_IP();
  if(!server.authenticate(cch_web_user, cch_web_pass)) return server.requestAuthentication();
  Serial.print(F("Server has: ")); Serial.print(server.args()); Serial.println(F(" argument(s):"));
  for(int i = 0; i < server.args(); i++) {
    Serial.print(F("arg name: ")); Serial.print(server.argName(i)); Serial.print(F(", value: ")); Serial.println(server.arg(i));
  }
  if(server.hasArg(F("buttID"))) {
    if(server.arg(F("buttID")) == F("rstEnergy")) {    
      b_PZEMRst = true;
      xQueueSend(qh_ResetPZEMEnergy, &b_PZEMRst, portMAX_DELAY);
    }
    if(server.arg(F("buttID")) == F("emul")) {    
      if(!b_PZEMEmul) b_PZEMEmul = true;
      else b_PZEMEmul = false;
    }
  }
  server.send(200, F("text/xml"), build_XML());
}

void h_WebRequests() {
  Serial.println("h_WebRequests");
  //print_remote_IP();
  if(!server.authenticate(cch_web_user, cch_web_pass)) return server.requestAuthentication();
  if(loadFromSpiffs(server.uri())) return;
  Serial.println(F("File Not Detected"));
  String message = F("File Not Detected\n\n");
  message += F("URI: ");
  message += server.uri();
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET)?F("GET"):F("POST");
  message += F("\nArguments: ");
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " NAME:" + server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, F("text/plain"), message);
  Serial.println(message);
}

/* Обработчик/handler запроса HTML страницы */
/* Отправляет клиенту(браузеру) HTML страницу */
void h_Website() {
  Serial.println("h_Website");
  //print_remote_IP();
  if(!server.authenticate(cch_web_user, cch_web_pass)) return server.requestAuthentication();
  server.sendHeader(F("Location"), F("/index.html"), true);   //Redirect to our html web page
  server.send(302, F("text/plane"), "");
}

void pzem_send(uint8_t* txData, uint8_t len) {
  uint16_t crc = GetCRC16(txData, (len - 2));
  txData[len - 2] = crc & 0xFF; // Low byte first
  txData[len - 1] = (crc >> 8) & 0xFF; // High byte second
  swSerial.write(txData, len);
}

bool pzem_receive(uint8_t* rxData, uint8_t len) {
  for(int i = 0; i < len; i++) rxData[i] = 0x00;
  unsigned long startTime = millis();
  int index = 0;
  while((index < len) && ((millis() - startTime) < 80)) {
    yield();	// do background netw tasks while blocked for IO (prevents ESP watchdog trigger)
    if(swSerial.available() > 0) {
        uint8_t c = (uint8_t)swSerial.read();
        rxData[index++] = c;
    }
  }
  if(GetCRC16(rxData, (len - 2)) == ((rxData[len - 1] << 8) + rxData[len - 2])) return true;
  else return false;
}

void pzem_reset_energy() {
  Serial.println("Try reset energy");
  uint8_t buff[4] = {0xF8, 0x42, 0x00, 0x00};
  pzem_send(buff, 4);
  pzem_receive(buff, 4);
}

void print_pzemData(PZEMData* pd) {
  Serial.println("------------------------------");
  Serial.println(String("Voltage: ") + String(pd->iVoltage));
  Serial.println(String("Current: ") + String(pd->iCurrent));
  Serial.println(String("Power: ") + String(pd->iPower));
  Serial.println(String("Energy: ") + String(pd->iEnergy));
  Serial.println(String("Power factor: ") + String(pd->iPowerFactor));
  Serial.println(String("Frequency: ") + String(pd->iFrequency));
  Serial.println("------------------------------");
  Serial.println("");
}

#define POLYNOMIAL    0xA001
#define INITIAL_VALUE 0xFFFF
uint16_t GetCRC16(uint8_t* bufData, uint16_t sizeData)
{
  uint16_t TmpCRC, CRC, i;
  uint8_t j;
  CRC = INITIAL_VALUE;
  for(i=0; i < sizeData; i++)
  {
    TmpCRC = CRC ^ bufData[i];
    for(j=0; j < 8; j++)
    {
      if( TmpCRC & 0x0001 ) { TmpCRC >>= 1; TmpCRC ^= POLYNOMIAL; }
      else TmpCRC >>= 1;
    }
    CRC = TmpCRC;
  }
  return CRC;
}

bool pzem_poll(PZEMData* pd) {
  uint8_t txBuff[8] = {0xF8, 0x04, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00};
  uint8_t rxBuff[25];

  if(b_PZEMEmul) {
    pd->iVoltage = 2200 + random(-50, 50);
    pd->iCurrent = 3000 + random(-500, 500);;
    pd->iPower = 5000 + random(-500, 500);
    pd->iEnergy = 22437;
    pd->iPowerFactor = 100 + random(-50, 0);;
    pd->iFrequency = 500 + random(-5, 5);
    return true;
  }
  else {
    pzem_send(txBuff, 8);
    if(pzem_receive(rxBuff, 25)) {
      pd->iVoltage = ((uint32_t)rxBuff[3] << 8 | // Raw voltage in 0.1V
                                (uint32_t)rxBuff[4]);// / 10.0;

      pd->iCurrent = ((uint32_t)rxBuff[5] << 8 | // Raw current in 0.001A
                                (uint32_t)rxBuff[6] |
                                (uint32_t)rxBuff[7] << 24 |
                                (uint32_t)rxBuff[8] << 16);// / 1000.0;

      pd->iPower =  ((uint32_t)rxBuff[9] << 8 | // Raw power in 0.1W
                                (uint32_t)rxBuff[10] |
                                (uint32_t)rxBuff[11] << 24 |
                                (uint32_t)rxBuff[12] << 16);// / 10.0;

      pd->iEnergy =  ((uint32_t)rxBuff[13] << 8 | // Raw Energy in 1Wh
                                (uint32_t)rxBuff[14] |
                                (uint32_t)rxBuff[15] << 24 |
                                (uint32_t)rxBuff[16] << 16);// / 1000.0;

      pd->iFrequency = ((uint32_t)rxBuff[17] << 8 | // Raw Frequency in 0.1Hz
                                (uint32_t)rxBuff[18]);// / 10.0;

      pd->iPowerFactor =      ((uint32_t)rxBuff[19] << 8 | // Raw pf in 0.01
                                (uint32_t)rxBuff[20]);// / 100.0;

      /*_pd.alarms =  ((uint32_t)response[21] << 8 | // Raw alarm value
                                (uint32_t)response[22]);*/
      // print_pzemData(pd);
      return true;
    }
    else return false;
  }
}

/* Прочие функции ***************************************************************************/
//	eRunning = 0,	/*!< A task is querying the state of itself, so must be running. */
//	eReady,			/*!< The task being queried is in a read or pending ready list. */
//	eBlocked,		/*!< The task being queried is in the Blocked state. */
//	eSuspended,		/*!< The task being queried is in the Suspended state, or is in the Blocked state with an infinite time out. */
//	eDeleted		/*!< The task being queried has been deleted, but its TCB has not yet been freed. */

void print_task_state(String taskName, int state) {
  const String tState[] = {"eRunning", "eReady", "eBlocked", "eSuspended", "eDeleted"};
  Serial.print(F("Task "));
  Serial.print(taskName);
  Serial.print(F(" state: "));
  Serial.println(tState[state]);
}

void ap_config() {
  const char cch_APssid[] = AP_SSID;
  const char cch_APpass[] = AP_PASS;
  Serial.println(F("Wi-Fi AP mode"));
  Serial.println(F("AP configuring..."));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(cch_APssid, cch_APpass); 
  Serial.println(F("done"));
  IPAddress myIP = WiFi.softAPIP();
  Serial.print(F("AP IP address: "));
  Serial.println(myIP);    
}

void save_param() {
  int eSize = sizeof(Param);
  EEPROM.begin(eSize);
  EEPROM.put(PARAM_ADDR, Param);
  EEPROM.end();
}

void eeprom_init() {
  Serial.println("EEPROM initialization");
  int eSize = sizeof(Param);
  EEPROM.begin(eSize);
  Param.FuseMac = ESP.getEfuseMac();
  EEPROM.put(PARAM_ADDR, Param);
  EEPROM.end();
  Serial.println("EEPROM initialization completed");
  Serial.println("");
}

void print_task_header(String taskName) {
  char taskStr[4] = {'T', 'a', 's', 'k'};
  int strLenght = (taskName.length() <= 11)?(15):(taskName.length() + 4);
  Serial.println();
  for(int i = 0; i < strLenght; i++) {
    if(i < 4) Serial.print(taskStr[i]);
    else Serial.print(F("-"));
  }
  Serial.println();
  Serial.print(F("| "));
  Serial.print(taskName);
  if(taskName.length() <= 11) {
    for(int i = 0; i < 11 - taskName.length(); i++) Serial.print(F(" "));
  }  
  Serial.print(F(" |"));
  Serial.println();
  Serial.print(F("| "));
  Serial.print(F("is running!"));
  for(int i = 0; i < strLenght - 15; i++) Serial.print(F(" "));
  Serial.print(F(" |"));
  Serial.println();
  for(int i = 0; i < strLenght; i++) Serial.print(F("-"));
  Serial.println();
}
