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

    Message message = createFileMessage(file);

    esp_err_t response;
    txState = STATE_STARTED;

    displayBanner(APP_MODE_FILESEND);
    padprintln("");
    padprintln("Sending...");

    delay(100);

    while (file.available()) {
        if (check(EscPress)) txState = STATE_BREAK;

        if (txState == STATE_BREAK || txState == STATE_FAILED) {
            message.head.flags |= MSG_FLAG_DONE;
            message.head.dataSize = 0;
            esp_now_send(dstAddress, (uint8_t *)&message, ESP_NOW_MAX_DATA_LEN);
            displayError("Error sending file");
            break;
        }

        size_t bytesRead = file.readBytes(message.getData(), message.maxData());
        message.head.dataSize = bytesRead;
        message.seq.bytesSent = min(message.seq.bytesSent + bytesRead, message.seq.totalBytes);

        if (message.seq.bytesSent == message.seq.totalBytes) {
            message.head.flags |= MSG_FLAG_DONE; // mark as completed
        }

        response = esp_now_send(dstAddress, (uint8_t *)&message, ESP_NOW_MAX_DATA_LEN);
        if (response != ESP_OK) {
            Serial.printf("Send file response: %s\n", esp_err_to_name(response));
            txState = STATE_FAILED;
        }

        progressHandler(file.position(), file.size(), "Sending...");
        delay(100);
    }

    if (message.seq.bytesSent == message.seq.totalBytes) displaySuccess("File sent");

    file.close();
    delay(1000);
}

void FileSharing::receiveFile() {
    displayBanner(APP_MODE_FILERECV);
    padprintln("");
    padprintln("Waiting...");

    rxFileName = "";
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
            Message recvFileMessage = rxQueue.front();
            rxQueue.erase(rxQueue.begin());

            // Filter non-file messages.
            if (recvFileMessage.head.type != MSG_TYPE_FILEHEAD) { continue; }

            progressHandler(recvFileMessage.seq.bytesSent, recvFileMessage.seq.totalBytes, "Receiving...");

            if (!appendToFile(recvFileMessage)) {
                rxState = STATE_FAILED;
                Serial.println("Failed appending to file");
            }
            if (recvFileMessage.head.flags & MSG_FLAG_DONE) {
                Serial.println("Recv done");
                rxState = recvFileMessage.seq.bytesSent == recvFileMessage.seq.totalBytes ? STATE_DONE
                                                                                          : STATE_FAILED;
            }
        }

        delay(100);
    }

    delay(1000);

    if (rxState == STATE_DONE) {
        displayBanner(APP_MODE_FILERECV);
        padprintln("");
        padprintln("File received: ");
        padprintln(rxFileName);
        padprintln("\n");
        padprintln("Press any key to leave");
        while (!check(AnyKeyPress)) delay(80);
    }
}

bool FileSharing::appendToFile(FileSharing::Message fileMessage) {
    FS *fs;
    if (!getFsStorage(fs)) return false;

    if (rxFileName == "") createFilename(fs, fileMessage);

    File file = (*fs).open(rxFileName, FILE_APPEND);
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

    rxFileName = messageFilepath + "/" + filename + ext;
}
