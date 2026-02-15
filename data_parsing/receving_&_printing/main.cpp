#include "messageReceiver.h"

int main() {
    MessageReceiver receiver;
    while (true) {
        if (!receiver.receiveMessage()) {
            return 1;
        }
        if (receiver.isMessageEmpty()) {
            std::cout << "Empty message received. Exiting receiver." << std::endl;
            break;
        }
        receiver.printMessage();
    }
    return 0;
}
