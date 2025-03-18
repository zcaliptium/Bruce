#ifndef __ESP_BRUCE_H__
#define __ESP_BRUCE_H__

#include "esp_connection.h"

#define ESP_BRUCE_ID "BRUCE"
#define ESP_BRUCE_VER 0
#define ESP_FILENAME_SIZE 30
#define ESP_FILEPATH_SIZE 50
#define ESP_DATA_SIZE 150
#define ESP_RAWDATA_SIZE (ESP_NOW_MAX_DATA_LEN - sizeof(MessageHeader))
#define ESP_CMDLONG_SIZE 1024

class EspBruce : public EspConnection {
public:
    enum State {
        STATE_CONNECTING,
        STATE_STARTED,
        STATE_WAITING,
        STATE_FAILED,
        STATE_DONE,
        STATE_BREAK,
    };

    enum MessageType {
        MSG_TYPE_NOP = 0,  // Does nothing.
        MSG_TYPE_PING,     // Device search request.
        MSG_TYPE_PONG,     // Device search response.
        MSG_TYPE_FILEHEAD, // File - first piece.
        MSG_TYPE_FILEBODY, // File - puzzle pieces
        MSG_TYPE_CMDTINY,  // Shell - single packet.
        MSG_TYPE_CMDLONG,  // Shell - command sequence.
        MSG_TYPE_TEXTTINY, // Text - tiny message.
        MSG_TYPE_TEXTLONG, // Text - large portion.
        MSG_TYPE_MAX,
    };

    enum MessageFlags {
        MSG_FLAG_DONE = 0x01,
    };

    enum MessageFilter {
        MSG_FILTER_NONE = 0,
        MSG_FILTER_SERIAL,
        MSG_FILTER_FILE,
        MSG_FILTER_TEXT,
    };

#pragma pack(1)
    // Sequence transfer state.
    struct SeqTransfer {
        uint8_t mac[6];     // peer address
        bool isStarted;     // started
        uint16_t handle;    // sequence identifier
        size_t size;        // full size (up to 4 GB)
        size_t dataCounter; // amount of exchanged bytes
        String filename;    // file sharing - local full path
        String command;     // serial - text command

        // Constructor to initialize defaults.
        SeqTransfer() { reset(); }

        void reset() {
            memset(mac, 0, sizeof(mac));
            isStarted = false;
            handle = 0;
            size = 0;
            dataCounter = 0;
            filename = "";
        }

        // Size of useful payload.
        size_t maxData() { return ESP_DATA_SIZE; }
    };

    // Message head (10 bytes)
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

    // Sequence block (240 bytes)
    struct SequenceBlock {
        uint16_t handle;   // 2 - sequence identifier
        size_t totalBytes; // 4 - file size
        size_t bytesSent;  // 4 - data counter
        char data[230];    // 230 - useful payload
    };

    // File head block (240 bytes)
    struct FileHeadBlock {
        uint16_t handle;                  // 2 - sequence identifier
        size_t totalBytes;                // 4 - file size
        size_t bytesSent;                 // 4 - data counter
        char filename[ESP_FILENAME_SIZE]; // 30
        char filepath[ESP_FILEPATH_SIZE]; // 50
        char data[ESP_DATA_SIZE];         // 150
    };

    // Max struct size (ESP-NOW v1.0 - 250, ESP-NOW v2.0 - 1490)
    // Struct should be 250B max (10 bytes Header + 240 for Message)
    struct Message {
        MessageHeader head;

        union {
            SequenceBlock seq;
            FileHeadBlock fhb;
            char rawBody[ESP_RAWDATA_SIZE];
        };
        char zero;      // terminator for text data
        uint8_t mac[6]; // track mac address for received packets

        // Constructor to initialize defaults
        Message() : zero('\0') {
            memset(rawBody, 0, sizeof(rawBody)); // fill everything with zeroes
            memset(mac, 0, sizeof(mac));
        }

        // Pointer to useful payload.
        char *getData() {
            if (head.type == MSG_TYPE_FILEHEAD) { return fhb.data; }
            if (head.type == MSG_TYPE_FILEBODY) { return seq.data; }
            if (head.type == MSG_TYPE_CMDLONG) { return seq.data; }
            if (head.type == MSG_TYPE_TEXTLONG) { return seq.data; }
            return rawBody;
        }

        // Size of useful payload.
        size_t maxData() {
            if (head.type == MSG_TYPE_FILEHEAD) { return sizeof(fhb.data); }
            if (head.type == MSG_TYPE_FILEBODY) { return sizeof(seq.data); }
            if (head.type == MSG_TYPE_CMDLONG) { return sizeof(seq.data); }
            if (head.type == MSG_TYPE_TEXTLONG) { return sizeof(seq.data); }
            return sizeof(rawBody);
        }
    };
#pragma pack()

    enum AppMode {
        APP_MODE_CMDSSEND,
        APP_MODE_CMDSRECV,
        APP_MODE_FILESEND,
        APP_MODE_FILERECV,
    };

    EspBruce();

protected:
    MessageFilter rxQueueFilter;
    SeqTransfer rxSeq;
    SeqTransfer txSeq;

    State rxState;
    State txState;
    std::vector<Message> rxQueue;

    uint8_t dstAddress[6];

    /////////////////////////////////////////////////////////////////////////////////////
    // Helpers
    /////////////////////////////////////////////////////////////////////////////////////
    bool beginSend();
    void displayBanner(AppMode mode);
    File selectFile();
    void setDstAddress(const uint8_t *address) { memcpy(dstAddress, address, 6); }

    Message createMessage(String text);
    Message createFileMessage(File file);
    Message createPingMessage();
    Message createPongMessage();

    void sendPing();
    void sendPong(const uint8_t *mac);
    void appendPeerToList(const uint8_t *mac);

    bool isSeqMsgType(uint8_t type);
    String msgTypeToString(uint8_t type);
    void printMessage(Message message);

    virtual void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) override;
    virtual void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) override;
};

#endif
