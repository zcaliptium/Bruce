#ifndef __ESP_CONNECTION_H__
#define __ESP_CONNECTION_H__

#include <esp_now.h>
#include <globals.h>
#include <vector>

#define ESP_CHANNEL_CURRENT 0

class EspConnection {
public:
    EspConnection();
    ~EspConnection();

    static void setInstance(EspConnection *conn) { instance = conn; }

    static void onDataSentStatic(const uint8_t *mac_addr, esp_now_send_status_t status) {
        if (instance) instance->onDataSent(mac_addr, status);
    };
    static void onDataRecvStatic(const uint8_t *mac, const uint8_t *incomingData, int len) {
        if (instance) instance->onDataRecv(mac, incomingData, len);
    };

protected:
    uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    bool beginEspnow();

    bool addPeer(const uint8_t *mac);

    String macToString(const uint8_t *mac);

    virtual void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) = 0;
    virtual void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) = 0;

private:
    static EspConnection *instance;
};

#endif
