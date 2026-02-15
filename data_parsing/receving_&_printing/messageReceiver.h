#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <sys/ipc.h>
#include <sys/msg.h>

constexpr key_t MSG_QUEUE_KEY = 0x2222;

struct Msg {
    long type;
    char text[256];
};

class MessageReceiver {
public:
    bool receiveMessage();
    void printMessage();
    bool isMessageEmpty() const;
private:
    Msg msg{};
};
