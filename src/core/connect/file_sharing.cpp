#include "file_sharing.h"
#include "core/display.h"

FileSharing::FileSharing() {}

void FileSharing::sendFile() {
    drawMainBorderWithTitle("SEND FILE");

    if (!beginSend()) return;

    File file = selectFile();
    if (!file) {
        displayError("Error selecting file");
        delay(1000);
        return;
    }

    Message txMessage = createFileMessage(file);

    esp_err_t response;

    txState = STARTED;
    txSeq.reset();
    txSeq.filename = file.path();
    txSeq.size = file.size();
    txSeq.seqId = random(0, 0xFFFF);

    drawMainBorderWithTitle("SEND FILE");
    padprintln("");
    padprintln("Sending...");

    delay(100);

    while (file.available()) {
        if (check(EscPress)) txState = ABORTED;

        if (txState == ABORTED || txState == FAILED) {
            txMessage.header.flags |= MSG_FLAG_DONE;
            txMessage.header.dataSize = 0;
            esp_now_send(dstAddress, (uint8_t *)&txMessage, ESP_NOW_MAX_DATA_LEN);
            displayError("Error sending file");
            break;
        }

        size_t bytesRead = file.readBytes(txMessage.body.data, ESP_DATA_SIZE);
        txSeq.dataCounter = min(txSeq.dataCounter + bytesRead, txSeq.size);
        txMessage.header.dataSize = bytesRead;
        txMessage.body.seqId = txSeq.seqId;
        txMessage.body.totalBytes = txSeq.size;
        txMessage.body.bytesSent = txSeq.dataCounter;

        if (txSeq.dataCounter == txSeq.size) {
            txMessage.header.flags |= MSG_FLAG_DONE; // mark as completed
        }

        response = esp_now_send(dstAddress, (uint8_t *)&txMessage, ESP_NOW_MAX_DATA_LEN);
        if (response != ESP_OK) {
            Serial.printf("Send file response: %s\n", esp_err_to_name(response));
            txState = FAILED;
        }

        progressHandler(file.position(), txSeq.size, "Sending...");
        delay(100);
    }

    if (txSeq.dataCounter == txSeq.size) displaySuccess("File sent");

    file.close();
    delay(1000);
}

void FileSharing::receiveFile() {
    drawMainBorderWithTitle("RECEIVE FILE");
    padprintln("");
    padprintln("Waiting...");

    rxSeq.reset();
    recvQueue = {};
    rxState = CONNECTING;

    if (!beginEspnow()) return;

    delay(100);

    while (1) {
        if (check(EscPress)) rxState = ABORTED;

        if (rxState == ABORTED || rxState == FAILED) {
            displayError("Error receiving file");
            break;
        }
        if (rxState == SUCCESS) {
            displaySuccess("File received");
            break;
        }

        if (!recvQueue.empty()) {
            Message rxMessage = recvQueue.front();
            recvQueue.erase(recvQueue.begin());

            // Filter non-file messages.
            if (rxMessage.header.type != MSG_TYPE_FILE) {
                continue;
            }

            // If we have no active tranfer...
            if (rxSeq.filename == "") {
                // Safety check - ensure we got first packet in a sequence.
                if (rxMessage.body.bytesSent != rxMessage.header.dataSize) {
                    continue; // Skip initiated tranfers.
                }

                // First packet on sequence - keep mac, sequence & size
                memcpy(rxSeq.mac, rxMessage.mac, sizeof(rxSeq.mac));
                rxSeq.seqId = rxMessage.body.seqId;
                rxSeq.size = rxMessage.body.totalBytes;
            }

            // Safety check - ensure all data from same peer.
            // That's for case if we share thru broadcast.
            // Without this check another Bruce can break everything. :)
            if (memcmp(rxMessage.mac, rxSeq.mac, sizeof(rxSeq.mac)) != 0) {
                continue; // Skip everything from other peers.
            }

            // Safety check - ensure all data from same sequence.
            if (rxMessage.body.seqId != rxSeq.seqId) {
                continue; // Skip everything from other sequences.
            }

            // Safety check - ensure file size is same.
            // It can't suddently change during tranfer.
            if (rxMessage.body.totalBytes != rxSeq.size) {
                displayError("Err recv - file size mismatch.");
                break;
            }

            rxSeq.dataCounter += rxMessage.header.dataSize; // increment counter

            // Safety check - ensure we receive full sequence.
            if (rxSeq.dataCounter != rxMessage.body.bytesSent) {
                displayError("Err recv - lost packet.");
                break;
            }

            progressHandler(rxSeq.dataCounter, rxSeq.size, "Receiving...");

            if (!appendToFile(rxMessage)) {
                rxState = FAILED;
                Serial.println("Failed appending to file");
            }

            if (rxMessage.header.flags & MSG_FLAG_DONE) {
                Serial.println("Recv done");
                rxState = rxMessage.body.bytesSent == rxSeq.size ? SUCCESS : FAILED;
            }
        }

        delay(100);
    }

    delay(1000);

    if (rxState == SUCCESS) {
        drawMainBorderWithTitle("RECEIVE FILE");
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

    file.write((const uint8_t *)fileMessage.body.data, fileMessage.header.dataSize);
    file.close();

    return true;
}

void FileSharing::createFilename(FS *fs, FileSharing::Message fileMessage) {
    String messageFilename = String(fileMessage.body.filename);
    String messageFilepath = String(fileMessage.body.filepath);

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
