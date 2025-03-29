#include "serial_commands.h"
#include "core/display.h"
#include "core/mykeyboard.h"

EspSerialCmd::EspSerialCmd() {}

void EspSerialCmd::sendCommands() {
    displaySendBanner();
    padprintln("Waiting...");

    if (!beginSend()) return;

    txState = STATE_CONNECTING;
    Message message;

    delay(100);

    while (1) {
        if (check(EscPress)) {
            displayInfo("Aborting...");
            txState = STATE_BREAK;
            break;
        }

        if (check(SelPress)) { txState = STATE_CONNECTING; }

        if (txState == STATE_CONNECTING) {
            message = createCmdMessage();

            if (message.dataSize > 0) {
                esp_err_t response = esp_now_send(dstAddress, (uint8_t *)&message, sizeof(message));
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
            displaySentError();
            txState = STATE_WAITING;
        }

        if (txState == STATE_DONE) {
            displaySentCommand(message.getData());
            txState = STATE_WAITING;
        }

        delay(100);
    }

    delay(1000);
}

void EspSerialCmd::receiveCommands() {
    displayRecvBanner();
    padprintln("Waiting...");

    rxCommand = "";
    rxQueue.clear();
    rxState = STATE_CONNECTING;
    Message recvMessage;

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
        }
        if (rxState == STATE_DONE) {
            displayRecvCommand(serialCli.parse(rxCommand));
            rxState = STATE_WAITING;
        }

        if (!rxQueue.empty()) {
            recvMessage = rxQueue.front();
            rxQueue.erase(rxQueue.begin());

            rxCommand = recvMessage.getData();
            Serial.println(rxCommand);

            if (recvMessage.done) {
                Serial.println("Recv done");
                rxState = recvMessage.bytesSent == recvMessage.totalBytes ? STATE_DONE : STATE_FAILED;
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
    padprintln(rxCommand);
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
