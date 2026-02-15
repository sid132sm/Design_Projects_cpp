#include "messageReceiver.h"
#include <cerrno>
#include <chrono>
#include <thread>

bool MessageReceiver::receiveMessage() {
    key_t key = MSG_QUEUE_KEY;

    int msgid = msgget(key, IPC_CREAT | 0666);  //0666 is for permission
    if (msgid == -1) {
        perror("msgget");
        return false;
    }

    constexpr int kPollSleepMs = 100;
    constexpr int kMaxIdlePolls = 20;  // ~2 seconds total idle wait
    int idlePolls = 0;

    while (true) {
        if (msgrcv(msgid, &msg, sizeof(msg.text), 0, IPC_NOWAIT) != -1) {
            std::cout << "Received message successfully: " << msg.text << std::endl;
            return true;
        }

        if (errno == ENOMSG) {
            if (idlePolls >= kMaxIdlePolls) {
                msg.text[0] = '\0';
                return true;
            }
            ++idlePolls;
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollSleepMs));
            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        perror("msgrcv");
        return false;
    }
}

void MessageReceiver::printMessage() {
    // This function can be expanded to format the message or extract specific fields if needed.
    // For now, it simply prints the received message.
    std::vector<std::string> fields;
    std::string msgText(msg.text);
    std::stringstream ss(msgText);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    for (const auto& f : fields) {
        std::cout << "Field: " << f << std::endl;
    }
}

bool MessageReceiver::isMessageEmpty() const {
    return msg.text[0] == '\0';
}
