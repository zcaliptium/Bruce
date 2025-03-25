#include "esp_bruce.h"
#include "core/display.h"
#include "core/mykeyboard.h"

std::vector<Option> peerOptions;

EspBruce::EspBruce() {}

bool EspBruce::beginSend() {
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

void EspBruce::displayBanner(AppMode mode) {
    switch (mode) {
        case APP_MODE_CMDSRECV: drawMainBorderWithTitle("RECEIVE COMMANDS"); break;
        case APP_MODE_CMDSSEND: drawMainBorderWithTitle("SEND COMMANDS"); break;
        case APP_MODE_FILERECV: drawMainBorderWithTitle("RECEIVE FILE"); break;
        case APP_MODE_FILESEND: drawMainBorderWithTitle("SEND FILE"); break;
        case APP_MODE_TEXTRECV: drawMainBorderWithTitle("RECEIVE TEXT"); break;
        case APP_MODE_TEXTSEND: drawMainBorderWithTitle("SEND TEXT"); break;

        default: drawMainBorderWithTitle("UNKNOWN MODE"); break;
    }

    padprintln("");
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

String EspBruce::inputText(String msg) {
    String text;

    // debounce
    tft.fillScreen(bruceConfig.bgColor);
    delay(500);

    text = keyboard("", ESP_RAWDATA_SIZE, msg);

    if (text == "doom") {
        text =
            "music_player "
            "doom:d=4,o=5,b=112:16e4,16e4,16e,16e4,16e4,16d,16e4,16e4,16c,16e4,16e4,16a#4,16e4,16e4,16b4,16c,"
            "16e4,16e4,16e,16e4,16e4,16d,16e4,16e4,16c,16e4,16e4,a#4,16p,16e4,16e4,16e,16e4,16e4,16d,16e4,"
            "16e4,16c,16e4,16e4,16a#4,16e4,16e4,16b4,16c,16e4,16e4,16e,16e4,16e4,16d,16e4,16e4,16c,16e4,16e4,"
            "a#4,16p,16a4,16a4,16a,16a4,16a4,16g,16a4,16a4,16f,16a4,16a4,16d#,16a4,16a4,16e,16f,16a4,16a4,"
            "16a,16a4,16a4,16g,16a4,16a4,16f,16a4,16a4,d#";
    }

    return text;
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

bool EspBruce::isSeqMsgType(uint8_t type) {
    return type == MSG_TYPE_FILEHEAD || type == MSG_TYPE_FILEBODY || type == MSG_TYPE_CMDLONG ||
           type == MSG_TYPE_TEXTLONG;
}

String EspBruce::msgTypeToStr(uint8_t type) {
    switch (type) {
        case MSG_TYPE_NOP: return "MSG_TYPE_NOP";
        case MSG_TYPE_PING: return "MSG_TYPE_PING";
        case MSG_TYPE_PONG: return "MSG_TYPE_PONG";
        case MSG_TYPE_FILEHEAD: return "MSG_TYPE_FILEHEAD";
        case MSG_TYPE_FILEBODY: return "MSG_TYPE_FILEBODY";
        case MSG_TYPE_CMDTINY: return "MSG_TYPE_CMDTINY";
        case MSG_TYPE_CMDLONG: return "MSG_TYPE_CMDLONG";
        case MSG_TYPE_TEXTTINY: return "MSG_TYPE_TEXTTINY";
        case MSG_TYPE_TEXTLONG: return "MSG_TYPE_TEXTLONG";
        default: return "<invalid>";
    }
}

String EspBruce::stateToStr(State type) {
    switch (type) {
        case STATE_STOP: return "STATE_STOP";
        case STATE_WAIT: return "STATE_WORK";
        case STATE_WORK: return "STATE_WORK";
        case STATE_BREAK: return "STATE_BREAK";
        case STATE_OK: return "STATE_OK";
        case STATE_ERR: return "STATE_ERR";
        case STATE_ERR_ARG: return "STATE_ERR_ARG";
        case STATE_ERR_APPEND: return "STATE_ERR_APPEND";
        case STATE_ERR_PKTLOST: return "STATE_ERR_PKTLOST";
        case STATE_ERR_TIMEOUT: return "STATE_ERR_TIMEOUT";
        case STATE_ERR_FILEPICK: return "STATE_ERR_FILEPICK";
        case STATE_ERR_PARSE: return "STATE_ERR_PARSE";
        default: return "<UNKNOWN>";
    }
}

void EspBruce::printMessage(Message message) {
    delay(100);

    Serial.println("Message Details:");

    // Print message head
    Serial.println("Version: " + String(message.head.protocolVer));
    Serial.println("Type: " + msgTypeToStr(message.head.type));
    Serial.println("Flags: " + String(message.head.flags));
    Serial.println("Data Size: " + String(message.head.dataSize));
    Serial.println("");

    // for MSG_TYPE_FILEHEAD & MSG_TYPE_CMDTINY
    if (isSeqMsgType(message.head.type)) {
        Serial.println("Seq Handle: " + String(message.seq.handle));
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
        Serial.println("EspBruce - send success");
    } else {
        Serial.println("EspBruce - send fail");
    }
}

void EspBruce::onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    // there should be place for head & valid signature
    if (len < sizeof(MessageHeader) || strncmp((const char *)incomingData, ESP_BRUCE_ID, 5) != 0) {
        return; // ignore non-Bruce packets
    }

    Message rxMsg;

    // Use reinterpret_cast and copy assignment
    const Message *incomingMessage = reinterpret_cast<const Message *>(incomingData);
    rxMsg = *incomingMessage;                  // Use copy assignment
    rxMsg.zero = '\0';                         // copy assignment can rewrite terminator
    memcpy(rxMsg.mac, mac, sizeof(rxMsg.mac)); // keep mac for queued messages.

    printMessage(rxMsg);

    switch (rxMsg.head.type) {
        case MSG_TYPE_NOP: return; // do nothing

        case MSG_TYPE_PING: {
            sendPong(mac);
            return;
        }

        case MSG_TYPE_PONG: {
            peerOptions.push_back({macToString(mac).c_str(), [=]() { setDstAddress(mac); }});
            return;
        }
    }

    // filter packets that come into rxQueue.
    if (rxSeq.type != SEQ_TYPE_INVALID && (rxSeq.state == STATE_WAIT || rxSeq.state == STATE_WORK)) {
        switch (rxSeq.type) {
            case SEQ_TYPE_FILE: {
                bool isHead = rxMsg.head.type == MSG_TYPE_FILEHEAD;
                bool isBody = rxMsg.head.type == MSG_TYPE_FILEBODY;

                if (rxSeq.state == STATE_WAIT) {
                    if (isHead) { rxQueue.push_back(rxMsg); }
                } else {
                    if (isBody) { rxQueue.push_back(rxMsg); }
                }
                return;
            }
            case SEQ_TYPE_SERIAL: {
                bool isTiny = rxMsg.head.type == MSG_TYPE_CMDTINY;
                bool isLong = rxMsg.head.type == MSG_TYPE_CMDLONG;
                if (isTiny || isLong) { rxQueue.push_back(rxMsg); }
                return;
            }
            case SEQ_TYPE_TEXT: {
                bool isTiny = rxMsg.head.type == MSG_TYPE_TEXTTINY;
                bool isLong = rxMsg.head.type == MSG_TYPE_TEXTLONG;
                if (isTiny || isLong) { rxQueue.push_back(rxMsg); }
                return;
            }

            default: return;
        }
    }
}

bool EspBruce::appendToFile(SeqTransfer &seq, EspBruce::Message fileMsg) {
    FS *fs;
    if (!getFsStorage(fs)) return false;

    if (seq.filename == "") createFilename(fs, fileMsg);

    File file = (*fs).open(seq.filename, FILE_APPEND);
    if (!file) return false;

    file.write((const uint8_t *)fileMsg.getData(), fileMsg.head.dataSize);
    file.close();

    return true;
}

void EspBruce::createFilename(FS *fs, EspBruce::Message fileMessage) {
    String messageFilename = String(fileMessage.fhb.filename);
    String messageFilepath = String(fileMessage.fhb.filepath);

    String filename = messageFilename.substring(0, messageFilename.lastIndexOf("."));
    String ext = messageFilename.substring(messageFilename.lastIndexOf("."));

    Serial.println("Creating filename");
    Serial.print("Path: ");
    Serial.println(messageFilepath);
    Serial.print("Name: ");
    Serial.println(filename);
    Serial.print("Ext: ");
    Serial.println(ext);

    if (!(*fs).exists(messageFilepath)) (*fs).mkdir(messageFilepath);
    if ((*fs).exists(messageFilepath + "/" + filename + ext)) {
        int i = 1;
        filename += "_";
        while ((*fs).exists(messageFilepath + "/" + filename + String(i) + ext)) i++;
        filename += String(i);
    }

    rxSeq.filename = messageFilepath + "/" + filename + ext;
}

void EspBruce::sendSequence(SeqTransfer &seq, File file, const char *buf, size_t buflen) {
    size_t bytesRead;
    Message txMessage;

    switch (seq.type) {
        case SEQ_TYPE_TEXT:
            txMessage.head.type = seq.size > ESP_RAWDATA_SIZE ? MSG_TYPE_CMDLONG : MSG_TYPE_CMDTINY;
            break;
        case SEQ_TYPE_SERIAL:
            txMessage.head.type = seq.size > ESP_RAWDATA_SIZE ? MSG_TYPE_TEXTLONG : MSG_TYPE_TEXTTINY;
            break;
        case SEQ_TYPE_FILE:
            if (!file) {
                seq.state = STATE_ERR_ARG;
                return;
            }
            txMessage = createFileMessage(file);
            break;
        // unknow type
        default: seq.state = STATE_ERR_ARG; return;
    }

    // reset
    seq.state = STATE_WORK;
    seq.handle = random(0, 0xFFFF);
    seq.size = file ? file.size() : buflen;
    seq.dataCounter = 0;

    while (seq.dataCounter < seq.size) {
        if (check(EscPress)) {
            // break sequence for clients
            if (isSeqMsgType(txMessage.head.type)) {
                txMessage.head.flags |= MSG_FLAG_DONE;
                txMessage.head.dataSize = 0;
                esp_now_send(dstAddress, (uint8_t *)&txMessage, ESP_NOW_MAX_DATA_LEN);
            }

            seq.state = STATE_BREAK;
            break;
        }

        if (seq.type == SEQ_TYPE_FILE) {
            // First packet - file head, otherwise - file body.
            txMessage.head.type = seq.dataCounter == 0 ? MSG_TYPE_FILEHEAD : MSG_TYPE_FILEBODY;
        }

        if (file) {
            bytesRead = file.readBytes(txMessage.getData(), txMessage.maxData());
        } else {
            bytesRead = min(txMessage.maxData(), seq.size - seq.dataCounter);
            memcpy(txMessage.getData(), buf + seq.dataCounter, bytesRead);
        }

        seq.dataCounter = min(seq.dataCounter + bytesRead, seq.size);

        // fill
        txMessage.head.dataSize = bytesRead;
        if (isSeqMsgType(txMessage.head.type)) {
            txMessage.seq.handle = seq.handle;
            txMessage.seq.totalBytes = seq.size;
            txMessage.seq.bytesSent = seq.dataCounter;
        }

        if (seq.dataCounter == seq.size) {
            txMessage.head.flags |= MSG_FLAG_DONE; // mark as completed
            seq.state = STATE_OK;
        }

        printMessage(txMessage);

        esp_err_t response = esp_now_send(dstAddress, (uint8_t *)&txMessage, ESP_NOW_MAX_DATA_LEN);
        if (response != ESP_OK) {
            Serial.printf("sendSequence - esp_now_send: %s\n", esp_err_to_name(response));
            seq.state = STATE_ERR;
            break;
        }

        progressHandler(seq.dataCounter, seq.size, "Sending...");
        delay(100);
    }
}

void EspBruce::recvSequence(SeqTransfer &seq, File file, char *buf, size_t buflen) {
    bool isTiny = false;
    int pktAttempts = 0;
    Message msg;

    if (seq.type != SEQ_TYPE_FILE && buf == nullptr) {
        seq.state = STATE_ERR_ARG;
        goto ENDRECV;
    }

    seq.state = STATE_WAIT;
    Serial.println("recvSequence - " + stateToStr(seq.state));

    while (1) {
        if (check(EscPress)) {
            seq.state = STATE_BREAK;
            goto ENDRECV;
        }

        if (seq.state == STATE_WORK && rxQueue.empty()) {
            pktAttempts++;
            if (pktAttempts > 20) {
                seq.state = STATE_ERR_TIMEOUT;
                goto ENDRECV;
            }
            delay(100);
            continue;
        }

        while (!rxQueue.empty()) {
            msg = rxQueue.front();
            rxQueue.erase(rxQueue.begin());

            // Necessary message filtering.
            if (seq.state == STATE_WAIT) {
                if (msg.head.type == MSG_TYPE_CMDTINY || msg.head.type == MSG_TYPE_TEXTTINY) {
                    memcpy(seq.mac, msg.mac, sizeof(seq.mac));
                    seq.size = seq.dataCounter = msg.head.dataSize;
                    seq.state = STATE_OK;
                    memcpy(buf, msg.getData(), seq.size);
                    goto ENDRECV;
                }

                // Safety check - ensure we got first packet in a sequence.
                if (msg.seq.bytesSent != msg.head.dataSize) {
                    continue; // Skip initiated tranfers.
                }

                // Safety check - ensure we got sequence that fit to buffer.
                if (buf != nullptr && msg.seq.totalBytes > buflen) {
                    continue; // Skip too big sequences.
                }

                // First packet on sequence - keep mac, identifier & size
                memcpy(seq.mac, msg.mac, sizeof(seq.mac));
                seq.handle = msg.seq.handle;
                seq.size = msg.seq.totalBytes;
                seq.state = STATE_WORK;
                Serial.println("recvSequence - " + stateToStr(seq.state));
            } else {
                // Safety check - ensure all data from same peer.
                // That's for case if we share thru broadcast.
                // Without this check another Bruce can break everything. :)
                if (memcmp(msg.mac, seq.mac, sizeof(seq.mac)) != 0) {
                    continue; // Skip everything from other peers.
                }

                // Safety check - ensure all data from same sequence.
                if (msg.seq.handle != seq.handle) {
                    continue; // Skip everything from other sequences.
                }
            }

            if (seq.type == SEQ_TYPE_FILE) {
                if (!appendToFile(seq, msg)) {
                    seq.state = STATE_ERR_APPEND;
                    goto ENDRECV;
                }
            } else {
                memcpy(buf + seq.dataCounter, msg.getData(), msg.head.dataSize);
            }

            seq.dataCounter += msg.head.dataSize; // increment counter
            pktAttempts = 0;                      // reset counter as we got packet

            progressHandler(seq.dataCounter, seq.size, "Receiving...");

            bool isDone = msg.head.flags & MSG_FLAG_DONE;
            bool isFull = seq.dataCounter == seq.size;

            // Safety check - ensure we receive full sequence.
            if (isDone && isFull) {
                seq.state = STATE_OK;
                goto ENDRECV;
            } else if ((isDone && !isFull) || (!isDone && isFull)) {
                seq.state = STATE_ERR_PKTLOST;
                goto ENDRECV;
            }
        }

        delay(100);
    }

ENDRECV:
    Serial.println("recvSequence - " + stateToStr(seq.state));
}

void EspBruce::run(AppMode mode) {
    displayBanner(mode);

    bool isSendMode;
    bool isAutoMode;

    switch (mode) {
        case APP_MODE_CMDSSEND: isSendMode = true; break;
        case APP_MODE_FILESEND: isSendMode = true; break;
        case APP_MODE_TEXTSEND: isSendMode = true; break;
        default: isSendMode = false;
    }

    if (isSendMode) {
        if (!beginSend()) return;
    } else {
        if (!beginEspnow()) return;
    }

    if (!isSendMode) {
        options = {
            {"Auto accept", [&]() { isAutoMode = true; } },
            {"Semi auto",   [&]() { isAutoMode = false; }},
        };
        loopOptions(options);
    }

    State runState = STATE_WORK;

    delay(100);

    while (1) {
        if (check(EscPress)) {
            displayInfo("Aborting...");
            runState = STATE_BREAK;
            break;
        }

        if (check(SelPress)) { runState = STATE_WORK; }

        if (runState == STATE_WORK) {
            if (isSendMode) {
                displayInfo("Sending...");
                sendOneThingy(mode);
                displayStatus(mode, txSeq);
            } else {
                displayInfo("Waiting...");
                recvOneThingy(mode);
                displayStatus(mode, rxSeq);
            }

            runState = isAutoMode ? STATE_WORK : STATE_STOP;
        }

        delay(100);
    }

    delay(1000);
}

void EspBruce::sendOneThingy(AppMode mode) {
    txSeq.reset();
    File file;
    bool isPickFile = false;

    options = {};

    switch (mode) {
        case APP_MODE_CMDSSEND: {
            txSeq.type = SEQ_TYPE_SERIAL;
            options.push_back({"Input command", [&]() { txSeq.command = inputText("Serial Command"); }});
            break;
        }

        case APP_MODE_FILESEND: {
            txSeq.type = SEQ_TYPE_FILE;
            break;
        }

        // otwerwise - APP_MODE_TEXTSEND
        default: {
            txSeq.type = SEQ_TYPE_TEXT;
            options.push_back({"Input text", [&]() { txSeq.command = inputText("Text message"); }});
            break;
        }
    }

    options.push_back({"Send file", [&]() { isPickFile = true; }});

    loopOptions(options);

    if (isPickFile) {
        file = selectFile();
        if (!file) {
            txSeq.state = STATE_ERR_FILEPICK;
            return;
        }
        txSeq.filename = file.path();
    } else if (txSeq.command == "") {
        Serial.println("Nothing to send");
        return;
    }

    sendSequence(txSeq, file, file ? nullptr : txSeq.command.c_str(), file ? 0 : txSeq.command.length());
}

void EspBruce::recvOneThingy(AppMode mode) {
    File file;
    char buf[ESP_CMDLONG_SIZE + 1];
    memset(buf, 0, sizeof(buf));

    rxQueue.clear();
    rxSeq.reset(); //

    switch (mode) {
        case APP_MODE_CMDSRECV: rxSeq.type = SEQ_TYPE_SERIAL; break;
        case APP_MODE_FILERECV: rxSeq.type = SEQ_TYPE_FILE; break;
        // APP_MODE_TEXTRECV
        default: rxSeq.type = SEQ_TYPE_TEXT; break;
    }

    if (rxSeq.type == SEQ_TYPE_SERIAL || rxSeq.type == SEQ_TYPE_TEXT) {
        recvSequence(rxSeq, file, buf, sizeof(buf));
        rxSeq.command = "";
        rxSeq.command += buf;

        if (rxSeq.type == SEQ_TYPE_SERIAL && rxSeq.state == STATE_OK) {
            if (!serialCli.parse(rxSeq.command)) { rxSeq.state = STATE_ERR_PARSE; }
        }
    } else if (rxSeq.type == SEQ_TYPE_FILE) {
        recvSequence(rxSeq, file, nullptr, 0);
    }
}

void EspBruce::displayStatus(AppMode mode, SeqTransfer &seq) {
    displayBanner(mode);

    if (seq.state == STATE_OK) {
        padprintf("State - %s (%d b)\n", stateToStr(STATE_OK), seq.size);

        if (seq.filename != "") {
            padprintln("File: " + seq.filename);
        } else {
            padprintln("Data: " + seq.command);
        }

    } else {
        padprintf("State - %s\n", stateToStr(seq.state));
    }

    padprintln("\n");
    padprintln("Press [OK] to action [ESC] to stop");
}
