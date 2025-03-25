#include "file_sharing.h"
#include "core/display.h"

FileSharing::FileSharing() { rxQueueFilter = MSG_FILTER_FILE; }

void FileSharing::sendFile() {
    displayBanner(APP_MODE_FILESEND);

    if (!beginSend()) return;

    File file = selectFile();
    if (!file) {
        displayError("Error selecting file");
        delay(1000);
        return;
    }

    Message txMessage = createFileMessage(file);

    esp_err_t response;

    txState = STATE_STARTED;
    txSeq.reset();
    txSeq.filename = file.path();
    txSeq.size = file.size();
    txSeq.seqId = random(0, 0xFFFF);

    displayBanner(APP_MODE_FILESEND);
    padprintln("");
    padprintln("Sending...");

    delay(100);

    while (file.available()) {
        if (check(EscPress)) txState = STATE_BREAK;

        if (txState == STATE_BREAK || txState == STATE_FAILED) {
            txMessage.head.flags |= MSG_FLAG_DONE;
            txMessage.head.dataSize = 0;
            esp_now_send(dstAddress, (uint8_t *)&txMessage, ESP_NOW_MAX_DATA_LEN);
            displayError("Error sending file");
            break;
        }

        // First packet - file head, otherwise - file body.
        if (txSeq.dataCounter > 0) { txMessage.head.type = MSG_TYPE_FILEBODY; }

        size_t bytesRead = file.readBytes(txMessage.getData(), txMessage.maxData());
        txSeq.dataCounter = min(txSeq.dataCounter + bytesRead, txSeq.size);
        txMessage.head.dataSize = bytesRead;
        txMessage.seq.seqId = txSeq.seqId;
        txMessage.seq.totalBytes = txSeq.size;
        txMessage.seq.bytesSent = txSeq.dataCounter;

        if (txSeq.dataCounter == txSeq.size) {
            txMessage.head.flags |= MSG_FLAG_DONE; // mark as completed
        }

        response = esp_now_send(dstAddress, (uint8_t *)&txMessage, ESP_NOW_MAX_DATA_LEN);
        if (response != ESP_OK) {
            Serial.printf("Send file response: %s\n", esp_err_to_name(response));
            txState = STATE_FAILED;
        }

        progressHandler(file.position(), txSeq.size, "Sending...");
        delay(100);
    }

    if (txSeq.dataCounter == txSeq.size) displaySuccess("File sent");

    file.close();
    delay(1000);
}

void FileSharing::receiveFile() {
    displayBanner(APP_MODE_FILERECV);
    padprintln("");
    padprintln("Waiting...");

    rxSeq.reset();
    rxQueue = {};
    rxState = STATE_CONNECTING;

    if (!beginEspnow()) return;

    delay(100);

    while (1) {
        if (check(EscPress)) rxState = STATE_BREAK;

        if (rxState == STATE_BREAK || rxState == STATE_FAILED) {
            displayError("Error receiving file");
            break;
        }
        if (rxState == STATE_DONE) {
            displaySuccess("File received");
            break;
        }

        if (!rxQueue.empty()) {
            Message rxMessage = rxQueue.front();
            rxQueue.erase(rxQueue.begin());

            // Filter non-file messages.
            if (rxMessage.head.type != MSG_TYPE_FILEHEAD && rxMessage.head.type != MSG_TYPE_FILEBODY) {
                continue;
            }

            // If we have no active tranfer...
            if (rxSeq.filename == "") {
                // We should get file head first...
                if (rxMessage.head.type != MSG_TYPE_FILEHEAD) {
                    continue; // Skip initiated tranfers.
                }

                // Safety check - ensure we got first packet in a sequence.
                if (rxMessage.seq.bytesSent != rxMessage.head.dataSize) {
                    continue; // Skip initiated tranfers.
                }

                // First packet on sequence - keep mac, sequence & size
                memcpy(rxSeq.mac, rxMessage.mac, sizeof(rxSeq.mac));
                rxSeq.seqId = rxMessage.seq.seqId;
                rxSeq.size = rxMessage.seq.totalBytes;
            }

            // Safety check - ensure all data from same peer.
            // That's for case if we share thru broadcast.
            // Without this check another Bruce can break everything. :)
            if (memcmp(rxMessage.mac, rxSeq.mac, sizeof(rxSeq.mac)) != 0) {
                continue; // Skip everything from other peers.
            }

            // Safety check - ensure all data from same sequence.
            if (rxMessage.seq.seqId != rxSeq.seqId) {
                continue; // Skip everything from other sequences.
            }

            // Safety check - ensure file size is same.
            // It can't suddently change during tranfer.
            if (rxMessage.seq.totalBytes != rxSeq.size) {
                displayError("Err recv - file size mismatch.");
                break;
            }

            rxSeq.dataCounter += rxMessage.head.dataSize; // increment counter

            // Safety check - ensure we receive full sequence.
            if (rxSeq.dataCounter != rxMessage.seq.bytesSent) {
                displayError("Err recv - lost packet.");
                break;
            }

            progressHandler(rxSeq.dataCounter, rxSeq.size, "Receiving...");

            if (!appendToFile(rxMessage)) {
                rxState = STATE_FAILED;
                Serial.println("Failed appending to file");
            }

            if (rxMessage.head.flags & MSG_FLAG_DONE) {
                Serial.println("Recv done");
                rxState = rxMessage.seq.bytesSent == rxSeq.size ? STATE_DONE : STATE_FAILED;
            }
        }

        delay(100);
    }

    delay(1000);

    if (rxState == STATE_DONE) {
        displayBanner(APP_MODE_FILERECV);
        padprintln("");
        padprintln("File received: ");
        padprintln(rxSeq.filename);
        padprintln("\n");
        padprintf("Bytes: %d\n", rxSeq.size);
        padprintln("");
        padprintln("Press any key to leave");
        while (!check(AnyKeyPress)) delay(80);
    }
}

File FileSharing::selectFile() {
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

bool FileSharing::appendToFile(FileSharing::Message fileMessage) {
    FS *fs;
    if (!getFsStorage(fs)) return false;

    if (rxSeq.filename == "") createFilename(fs, fileMessage);

    File file = (*fs).open(rxSeq.filename, FILE_APPEND);
    if (!file) return false;

    file.write((const uint8_t *)fileMessage.getData(), fileMessage.head.dataSize);
    file.close();

    return true;
}

void FileSharing::createFilename(FS *fs, FileSharing::Message fileMessage) {
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

void FileSharing::displayBanner(AppMode mode) {
    switch (mode) {
        case APP_MODE_FILERECV: drawMainBorderWithTitle("RECEIVE COMMANDS"); break;
        case APP_MODE_FILESEND: drawMainBorderWithTitle("SEND COMMANDS"); break;
        default: drawMainBorderWithTitle("UNKNOWN MODE"); break;
    }

    padprintln("");
}
