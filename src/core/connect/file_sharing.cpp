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

    Message message = createFileMessage(file);

    esp_err_t response;
    sendStatus = STARTED;

    drawMainBorderWithTitle("SEND FILE");
    padprintln("");
    padprintln("Sending...");

    delay(100);

    while (file.available()) {
        if (check(EscPress)) sendStatus = ABORTED;

        if (sendStatus == ABORTED || sendStatus == FAILED) {
            message.header.flags |= MSG_FLAG_DONE;
            message.header.dataSize = 0;
            esp_now_send(dstAddress, (uint8_t *)&message, sizeof(message));
            displayError("Error sending file");
            break;
        }

        size_t bytesRead = file.readBytes(message.body.data, ESP_DATA_SIZE);
        message.header.dataSize = bytesRead;
        message.body.bytesSent = min(message.body.bytesSent + bytesRead, message.body.totalBytes);
        if (message.body.bytesSent == message.body.totalBytes) { message.header.flags |= MSG_FLAG_DONE; }

        response = esp_now_send(dstAddress, (uint8_t *)&message, sizeof(message));
        if (response != ESP_OK) {
            Serial.printf("Send file response: %s\n", esp_err_to_name(response));
            sendStatus = FAILED;
        }

        progressHandler(file.position(), file.size(), "Sending...");
        delay(100);
    }

    if (message.body.bytesSent == message.body.totalBytes) displaySuccess("File sent");

    file.close();
    delay(1000);
}

void FileSharing::receiveFile() {
    drawMainBorderWithTitle("RECEIVE FILE");
    padprintln("");
    padprintln("Waiting...");

    recvFileName = "";
    recvQueue = {};
    recvStatus = CONNECTING;

    if (!beginEspnow()) return;

    delay(100);

    while (1) {
        if (check(EscPress)) recvStatus = ABORTED;

        if (recvStatus == ABORTED || recvStatus == FAILED) {
            displayError("Error receiving file");
            break;
        }
        if (recvStatus == SUCCESS) {
            displaySuccess("File received");
            break;
        }

        if (!recvQueue.empty()) {
            Message recvFileMessage = recvQueue.front();
            recvQueue.erase(recvQueue.begin());

            // Filter non-file messages.
            if (recvFileMessage.header.type != MSG_TYPE_FILE) { continue; }

            progressHandler(recvFileMessage.body.bytesSent, recvFileMessage.body.totalBytes, "Receiving...");

            if (!appendToFile(recvFileMessage)) {
                recvStatus = FAILED;
                Serial.println("Failed appending to file");
            }
            if (recvFileMessage.header.flags & MSG_FLAG_DONE) {
                Serial.println("Recv done");
                recvStatus = recvFileMessage.body.bytesSent == recvFileMessage.body.totalBytes ? SUCCESS : FAILED;
            }
        }

        delay(100);
    }

    delay(1000);

    if (recvStatus == SUCCESS) {
        drawMainBorderWithTitle("RECEIVE FILE");
        padprintln("");
        padprintln("File received: ");
        padprintln(recvFileName);
        padprintln("\n");
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

    if (recvFileName == "") createFilename(fs, fileMessage);

    File file = (*fs).open(recvFileName, FILE_APPEND);
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

    recvFileName = messageFilepath + "/" + filename + ext;
}
