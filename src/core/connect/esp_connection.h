#ifndef __ESP_CONNECTION_H__
#define __ESP_CONNECTION_H__

#include <esp_now.h>
#include <globals.h>
#include <vector>

#define ESP_BRUCE_ID "BRUCE"
#define ESP_BRUCE_VER 0
#define ESP_FILENAME_SIZE 30
#define ESP_FILEPATH_SIZE 50
#define ESP_DATA_SIZE 150

class EspConnection {
public:
    enum Status {
        CONNECTING,
        STARTED,
        WAITING,
        FAILED,
        SUCCESS,
        ABORTED,
    };

    enum MessageType {
        MSG_TYPE_NOP = 0, // Does nothing.
        MSG_TYPE_PING,
        MSG_TYPE_PONG,
        MSG_TYPE_FILE,
        MSG_TYPE_COMMAND,
    };

#pragma pack(1)
    // Message header (8 bytes)
    struct MessageHeader {
        char magic[5];       // 5 - protocol identifier
        uint8_t protocolVer; // 1 - to deal with breaking changes
        uint8_t type;        // 1 - packet type
        bool done;           // 1

        // Constructor to initialize defaults
        MessageHeader() : protocolVer(ESP_BRUCE_VER), type(MSG_TYPE_NOP), done(false) {
            memcpy(magic, ESP_BRUCE_ID, 5);
        }
    };

    // Struct has to be 250 B max (8 bytes Header + 242 for Message)
    struct Message {
        MessageHeader header;
        char filename[ESP_FILENAME_SIZE]; // 30
        char filepath[ESP_FILEPATH_SIZE]; // 50
        char data[ESP_DATA_SIZE];         // 150
        size_t dataSize;                  // 4
        size_t totalBytes;                // 4
        size_t bytesSent;                 // 4

        // Constructor to initialize defaults
        Message() : dataSize(0), totalBytes(0) {}
    };
#pragma pack()

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
    Status recvStatus;
    Status sendStatus;
    uint8_t dstAddress[6];
    uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    std::vector<Message> recvQueue;

    bool beginSend();
    bool beginEspnow();

    String msgTypeToString(uint8_t type);

    Message createMessage(String text);
    Message createFileMessage(File file);
    Message createPingMessage();
    Message createPongMessage();

    void sendPing();
    void sendPong(const uint8_t *mac);

    bool setupPeer(const uint8_t *mac);
    void appendPeerToList(const uint8_t *mac);
    void setDstAddress(const uint8_t *address) { memcpy(dstAddress, address, 6); }

    std::string macToString(const uint8_t *mac);
    void printMessage(Message message);

    void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
    void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len);

private:
    static EspConnection *instance;
};

#endif
