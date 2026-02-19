#include "string_parsing.h"
#include <cstdio>

VehicleDataParser* VehicleDataParser::getInstance() {
    static VehicleDataParser instance;
    return &instance;
}


bool VehicleDataParser::parseLine(const std::string& line, VehicleData& data) {
    std::stringstream ss(line);
    std::vector<std::string> fields;
    std::string token;
    while (std::getline(ss, token, ',')) {
        fields.emplace_back(token);
    }

    if (fields.size() != 5) {
        std::cerr << "Malformed line: expected 5 fields, got " << fields.size()
                  << " -> " << line << std::endl;
        return false;
    }

    try {
        data.vehicleId = std::stoi(fields[0]);
        data.timestamp = fields[1];
        data.speed = std::stod(fields[2]);

        if (fields[3] == "1" || fields[3] == "ON" || fields[3] == "ENGINE_OK") {
            data.engineOn = true;
        } else if (fields[3] == "0" || fields[3] == "OFF") {
            data.engineOn = false;
        } else {
            throw std::invalid_argument("invalid engineOn field");
        }

        if (fields[4] == "ENGINE_OK" || fields[4] == "OK") {
            data.errorCode = EngineStatus::OK;
        } else if (fields[4] == "ENGINE_OVERHEAT") {
            data.errorCode = EngineStatus::E_Overheat;
        } else if (fields[4] == "SENSOR_FAILURE" || fields[4] == "ENGINE_SENSOR_FAIL") {
            data.errorCode = EngineStatus::E_SensorFailure;
        } else {
            data.errorCode = EngineStatus::E_Unknown;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Malformed line: " << ex.what() << " -> " << line << std::endl;
        return false;
    }
    return true;
}

bool messageQueueSend(int msgId, const VehicleData& data) {

    Msg msg;
    msg.type = 1;
    std::snprintf(msg.text, sizeof(msg.text), "ID:%d,Time:%s,Speed:%.2f,Engine:%s,ErrorCode:%d",
        data.vehicleId, data.timestamp.c_str(), data.speed, data.engineOn ? "ON" : "OFF", static_cast<int>(data.errorCode));
    if (msgsnd(msgId, &msg, sizeof(msg.text), 0) == -1) {
        perror("msgsnd");
        return false;
    }
    std::cout << "Sent message successfully: " << msg.text << std::endl;
    return true;
}

bool sendEmptyTerminationMessage() {
    key_t key = MSG_QUEUE_KEY;

    int msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("msgget");
        return false;
    }

    Msg msg{};
    msg.type = 1;
    msg.text[0] = '\0';

    if (msgsnd(msgid, &msg, sizeof(msg.text), 0) == -1) {
        perror("msgsnd");
        return false;
    }
    std::cout << "Sent termination message (empty payload)" << std::endl;
    return true;
}

sendStatus VehicleDataParser::parseAndSend() {
    // implementing message queue and sending data to receiver
    key_t key = MSG_QUEUE_KEY;

    std::ifstream dataFile(DATA_FILE_PATH);
    if (!dataFile.is_open()) {
        std::cerr << "Failed to open data file : " << DATA_FILE_PATH << std::endl;
        return sendStatus::E_Error;
    }


    int msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("msgget");
        return sendStatus::E_Error;
    }
    
    std::string line;
    size_t lineNumber = 0;
    size_t validCount = 0;
    size_t invalidCount = 0;
    while (std::getline(dataFile, line)) {
        ++lineNumber;
        if (line.empty()) {
            continue;
        }
        VehicleData data;
        if (!parseLine(line, data)) {
            std::cerr << "Skipping line " << lineNumber << " due to parse error\n";
            ++invalidCount;
            continue;
        }
        if (!messageQueueSend(msgid, data)) {
            std::cerr << "Unable to send message for line " << lineNumber << std::endl;
            return sendStatus::E_Error;
        }
        ++validCount;
    }
    std::cout << "Finished sending messages. Valid lines: " << validCount << ", Invalid lines: " << invalidCount << std::endl;

    if (!sendEmptyTerminationMessage()) {
        std::cerr << "unable to send termination message to receiverManager" << std::endl;
        return sendStatus::E_Error;
    }

    return sendStatus::E_OK;

}
