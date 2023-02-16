#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <SD.h>
#include <FS.h>


#include "fileuploadPage.h"

void configureWebServerUpload();
String humanReadableSize(const size_t bytes);
String processor(const String &var);
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
