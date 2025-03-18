#include "esp_bruce.h"
#include "core/display.h"

std::vector<Option> peerOptions;

EspBruce::EspBruce() { rxQueueFilter = MSG_FILTER_NONE; }

bool EspBruce::beginSend() {
    txState = STATE_CONNECTING;

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

EspBruce::Message EspBruce::createMessage(String text) {
    Message message;

    // clamp dataSize to actual capacity
    message.head.dataSize = min(text.length(), message.maxData());
    message.head.flags |= MSG_FLAG_DONE;

    strncpy(message.getData(), text.c_str(), message.maxData());

    return message;
}

EspBruce::Message EspBruce::createFileMessage(File file) {
    Message message;
    String path = String(file.path());

    message.head.type = MSG_TYPE_FILEHEAD;
    message.seq.totalBytes = file.size();

    strncpy(message.fhb.filename, file.name(), ESP_FILENAME_SIZE);
    strncpy(message.fhb.filepath, path.substring(0, path.lastIndexOf("/")).c_str(), ESP_FILEPATH_SIZE);

    return message;
}

EspBruce::Message EspBruce::createPingMessage() {
    Message message;
    message.head.type = MSG_TYPE_PING;

    return message;
}

EspBruce::Message EspBruce::createPongMessage() {
    Message message;
    message.head.type = MSG_TYPE_PONG;

    return message;
}

void EspBruce::sendPing() {
    peerOptions = {
        {"Broadcast", [=]() { setDstAddress(broadcastAddress); }},
    };

    Message message = createPingMessage();

    esp_err_t response = esp_now_send(broadcastAddress, (uint8_t *)&message, ESP_NOW_MAX_DATA_LEN);
    if (response != ESP_OK) { Serial.printf("Send ping response: %s\n", esp_err_to_name(response)); }

    delay(500);
}

void EspBruce::sendPong(const uint8_t *mac) {
    Message message = createPongMessage();

    if (!addPeer(mac)) return;

    esp_err_t response = esp_now_send(mac, (uint8_t *)&message, ESP_NOW_MAX_DATA_LEN);
    if (response != ESP_OK) { Serial.printf("Send pong response: %s\n", esp_err_to_name(response)); }
}

void EspBruce::appendPeerToList(const uint8_t *mac) {
    peerOptions.push_back({macToString(mac).c_str(), [=]() { setDstAddress(mac); }});
}

bool EspBruce::isSeqMsgType(uint8_t type) {
    return type == MSG_TYPE_FILEHEAD || type == MSG_TYPE_FILEBODY || type == MSG_TYPE_CMDLONG ||
           type == MSG_TYPE_TEXTLONG;
}

String EspBruce::msgTypeToString(uint8_t type) {
    switch (type) {
        case MSG_TYPE_NOP: return "MSG_NOP";
        case MSG_TYPE_PING: return "MSG_PING";
        case MSG_TYPE_PONG: return "MSG_PONG";
        case MSG_TYPE_FILEHEAD: return "MSG_FILEHEAD";
        case MSG_TYPE_FILEBODY: return "MSG_FILEBODY";
        case MSG_TYPE_CMDTINY: return "MSG_CMDTINY";
        case MSG_TYPE_CMDLONG: return "MSG_CMDLONG";
        case MSG_TYPE_TEXTTINY: return "MSG_TEXTTINY";
        case MSG_TYPE_TEXTLONG: return "MSG_TEXTLONG";
    }

    return "<invalid>";
}

void EspBruce::printMessage(Message message) {
    delay(100);

    Serial.println("Message Details:");

    // Print message head
    Serial.println("Version: " + String(message.head.protocolVer));
    Serial.println("Type: " + msgTypeToString(message.head.type));
    Serial.println("Flags: " + String(message.head.flags));
    Serial.println("Data Size: " + String(message.head.dataSize));
    Serial.println("");

    // for MSG_TYPE_FILEHEAD & MSG_TYPE_CMDTINY
    if (isSeqMsgType(message.head.type)) {
        Serial.println("Sequence Id: " + String(message.seq.handle));
        Serial.println("Total Bytes: " + String(message.seq.totalBytes));
        Serial.println("Bytes Sent: " + String(message.seq.bytesSent));

        if (message.head.type == MSG_TYPE_FILEHEAD) {
            Serial.println("Filename: " + String(message.fhb.filename));
            Serial.println("Filepath: " + String(message.fhb.filepath));
        }
    }

    Serial.print("Data: ");

    // Append data to the result if dataSize is greater than 0
    if (message.head.dataSize > 0) {
        for (size_t i = 0; i < message.head.dataSize; ++i) {
            Serial.print((char)message.getData()[i]); // Assuming data contains valid characters
        }
    } else {
        Serial.println("No data");
    }

    Serial.println("");
}

void EspBruce::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        txState = STATE_DONE;
        Serial.println("ESPNOW send success");
    } else {
        txState = STATE_FAILED;
        Serial.println("ESPNOW send fail");
    }
}

void EspBruce::onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    // there should be place for head & valid signature
    if (len < sizeof(MessageHeader) || strncmp((const char *)incomingData, ESP_BRUCE_ID, 5) != 0) {
        return; // ignore non-Bruce packets
    }

    Message rxMessage;

    // Use reinterpret_cast and copy assignment
    const Message *incomingMessage = reinterpret_cast<const Message *>(incomingData);
    rxMessage = *incomingMessage;                      // Use copy assignment
    rxMessage.zero = '\0';                             // copy assignment can rewrite terminator
    memcpy(rxMessage.mac, mac, sizeof(rxMessage.mac)); // keep mac for queued messages.

    printMessage(rxMessage);

    switch (rxMessage.head.type) {
        case MSG_TYPE_NOP: return; // do nothing

        case MSG_TYPE_PING: {
            sendPong(mac);
            return;
        }

        case MSG_TYPE_PONG: {
            appendPeerToList(mac);
            return;
        }

        case MSG_TYPE_FILEHEAD:
        case MSG_TYPE_FILEBODY: {
            if (rxQueueFilter == MSG_FILTER_FILE) { rxQueue.push_back(rxMessage); }
            return;
        }

        case MSG_TYPE_CMDTINY:
        case MSG_TYPE_CMDLONG: {
            if (rxQueueFilter == MSG_FILTER_SERIAL) { rxQueue.push_back(rxMessage); }
            return;
        }

        case MSG_TYPE_TEXTTINY:
        case MSG_TYPE_TEXTLONG: {
            if (rxQueueFilter == MSG_FILTER_TEXT) { rxQueue.push_back(rxMessage); }
        }
    }
}
