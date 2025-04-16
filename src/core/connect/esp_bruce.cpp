#include "esp_bruce.h"
#include "core/display.h"

EspBruce::EspBruce() {}

File EspBruce::selectFile() {
    String filename;
    FS *fs = &LittleFS;
    setupSdCard();
    if (sdcardMounted) {
        options = {
            {"SD Card",  [&]() { fs = &SD; }      },
            {"LittleFS", [&]() { fs = &LittleFS; }},
        };
        loopOptions(options);
    }
    filename = loopSD(*fs, true);

    File file = fs->open(filename, FILE_READ);
    return file;
}

void EspBruce::displayBanner(AppMode mode) {
    switch (mode) {
        case APP_MODE_FILERECV: drawMainBorderWithTitle("RECEIVE FILE"); break;
        case APP_MODE_FILESEND: drawMainBorderWithTitle("SEND FILE"); break;
        case APP_MODE_CMDSRECV: drawMainBorderWithTitle("RECEIVE COMMANDS"); break;
        case APP_MODE_CMDSSEND: drawMainBorderWithTitle("SEND COMMANDS"); break;
        default: drawMainBorderWithTitle("UNKNOWN MODE"); break;
    }

    padprintln("");
}
