#include "serial_commands.h"
#include "core/display.h"
#include "core/mykeyboard.h"

EspSerialCmd::EspSerialCmd() {}

void EspSerialCmd::sendCommands() {
    displaySendBanner();
    padprintln("Waiting...");

    if (!beginSend()) return;

    sendStatus = CONNECTING;
    Message message;

    delay(100);

    while (1) {
        if (check(EscPress)) {
            displayInfo("Aborting...");
            sendStatus = ABORTED;
            break;
        }

        if (check(SelPress)) { sendStatus = CONNECTING; }

        if (sendStatus == CONNECTING) {
            message = createCmdMessage();

            if (message.header.dataSize > 0) {
                esp_err_t response = esp_now_send(dstAddress, (uint8_t *)&message, sizeof(message));
                if (response == ESP_OK) sendStatus = SUCCESS;
                else {
                    Serial.printf("Send command response: %s\n", esp_err_to_name(response));
                    sendStatus = FAILED;
                }
            } else {
                Serial.println("No command to send");
                sendStatus = FAILED;
            }
        }

        if (sendStatus == FAILED) {
            displaySentError();
            sendStatus = WAITING;
        }

        if (sendStatus == SUCCESS) {
            displaySentCommand(message.body.data);
            sendStatus = WAITING;
        }

        delay(100);
    }

    delay(1000);
}

void EspSerialCmd::receiveCommands() {
    displayRecvBanner();
    padprintln("Waiting...");

    recvCommand = "";
    recvQueue.clear();
    recvStatus = CONNECTING;
    Message recvMessage;

    if (!beginEspnow()) return;

    delay(100);

    while (1) {
        if (check(EscPress)) {
            displayInfo("Aborting...");
            recvStatus = ABORTED;
            break;
        }

        if (recvStatus == FAILED) {
            displayRecvError();
            recvStatus = WAITING;
        }
        if (recvStatus == SUCCESS) {
            displayRecvCommand(serialCli.parse(recvCommand));
            recvStatus = WAITING;
        }

        if (!recvQueue.empty()) {
            recvMessage = recvQueue.front();
            recvQueue.erase(recvQueue.begin());

            // Filter non-command messages.
            if (recvMessage.header.type != MSG_TYPE_COMMAND) { continue; }

            recvCommand = recvMessage.body.data;
            Serial.println(recvCommand);

            if (recvMessage.header.flags & MSG_FLAG_DONE) {
                Serial.println("Recv done");
                recvStatus = recvMessage.body.bytesSent == recvMessage.body.totalBytes ? SUCCESS : FAILED;
            }
        }

        delay(100);
    }

    delay(1000);
}

EspSerialCmd::Message EspSerialCmd::createCmdMessage() {
    // debounce
    tft.fillScreen(bruceConfig.bgColor);
    delay(500);

    String command = keyboard("", ESP_DATA_SIZE, "Serial Command");
    Message msg = createMessage(command);
    msg.header.type = MSG_TYPE_COMMAND;
    printMessage(msg);

    return msg;
}

void EspSerialCmd::displayRecvBanner() {
    drawMainBorderWithTitle("RECEIVE COMMANDS");
    padprintln("");
}

void EspSerialCmd::displaySendBanner() {
    drawMainBorderWithTitle("SEND COMMANDS");
    padprintln("");
}

void EspSerialCmd::displayRecvCommand(bool success) {
    String execution = success ? "Execution success" : "Execution failed";
    Serial.println(execution);

    displayRecvBanner();
    padprintln("Command received: ");
    padprintln(recvCommand);
    padprintln("");
    padprintln(execution);

    displayRecvFooter();
}

void EspSerialCmd::displayRecvError() {
    displayRecvBanner();
    padprintln("Error receiving command");
    displayRecvFooter();
}

void EspSerialCmd::displayRecvFooter() {
    padprintln("\n");
    padprintln("Press [ESC] to leave");
}

void EspSerialCmd::displaySentCommand(const char *command) {
    displaySendBanner();
    padprintln("Command sent: ");
    padprintln(command);
    displaySentFooter();
}

void EspSerialCmd::displaySentError() {
    displaySendBanner();
    padprintln("Error sending command");
    displaySentFooter();
}

void EspSerialCmd::displaySentFooter() {
    padprintln("\n");
    padprintln("Press [OK] to send another command");
    padprintln("");
    padprintln("Press [ESC] to leave");
}
