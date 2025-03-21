#include "serial_commands.h"
#include "core/display.h"
#include "core/mykeyboard.h"

EspSerialCmd::EspSerialCmd() {}

void EspSerialCmd::sendCommands() {
    displaySendBanner();
    padprintln("Waiting...");

    if (!beginSend()) return;

    txState = CONNECTING;
    Message message;

    delay(100);

    while (1) {
        if (check(EscPress)) {
            displayInfo("Aborting...");
            txState = ABORTED;
            break;
        }

        if (check(SelPress)) { txState = CONNECTING; }

        if (txState == CONNECTING) {
            message = createCmdMessage();

            if (message.header.dataSize > 0) {
                esp_err_t response = esp_now_send(dstAddress, (uint8_t *)&message, ESP_NOW_MAX_DATA_LEN);
                if (response == ESP_OK) txState = SUCCESS;
                else {
                    Serial.printf("Send command response: %s\n", esp_err_to_name(response));
                    txState = FAILED;
                }
            } else {
                Serial.println("No command to send");
                txState = FAILED;
            }
        }

        if (txState == FAILED) {
            displaySentError();
            txState = WAITING;
        }

        if (txState == SUCCESS) {
            displaySentCommand(message.rawBody);
            txState = WAITING;
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
    rxState = CONNECTING;
    Message recvMessage;

    if (!beginEspnow()) return;

    delay(100);

    while (1) {
        if (check(EscPress)) {
            displayInfo("Aborting...");
            rxState = ABORTED;
            break;
        }

        if (rxState == FAILED) {
            displayRecvError();
            rxState = WAITING;
        }
        if (rxState == SUCCESS) {
            displayRecvCommand(serialCli.parse(recvCommand));
            rxState = WAITING;
        }

        if (!recvQueue.empty()) {
            recvMessage = recvQueue.front();
            recvQueue.erase(recvQueue.begin());

            // Filter non-command messages.
            if (recvMessage.header.type != MSG_TYPE_COMMAND) { continue; }

            recvCommand = recvMessage.rawBody;
            Serial.println(recvCommand);

            Serial.println("Recv done");
            rxState = SUCCESS;
        }

        delay(100);
    }

    delay(1000);
}

EspSerialCmd::Message EspSerialCmd::createCmdMessage() {
    // debounce
    tft.fillScreen(bruceConfig.bgColor);
    delay(500);

    String command = keyboard("", sizeof(MessageBody), "Serial Command");
    Message msg = createTextMessage(command);
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
