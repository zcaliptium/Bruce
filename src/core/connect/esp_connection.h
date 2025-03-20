#ifndef __ESP_CONNECTION_H__
#define __ESP_CONNECTION_H__

#include <esp_now.h>
#include <globals.h>
#include <vector>

#define ESP_BRUCE_ID "BRUCE"
#define ESP_BRUCE_VER 0
#define ESP_FILENAME_SIZE 30
#define ESP_FILEPATH_SIZE 50
#define ESP_DATA_SIZE 152

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
        MSG_TYPE_PING,    // Device search request.
        MSG_TYPE_PONG,    // Device search response.
        MSG_TYPE_FILE,    // Share file.
        MSG_TYPE_COMMAND, // Execute remote command.
        MSG_TYPE_CHAT,    // Chat between Bruce devices.
        MSG_TYPE_APCREDS, // Share Wifi credentials.
    };

    enum MessageFlags {
        MSG_FLAG_DONE = 0x01,
    };

#pragma pack(1)
    // Message header (10 bytes)
    struct MessageHeader {
        char magic[5];       // 5 - protocol identifier
        uint8_t protocolVer; // 1 - to deal with breaking changes
        uint8_t type;        // 1 - packet type
        uint8_t flags;       // 1 - general purpose flags
        uint16_t dataSize;   // 2 - amount of data for useful payload (ESP-NOW v1.0 - 250, ESP-NOW v2.0 - 1490)

        // Constructor to initialize defaults
        MessageHeader() : protocolVer(ESP_BRUCE_VER), type(MSG_TYPE_NOP), flags(0), dataSize(0) {
            memcpy(magic, ESP_BRUCE_ID, 5);
        }
    };

    // Message body (240 bytes)
    struct MessageBody {
        size_t totalBytes;                // 4
        size_t bytesSent;                 // 4
        char filename[ESP_FILENAME_SIZE]; // 30
        char filepath[ESP_FILEPATH_SIZE]; // 50
        char data[ESP_DATA_SIZE];         // 152
    };

    // Struct has to be 250 B max (8 bytes Header + 242 for Body)
    struct Message {
        MessageHeader header;

        union {
            MessageBody body;
            char rawBody[sizeof(MessageBody)];
        };

        char zero; // terminator for text messages
        Message() : zero('\0') {}
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

    bool isTextMsgType(uint8_t type);
    String msgTypeToString(uint8_t type);

    Message createTextMessage(String text);
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
