#include "FS.h"
#include "SD.h"

struct data {
    uint16_t type;
    uint16_t daysecond;
    int16_t temp[8];
    char padding[108];      // struct needs to be 128 long...
};



void appendFile(fs::FS &fs, const char *path, const char *message);

void setupSD();

void CreateDataFileIfNotExists(fs::FS &fs, const char *path);

void writeFile(fs::FS &fs, const char *path, const char *message);
