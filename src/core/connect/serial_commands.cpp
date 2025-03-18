#include "serial_commands.h"
#include "core/display.h"
#include "core/mykeyboard.h"

EspSerialCmd::EspSerialCmd() { rxQueueFilter = MSG_FILTER_SERIAL; }

void EspSerialCmd::sendCommands() {
    displayBanner(APP_MODE_CMDSSEND);
    padprintln("Waiting...");

    if (!beginSend()) return;

    txState = STATE_CONNECTING;
    Message txMessage;

    delay(100);

    while (1) {
        if (check(EscPress)) {
            displayInfo("Aborting...");
            txState = STATE_BREAK;
            break;
        }

        if (check(SelPress)) { txState = STATE_CONNECTING; }

        if (txState == STATE_CONNECTING) {
            txMessage = createCmdMessage();

            if (txMessage.head.dataSize > 0) {
                esp_err_t response = esp_now_send(dstAddress, (uint8_t *)&txMessage, ESP_NOW_MAX_DATA_LEN);
                if (response == ESP_OK) txState = STATE_DONE;
                else {
                    Serial.printf("Send command response: %s\n", esp_err_to_name(response));
                    txState = STATE_FAILED;
                }
            } else {
                Serial.println("No command to send");
                txState = STATE_FAILED;
            }
        }

        if (txState == STATE_FAILED) {
            displaySentStatus(NULL, false);
            txState = STATE_WAITING;
        }

        if (txState == STATE_DONE) {
            displaySentStatus(txMessage.getData(), true);
            txState = STATE_WAITING;
        }

        delay(100);
    }

    delay(1000);
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
            displayRecvStatus(false, false);
            rxState = STATE_WAITING;
        }
        if (rxState == STATE_DONE) {
            displayRecvStatus(true, serialCli.parse(rxCommand));
            rxState = STATE_WAITING;
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
        }

        delay(100);
    }

    delay(1000);
}

EspSerialCmd::Message EspSerialCmd::createCmdMessage() {
    // debounce
    tft.fillScreen(bruceConfig.bgColor);
    delay(500);

    Message msg;
    msg.head.type = MSG_TYPE_CMDTINY;

    String command = keyboard("", sizeof(FileHeadBlock), "Serial Command");
    msg.head.flags |= MSG_FLAG_DONE;
    msg.head.dataSize = min(command.length(), msg.maxData());
    strncpy(msg.rawBody, command.c_str(), msg.maxData());

    printMessage(msg);

    return msg;
}

void EspSerialCmd::displayBanner(AppMode mode) {
    switch (mode) {
        case APP_MODE_CMDSRECV: drawMainBorderWithTitle("RECEIVE COMMANDS"); break;
        case APP_MODE_CMDSSEND: drawMainBorderWithTitle("SEND COMMANDS"); break;
        default: drawMainBorderWithTitle("UNKNOWN MODE"); break;
    }

    padprintln("");
}

void EspSerialCmd::displaySentStatus(const char *command, bool success) {
    displayBanner(APP_MODE_CMDSSEND);
    if (success) {
        padprintln("Command sent: ");
        padprintln(command ? command : "<null>");
    } else {
        padprintln("Error sending command");
    }

    padprintln("\n");
    padprintln("Press [OK] to send another command");
    padprintln("");
    padprintln("Press [ESC] to leave");
}

void EspSerialCmd::displayRecvStatus(bool received, bool executed) {
    displayBanner(APP_MODE_CMDSRECV);

    if (received) {
        String execution = executed ? "Execution success" : "Execution failed";
        Serial.println(execution);

        padprintln("Command received: ");
        padprintln(rxCommand);
        padprintln("");
        padprintln(execution);
    } else {
        padprintln("Error receiving command");
    }

    padprintln("\n");
    padprintln("Press [ESC] to leave");
}
