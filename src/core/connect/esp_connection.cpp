#include "esp_connection.h"
#include "core/display.h"
#include <WiFi.h>

// Initialize the static instance pointer
EspConnection *EspConnection::instance = nullptr;
std::vector<Option> peerOptions;

EspConnection::EspConnection() { setInstance(this); }

EspConnection::~EspConnection() {
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();

    esp_now_deinit();
}

bool EspConnection::beginSend() {
    txState = CONNECTING;

    if (!beginEspnow()) return false;

    sendPing();

    loopOptions(peerOptions);

    peerOptions.clear();

    if (!addPeer(dstAddress)) {
        displayError("Failed to add peer");
        delay(1000);
        return false;
    }

    return true;
}

bool EspConnection::beginEspnow() {
    WiFi.mode(WIFI_STA);

    if ((sizeof(MessageHeader) + sizeof(MessageBody)) != ESP_NOW_MAX_DATA_LEN) {
        displayError("(Header + Body) != ESP_NOW_MAX_DATA_LEN");
        delay(1000);
        return false;
    }

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

EspConnection::Message EspConnection::createMessage(String text) {
    Message message;

    message.header.dataSize = text.length();
    message.header.flags |= MSG_FLAG_DONE;
    message.body.totalBytes = text.length();
    message.body.bytesSent = text.length();

    strncpy(message.body.data, text.c_str(), ESP_DATA_SIZE);

    return message;
}

EspConnection::Message EspConnection::createFileMessage(File file) {
    Message message;
    String path = String(file.path());

    message.header.type = MSG_TYPE_FILE;
    message.body.totalBytes = file.size();

    strncpy(message.body.filename, file.name(), ESP_FILENAME_SIZE);
    strncpy(message.body.filepath, path.substring(0, path.lastIndexOf("/")).c_str(), ESP_FILEPATH_SIZE);

    return message;
}

EspConnection::Message EspConnection::createPingMessage() {
    Message message;
    message.header.type = MSG_TYPE_PING;

    return message;
}

EspConnection::Message EspConnection::createPongMessage() {
    Message message;
    message.header.type = MSG_TYPE_PONG;

    return message;
}

void EspConnection::sendPing() {
    peerOptions = {
        {"Broadcast", [=]() { setDstAddress(broadcastAddress); }},
    };

    Message message = createPingMessage();

    esp_err_t response = esp_now_send(broadcastAddress, (uint8_t *)&message, ESP_NOW_MAX_DATA_LEN);
    if (response != ESP_OK) { Serial.printf("Send ping response: %s\n", esp_err_to_name(response)); }

    delay(500);
}

void EspConnection::sendPong(const uint8_t *mac) {
    Message message = createPongMessage();

    if (!addPeer(mac)) return;

    esp_err_t response = esp_now_send(mac, (uint8_t *)&message, ESP_NOW_MAX_DATA_LEN);
    if (response != ESP_OK) { Serial.printf("Send pong response: %s\n", esp_err_to_name(response)); }
}

bool EspConnection::addPeer(const uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) return true;

    esp_now_peer_info_t peerInfo = {};

    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = ESP_CHANNEL_CURRENT;
    peerInfo.encrypt = false;

    return esp_now_add_peer(&peerInfo) == ESP_OK;
}

String EspConnection::msgTypeToString(uint8_t type) {
    switch (type) {
        case MSG_TYPE_NOP: return "MSG_NOP";
        case MSG_TYPE_PING: return "MSG_PING";
        case MSG_TYPE_PONG: return "MSG_PONG";
        case MSG_TYPE_FILE: return "MSG_FILE";
        case MSG_TYPE_COMMAND: return "MSG_COMMAND";
        case MSG_TYPE_CHAT: return "MSG_CHAT";
    }

    return "<invalid>";
}

void EspConnection::printMessage(Message message) {
    delay(100);

    Serial.println("Message Details:");

    // Print message header
    Serial.println("Version: " + String(message.header.protocolVer));
    Serial.println("Type: " + msgTypeToString(message.header.type));
    Serial.println("Flags: " + String(message.header.flags));
    Serial.println("Data Size: " + String(message.header.dataSize));
    Serial.println("");

    if (message.header.type != MSG_TYPE_FILE && message.header.type != MSG_TYPE_COMMAND) { return; }

    // for MSG_TYPE_FILE & MSG_TYPE_COMMAND
    Serial.println("Total Bytes: " + String(message.body.totalBytes));
    Serial.println("Bytes Sent: " + String(message.body.bytesSent));
    Serial.println("Filename: " + String(message.body.filename));
    Serial.println("Filepath: " + String(message.body.filepath));
    Serial.print("Data: ");

    // Append data to the result if dataSize is greater than 0
    if (message.header.dataSize > 0) {
        for (size_t i = 0; i < message.header.dataSize; ++i) {
            Serial.print((char)message.body.data[i]); // Assuming data contains valid characters
        }
    } else {
        Serial.println("No data");
    }

    Serial.println("");
}

String EspConnection::macToString(const uint8_t *mac) {
    char macStr[18];
    sprintf(macStr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return macStr;
}

void EspConnection::appendPeerToList(const uint8_t *mac) {
    peerOptions.push_back({macToString(mac).c_str(), [=]() { setDstAddress(mac); }});
}

void EspConnection::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        txState = SUCCESS;
        Serial.println("ESPNOW send success");
    } else {
        txState = FAILED;
        Serial.println("ESPNOW send fail");
    }
}

void EspConnection::onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    // there should be place for header & valid signature
    if (len < sizeof(MessageHeader) || strncmp((const char *)incomingData, ESP_BRUCE_ID, 5) != 0) {
        return; // ignore non-Bruce packets
    }

    Message recvMessage;

    // Use reinterpret_cast and copy assignment
    const Message *incomingMessage = reinterpret_cast<const Message *>(incomingData);
    recvMessage = *incomingMessage; // Use copy assignment
    recvMessage.zero = '\0'; // copy assignment can rewrite terminator
    memcpy(recvMessage.mac, mac, sizeof(recvMessage.mac)); // keep mac for queued messages.

    printMessage(recvMessage);

    switch (recvMessage.header.type) {
        case MSG_TYPE_NOP: return; // do nothing

        case MSG_TYPE_PING: {
            sendPong(mac);
            return;
        }

        case MSG_TYPE_PONG: {
            appendPeerToList(mac);
            return;
        }

        case MSG_TYPE_FILE:
        case MSG_TYPE_COMMAND: {
            recvQueue.push_back(recvMessage);
        }
    }
}
