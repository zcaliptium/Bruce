#ifndef __ESP_BRUCE_H__
#define __ESP_BRUCE_H__

#include "esp_connection.h"

class EspBruce : public EspConnection {
public:
    enum AppMode {
        APP_MODE_CMDSSEND,
        APP_MODE_CMDSRECV,
        APP_MODE_FILESEND,
        APP_MODE_FILERECV,
    };

    EspBruce();

protected:
    /////////////////////////////////////////////////////////////////////////////////////
    // Helpers
    /////////////////////////////////////////////////////////////////////////////////////
    void displayBanner(AppMode mode);
    File selectFile();
};

#endif
