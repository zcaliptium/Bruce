#ifndef __ESP_CONNECTION_H__
#define __ESP_CONNECTION_H__

#include <esp_now.h>
#include <globals.h>
#include <vector>

#define ESP_CHANNEL_CURRENT 0
#define ESP_BRUCE_ID "BRUCE"
#define ESP_BRUCE_VER 0
#define ESP_FILENAME_SIZE 30
#define ESP_FILEPATH_SIZE 50
#define ESP_DATA_SIZE 150

class EspConnection {
public:
    enum State {
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
        MSG_TYPE_CHAT,    // Text messages.
        MSG_TYPE_MAX,
    };

    enum MessageFlags {
        MSG_FLAG_DONE = 0x01,
    };

#pragma pack(1)
    // Sequence transfer state.
    struct SeqTransfer {
        uint8_t mac[6];     // peer address
        uint16_t seqId;     // sequence identifier
        size_t size;        // full size (up to 4 GB)
        size_t dataCounter; // amount of exchanged bytes
        String filename;    // file sharing - local full path

        // Constructor to initialize defaults.
        SeqTransfer() {
            reset();
        }

        void reset() {
            memset(mac, 0, sizeof(mac));
            seqId = 0;
            size = 0;
            dataCounter = 0;
            filename = "";
        }
    };

    // Message header (10 bytes)
    struct MessageHeader {
        char magic[5];       // 5 - protocol identifier
        uint8_t protocolVer; // 1 - to deal with breaking changes
        uint8_t type;        // 1 - packet type
        uint8_t flags;       // 1 - general purpose flags
        uint16_t dataSize;   // 2 - amount of data for useful payload

        // Constructor to initialize defaults
        MessageHeader() : protocolVer(ESP_BRUCE_VER), type(MSG_TYPE_NOP), flags(0), dataSize(0) {
            memcpy(magic, ESP_BRUCE_ID, 5);
        }
    };

    // Message body (240 bytes)
    struct MessageBody {
        uint16_t seqId;                   // 2 - sequence identifier
        size_t totalBytes;                // 4 - file size
        size_t bytesSent;                 // 4 - block offset
        char filename[ESP_FILENAME_SIZE]; // 30
        char filepath[ESP_FILEPATH_SIZE]; // 50
        char data[ESP_DATA_SIZE];         // 150
    };

    // Max struct size (ESP-NOW v1.0 - 250, ESP-NOW v2.0 - 1490)
    // Struct should be 250B max (10 bytes Header + 240 for Message)
    struct Message {
        MessageHeader header;
        union {
            MessageBody body;
            char rawBody[sizeof(MessageBody)];
        };
        char zero; // terminator for text data
        uint8_t mac[6]; // track mac address for received packets

        // Constructor to initialize defaults
        Message() : zero('\0') {
            memset(rawBody, 0, sizeof(MessageBody)); // fill everything with zeroes
            memset(mac, 0, sizeof(mac));
        }
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
    SeqTransfer rxSeq;
    SeqTransfer txSeq;

    State rxState;
    State txState;

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

    bool addPeer(const uint8_t *mac);
    void appendPeerToList(const uint8_t *mac);
    void setDstAddress(const uint8_t *address) { memcpy(dstAddress, address, 6); }

    String macToString(const uint8_t *mac);
    void printMessage(Message message);

    void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
    void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len);

private:
    static EspConnection *instance;
};

#endif
