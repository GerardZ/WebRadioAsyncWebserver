#include "FS.h"
#include "SD.h"
#include "sdWriter.h"

// Define CS pin for the SD card module
#define SD_CS 5

void setupSD()
{
    // Initialize SD card
    SD.begin(SD_CS);
    if (!SD.begin(SD_CS))
    {
        Serial.println("Card Mount Failed");
        return;
    }
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE)
    {
        Serial.println("No SD card attached");
        return;
    }
    Serial.println("Initializing SD card...");
    if (!SD.begin(SD_CS))
    {
        Serial.println("ERROR - SD card initialization failed!");
        return; // init failed
    }

    // If the data.txt file doesn't exist
    // Create a file on the SD card and write the data labels
    File file = SD.open("/data.txt");
    if (!file)
    {
        Serial.println("File doens't exist");
        Serial.println("Creating file...");
        writeFile(SD, "/data.txt", "Reading ID, Date, Hour, Temperature \r\n");
    }
    else
    {
        Serial.println("File already exists");
    }
    file.close();
}

// Write to the SD card (DON'T MODIFY THIS FUNCTION)
void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println("Failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        Serial.println("File written");
    }
    else
    {
        Serial.println("Write failed");
    }
    file.close();
}

// Append data to the SD card (DON'T MODIFY THIS FUNCTION)
void appendFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Appending to file: %s\n", path);

    File file = fs.open(path, FILE_APPEND);
    if (!file)
    {
        Serial.println("Failed to open file for appending");
        return;
    }
    if (file.print(message))
    {
        Serial.println("Message appended");
    }
    else
    {
        Serial.println("Append failed");
    }
    file.close();
}



void CreateDataFileIfNotExists(fs::FS &fs, const char *path)
{
    data myData;
    myData.type = 0;
    myData.temp[0] = 65;

    if (fs.exists(path)){
        File file = fs.open(path, FILE_READ);
        int size = file.size();
        Serial.printf("\nFile %s (%d bytes) already exists, skipping create.\n\n", path, size);
        file.close();
        
        return;
    } 


    File file = fs.open(path, FILE_APPEND);
    
    Serial.printf("length of data: %d\n", sizeof(data));
    
    Serial.println("writing BIGfile to sd:");

    
    if (file){
        for(int day=1; day <366;day++){
            for(int second =0 ; second < 86400; second += 300){
                size_t result = file.write((byte *)&myData, sizeof(myData));
                myData.type++;
                //Serial.printf("%d bytes written\n", result);

            }
            delay(0);
            //Serial.print(".");
        }

    }
    Serial.println("\nwritten BIGfile to sd...");
    Serial.print("Size of BIGfile is: ");
    Serial.println(file.size());
    file.close();
}
