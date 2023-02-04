#include <Arduino.h>

#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

#include "AsyncJson.h"
#include <ArduinoJson.h>

#include "time.h"

#include "sdWriter.h"
#include "radios.h"

#include "wifiCreds.h"
#include "fileuploadPage.h"


// Digital I/O used
#define I2S_DOUT 22
#define I2S_BCLK 26
#define I2S_LRC 25

Audio audio;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");


int currentstation = 0;
int currentVolume = 7;

//****************************************************************************************
//                                   A U D I O _ T A S K                                 *
//****************************************************************************************

struct audioMessage
{
  uint8_t cmd;
  const char *txt;
  uint32_t value;
  uint32_t ret;
} audioTxMessage, audioRxMessage;

enum : uint8_t
{
  SET_VOLUME,
  GET_VOLUME,
  CONNECTTOHOST,
  CONNECTTOSD
};

QueueHandle_t audioSetQueue = NULL;
QueueHandle_t audioGetQueue = NULL;

void sendConnectingStreamInfo();
void sendStreamInfo();

void CreateQueues()
{
  audioSetQueue = xQueueCreate(10, sizeof(struct audioMessage));
  audioGetQueue = xQueueCreate(10, sizeof(struct audioMessage));
}

void audioTask(void *parameter)
{
  CreateQueues();
  if (!audioSetQueue || !audioGetQueue)
  {
    log_e("queues are not initialized");
    while (true)
    {
      ;
    } // endless loop
  }

  struct audioMessage audioRxTaskMessage;
  struct audioMessage audioTxTaskMessage;

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(5); // 0...21

  while (true)
  {
    if (xQueueReceive(audioSetQueue, &audioRxTaskMessage, 1) == pdPASS)
    {
      if (audioRxTaskMessage.cmd == SET_VOLUME)
      {
        audioTxTaskMessage.cmd = SET_VOLUME;
        audio.setVolume(audioRxTaskMessage.value);
        audioTxTaskMessage.ret = 1;
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else if (audioRxTaskMessage.cmd == CONNECTTOHOST)
      {
        audioTxTaskMessage.cmd = CONNECTTOHOST;
        audioTxTaskMessage.ret = audio.connecttohost(audioRxTaskMessage.txt);
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else if (audioRxTaskMessage.cmd == CONNECTTOSD)
      {
        audioTxTaskMessage.cmd = CONNECTTOSD;
        audioTxTaskMessage.ret = audio.connecttoSD(audioRxTaskMessage.txt);
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else if (audioRxTaskMessage.cmd == GET_VOLUME)
      {
        audioTxTaskMessage.cmd = GET_VOLUME;
        audioTxTaskMessage.ret = audio.getVolume();
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      }
      else
      {
        log_i("error");
      }
    }
    audio.loop();
    if (!audio.isRunning())
    {
      sleep(1);
    }
  }
}

void audioInit()
{
  xTaskCreatePinnedToCore(
      audioTask,             /* Function to implement the task */
      "audioplay",           /* Name of the task */
      5000,                  /* Stack size in words */
      NULL,                  /* Task input parameter */
      2 | portPRIVILEGE_BIT, /* Priority of the task */
      NULL,                  /* Task handle. */
      1                      /* Core where the task should run */
  );
}

audioMessage transmitReceive(audioMessage msg)
{
  xQueueSend(audioSetQueue, &msg, portMAX_DELAY);
  if (xQueueReceive(audioGetQueue, &audioRxMessage, portMAX_DELAY) == pdPASS)
  {
    if (msg.cmd != audioRxMessage.cmd)
    {
      log_e("wrong reply from message queue");
    }
  }
  return audioRxMessage;
}

void audioSetVolume(uint8_t vol)
{
  audioTxMessage.cmd = SET_VOLUME;
  audioTxMessage.value = vol;
  audioMessage RX = transmitReceive(audioTxMessage);
}

uint8_t audioGetVolume()
{
  audioTxMessage.cmd = GET_VOLUME;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

bool audioConnecttohost(const char *host)
{
  audioTxMessage.cmd = CONNECTTOHOST;
  audioTxMessage.txt = host;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

bool audioConnecttoSD(const char *filename)
{
  audioTxMessage.cmd = CONNECTTOSD;
  audioTxMessage.txt = filename;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

// ********************** end audio ****************************

// glue

void handlePlayStream(char * streamlink)
{
  audioConnecttohost(streamlink);
}

void prevStation()
{
  currentstation--;
  if (currentstation < 0 ) currentstation = (sizeof(stations) / sizeof(stations[0])) -1;
  sendConnectingStreamInfo();
  audioConnecttohost(stations[currentstation]);
}

void nextStation()
{
  currentstation = (currentstation + 1) % (sizeof(stations) / sizeof(stations[0]));
  sendConnectingStreamInfo();
  audioConnecttohost(stations[currentstation]);
}

void volUp()
{
  currentVolume++;
  if (currentVolume > 21)
    currentVolume = 21;
  audioSetVolume(currentVolume);
  sendStreamInfo();
}

void volDown()
{
  currentVolume--;
  if (currentVolume < 0)
    currentVolume = 0;
  audioSetVolume(currentVolume);
  sendStreamInfo();
}

/* #region webserver */
void notFound(AsyncWebServerRequest *request)
{
  // hier check spiffs maken, wanneer ook daar niet gevonden, dan:

  request->send(404, "text/plain", "Not found");
}

void initWebserver()
{
  // server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server.serveStatic("/", SD, "/");

  server.on("/hello", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", "Hello, world"); });

  server.on("/stations", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              AsyncJsonResponse *response = new AsyncJsonResponse();
              response->addHeader("Server", "ESP Async Web Server");
              JsonObject &root = response->getRoot();
              
              char keybuf[10];

              JsonArray& array = root.createNestedArray("stations");

              for (int i = 0; i < (sizeof(stations) / sizeof(stations[0])); i++){
                //printf(keybuf,"stat%d",i);
                //root[i.toStr()] = stations[i];
                array.add(stations[i]);
              }
              
              root["heap"] = ESP.getFreeHeap();
              root["ssid"] = WiFi.SSID();
              response->setLength();
              request->send(response);
            });

  // Send a GET request to <IP>/get?message=<message>
  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request)
            {
        String message;
        if (request->hasParam("parameter1")) {
            message = request->getParam("parameter1")->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, GET: " + message); });

  // Send a POST request to <IP>/post with a form field message set to <message>
  server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request)
            {
        String message;
        if (request->hasParam("parameter1", true)) {
            message = request->getParam("parameter1", true)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, POST: " + message); });

  server.onNotFound(notFound);

  server.begin();
}

/* #endregion */

/* #region websockets */



void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;

  Serial.println("Ws message is received!");
  Serial.println((char *)data);

  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    if (strcmp((char *)data, "volUp") == 0)             volUp();
    if (strcmp((char *)data, "volDown") == 0)           volDown();
    if (strcmp((char *)data, "nextStation") == 0)       nextStation();
    if (strcmp((char *)data, "prevStation") == 0)       prevStation();
    if (strncmp((char *)data, "playstream ", 11) == 0)  handlePlayStream((char *)&data[11]);   // 11 is length of searchstring


    
    //if ((char *)data.startsWith("playstream")) handlePlayStream(data);
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    sendStreamInfo();
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    Serial.printf("WebSocket client #%u %s send message!\n", client->id(), client->remoteIP().toString().c_str());
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

/* #endregion */

//****************************************************************************************
//                                   S E T U P                                           *
//****************************************************************************************

const char host[] = "webradio";

void setup()
{
  Serial.begin(115200);
  Serial.printf("Trying to connect to you wifi on SSID: %s", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
    delay(1500);

  if (MDNS.begin(host))
  {
    MDNS.addService("http", "tcp", 80);
    Serial.println("MDNS responder started");
    Serial.print("You can now connect to http://");
    Serial.print(host);
    Serial.println(".local");
  }

  setupSD();

  initWebserver();
  initWebSocket();

  audioInit();

  audioConnecttohost(stations[currentstation]);
  // audioSetVolume(15);
  volUp();
  log_i("current volume is: %d", audioGetVolume());
}

//****************************************************************************************
//                                   L O O P                                             *
//****************************************************************************************

void loop()
{
  // your own code here
}
//*****************************************************************************************
//                                  E V E N T S                                           *
//*****************************************************************************************

char station[100];
char streamtitle[100];

void sendConnectingStreamInfo()
{
  strcpy(station, "-- connecting --");
  strcpy(streamtitle, "-- waiting --");
  sendStreamInfo();
}

void sendStreamInfo()
{
  char buf[500];
  sprintf(buf, "{\"title\":\"%s\",\"stat\":\"%s\",\"statidx\":\"%d\",\"vol\":\"%d\"}", streamtitle, station, currentstation, currentVolume);
  ws.textAll(buf);
  Serial.println(buf);
}

void audio_info(const char *info)
{
  Serial.print("info        ");
  Serial.println(info);
  // ws.textAll(info);
}

void audio_id3data(const char *info)
{ // id3 metadata
  Serial.print("*id3data     ");
  Serial.println(info);
}
void audio_eof_mp3(const char *info)
{ // end of file
  Serial.print("*eof_mp3     ");
  Serial.println(info);
}

void audio_showstation(const char *info)
{
  strcpy(station, info);
  sendStreamInfo();

  Serial.print("*station     ");
  Serial.println(info);
}

void audio_showstreamtitle(const char *info)
{
  strcpy(streamtitle, info);
  sendStreamInfo();

  Serial.print("*streamtitle ");
  Serial.println(info);
}
