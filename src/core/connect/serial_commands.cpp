#include "serial_commands.h"
#include "core/display.h"
#include "core/mykeyboard.h"

EspSerialCmd::EspSerialCmd() { rxQueueFilter = MSG_FILTER_SERIAL; }

void EspSerialCmd::sendCommands() {
    displayBanner(APP_MODE_CMDSSEND);
    padprintln("Waiting...");

    if (!beginSend()) return;

    txSeq.reset();
    txState = STATE_CONNECTING;

    delay(100);

    while (1) {
        if (check(EscPress)) {
            displayInfo("Aborting...");
            txState = STATE_BREAK;
            break;
        }

        if (check(SelPress)) { txState = STATE_CONNECTING; }

        if (txState == STATE_CONNECTING) {
            txState = sendOneCommand() ? STATE_DONE : STATE_FAILED; // send one
        }

        if (txState == STATE_FAILED) {
            displaySentError();
            txState = STATE_WAITING;
        }

        if (txState == STATE_DONE) {
            displaySentCommand(txSeq.cmd.c_str());
            txState = STATE_WAITING;
        }

        delay(100);
    }

    delay(1000);
}

bool EspSerialCmd::sendOneCommand() {
    Message txMessage;
    File file;
    char buf[ESP_CMDLONG_SIZE];
    bool isInputCmd = false;
    bool isExecFile = false;

    txSeq.seqId = random(0, 0xFFFF);
    txSeq.size = min(txSeq.cmd.length(), size_t(ESP_CMDLONG_SIZE));
    txSeq.dataCounter = 0;

    options = {
        {"Input cmd", [&]() { isInputCmd = true; }},
        {"Exec file", [&]() { isExecFile = true; }},
    };

    if (txSeq.cmd.length() > 0) {
        options.push_back({"Repeat cmd", [&]() {}});
    }

    loopOptions(options);

    if (isInputCmd) {
        inputCommand();
    } else if (isExecFile) {
        file = selectFile();
        if (!file) {
            displayError("Error selecting file");
            delay(1000);
            return false;
        }

        if (file.size() > ESP_CMDLONG_SIZE) {
            displayError("File is too big");
            delay(1000);
            return false;
        }

        size_t bytesRead = file.readBytes(buf, sizeof(buf));
        txSeq.cmd += buf;
    }

    if (txSeq.cmd == "") {
        Serial.println("No command to send");
        return false;
    }

    while (txSeq.dataCounter < txSeq.size) {
        txMessage.head.type = txSeq.size > ESP_RAWDATA_SIZE ? MSG_TYPE_CMDLONG : MSG_TYPE_CMDTINY;

        size_t bytesRead = min(txMessage.maxData(), txSeq.size - txSeq.dataCounter);
        memcpy(txMessage.getData(), txSeq.cmd.c_str() + txSeq.dataCounter, bytesRead);
        txSeq.dataCounter = min(txSeq.dataCounter + bytesRead, txSeq.size);

        // fill
        txMessage.head.dataSize = bytesRead;
        if (txMessage.head.type == MSG_TYPE_CMDLONG) {
            txMessage.seq.seqId = txSeq.seqId;
            txMessage.seq.totalBytes = txSeq.size;
            txMessage.seq.bytesSent = txSeq.dataCounter;
        }

        if (txSeq.dataCounter == txSeq.size) {
            txMessage.head.flags |= MSG_FLAG_DONE; // mark as completed
        }

        printMessage(txMessage);

        esp_err_t response = esp_now_send(dstAddress, (uint8_t *)&txMessage, ESP_NOW_MAX_DATA_LEN);
        if (response != ESP_OK) {
            Serial.printf("Send cmd response: %s\n", esp_err_to_name(response));
            return false;
        }

        progressHandler(txSeq.dataCounter, txSeq.size, "Sending...");
        delay(100);
    }

    return true;
}

void EspSerialCmd::receiveCommands() {
    displayBanner(APP_MODE_CMDSRECV);
    padprintln("Waiting...");

    rxCommand = "";
    rxQueue.clear();
    rxState = STATE_CONNECTING;
    Message rxMessage;

    if (!beginEspnow()) return;

    delay(100);

    while (1) {
        if (check(EscPress)) {
            displayInfo("Aborting...");
            rxState = STATE_BREAK;
            break;
        }

        if (rxState == STATE_FAILED) {
            displayRecvError();
            rxState = STATE_WAITING;
            rxSeq.reset();
        }
        if (rxState == STATE_DONE) {
            displayRecvCommand(serialCli.parse(rxCommand));
            rxState = STATE_WAITING;
            rxSeq.reset();
        }

        if (!rxQueue.empty()) {
            rxMessage = rxQueue.front();
            rxQueue.erase(rxQueue.begin());

            if (rxMessage.head.type != MSG_TYPE_CMDLONG) {
                if (rxSeq.isStarted) {
                    continue; // We accept only CMDLONG for initiated tranfer.
                }

                if (rxMessage.head.type != MSG_TYPE_CMDTINY) {
                    continue; // We accept only CMDTINY for non-initiated transfers.
                }

                // Process small command
                rxCommand = rxMessage.getData();
                Serial.println(rxCommand);
                Serial.println("Recv done");
                rxState = STATE_DONE;
                continue;
            }

            // process CMDLONG

            if (!rxSeq.isStarted) {
                // Safety check - ensure we got first packet in a sequence.
                if (rxMessage.seq.bytesSent != rxMessage.head.dataSize) {
                    continue; // Skip initiated tranfers.
                }

                // Safety check - ensure file size is same.
                if (rxMessage.seq.totalBytes > ESP_CMDLONG_SIZE) {
                    rxState = STATE_FAILED;
                    displayError("Err recv - cmd too long");
                    continue;
                }

                // First packet on sequence - keep mac, sequence & size
                memcpy(rxSeq.mac, rxMessage.mac, sizeof(rxSeq.mac));
                rxSeq.seqId = rxMessage.seq.seqId;
                rxSeq.size = rxMessage.seq.totalBytes;
                rxSeq.isStarted = true;
            }

            // Safety check - ensure all data from same peer.
            if (memcmp(rxMessage.mac, rxSeq.mac, sizeof(rxSeq.mac)) != 0) {
                continue; // Skip everything from other peers.
            }

            // Safety check - ensure all data from same sequence.
            if (rxMessage.seq.seqId != rxSeq.seqId) {
                continue; // Skip everything from other sequences.
            }

            // Safety check - ensure file size is same.
            if (rxMessage.seq.totalBytes != rxSeq.size) {
                rxState = STATE_FAILED;
                displayError("Err recv - cmd size mismatch.");
                continue;
            }

            rxSeq.dataCounter += rxMessage.head.dataSize; // increment counter

            // Safety check - ensure we receive full sequence.
            if (rxSeq.dataCounter != rxMessage.seq.bytesSent) {
                rxState = STATE_FAILED;
                displayError("Err recv - lost packet.");
                continue;
            }

            progressHandler(rxSeq.dataCounter, rxSeq.size, "Receiving...");
            rxCommand += rxMessage.getData();

            if (rxMessage.head.flags & MSG_FLAG_DONE) {
                Serial.println(rxCommand);
                Serial.println("Recv done");
                rxState = rxMessage.seq.bytesSent == rxSeq.size ? STATE_DONE : STATE_FAILED;
            }
        }

        delay(100);
    }

    delay(1000);
}

File EspSerialCmd::selectFile() {
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

void EspSerialCmd::inputCommand() {
    // debounce
    tft.fillScreen(bruceConfig.bgColor);
    delay(500);

    txSeq.cmd = keyboard("", ESP_RAWDATA_SIZE, "Serial Command");

    if (txSeq.cmd == "doom") {
        txSeq.cmd =
            "music_player "
            "doom:d=4,o=5,b=112:16e4,16e4,16e,16e4,16e4,16d,16e4,16e4,16c,16e4,16e4,16a#4,16e4,16e4,16b4,16c,"
            "16e4,16e4,16e,16e4,16e4,16d,16e4,16e4,16c,16e4,16e4,a#4,16p,16e4,16e4,16e,16e4,16e4,16d,16e4,"
            "16e4,16c,16e4,16e4,16a#4,16e4,16e4,16b4,16c,16e4,16e4,16e,16e4,16e4,16d,16e4,16e4,16c,16e4,16e4,"
            "a#4,16p,16a4,16a4,16a,16a4,16a4,16g,16a4,16a4,16f,16a4,16a4,16d#,16a4,16a4,16e,16f,16a4,16a4,"
            "16a,16a4,16a4,16g,16a4,16a4,16f,16a4,16a4,d#";
    }
}

void EspSerialCmd::displayBanner(AppMode mode) {
    switch (mode) {
        case APP_MODE_CMDSRECV: drawMainBorderWithTitle("RECEIVE COMMANDS"); break;
        case APP_MODE_CMDSSEND: drawMainBorderWithTitle("SEND COMMANDS"); break;
        default: drawMainBorderWithTitle("UNKNOWN MODE"); break;
    }

    padprintln("");
}

void EspSerialCmd::displayRecvCommand(bool success) {
    String execution = success ? "Execution success" : "Execution failed";
    Serial.println(execution);

    displayBanner(APP_MODE_CMDSRECV);
    padprintln("Command received: ");
    padprintln(rxCommand);
    padprintln("");
    padprintln(execution);

    displayRecvFooter();
}

void EspSerialCmd::displayRecvError() {
    displayBanner(APP_MODE_CMDSRECV);
    padprintln("Error receiving command");
    displayRecvFooter();
}

void EspSerialCmd::displayRecvFooter() {
    padprintln("\n");
    padprintln("Press [ESC] to leave");
}

void EspSerialCmd::displaySentCommand(const char *command) {
    displayBanner(APP_MODE_CMDSSEND);
    padprintln("Command sent: ");
    padprintln(command);
    displaySentFooter();
}

void EspSerialCmd::displaySentError() {
    displayBanner(APP_MODE_CMDSSEND);
    padprintln("Error sending command");
    displaySentFooter();
}

void EspSerialCmd::displaySentFooter() {
    padprintln("\n");
    padprintln("Press [OK] to send another command");
    padprintln("");
    padprintln("Press [ESC] to leave");
}
