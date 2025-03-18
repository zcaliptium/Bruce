#include "esp_connection.h"
#include "core/display.h"
#include <WiFi.h>

// Initialize the static instance pointer
EspConnection *EspConnection::instance = nullptr;

EspConnection::EspConnection() { setInstance(this); }

EspConnection::~EspConnection() {
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();

    esp_now_deinit();
}

bool EspConnection::beginEspnow() {
    WiFi.mode(WIFI_STA);

    /*
    if (sizeof(FileHeadBlock) > ESP_RAWDATA_SIZE || sizeof(SequenceBlock) > ESP_RAWDATA_SIZE) {
        displayError("(FileHeadBlock or SequenceBlock) > ESP_RAWDATA_SIZE");
        delay(1000);
        return false;
    }
    */

    if (esp_now_init() != ESP_OK) {
        displayError("Error initializing share");
        delay(1000);
        return false;
    }

    if (!addPeer(broadcastAddress)) {
        displayError("Failed to add peer");
        delay(1000);
        return false;
    }

    esp_now_register_send_cb(onDataSentStatic);
    esp_now_register_recv_cb(onDataRecvStatic);

    return true;
}

bool EspConnection::addPeer(const uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) return true;

    esp_now_peer_info_t peerInfo = {};

    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = ESP_CHANNEL_CURRENT;
    peerInfo.encrypt = false;

    return esp_now_add_peer(&peerInfo) == ESP_OK;
}

String EspConnection::macToString(const uint8_t *mac) {
    char macStr[18];
    sprintf(macStr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return macStr;
}
