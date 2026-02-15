#include "string_parsing.h"
#include <cstdio>

VehicleDataParser* VehicleDataParser::getInstance() {
    static VehicleDataParser instance;
    return &instance;
}

bool VehicleDataParser::extractVehicleData(std::vector<VehicleData>& tokens) {
    std::ifstream dataFile(DATA_FILE_PATH);
    if (!dataFile.is_open()) {
        std::cerr << "Failed to open data file : " << DATA_FILE_PATH << std::endl;
        return false;
    }

    std::string line;
    size_t lineNumber = 0;
    while (std::getline(dataFile, line)) {
        ++lineNumber;

        if (line.empty()) {
            continue;
        }

        std::stringstream ss(line);
        std::vector<std::string> fields;
        std::string token;
        while (std::getline(ss, token, ',')) {
            fields.emplace_back(token);
        }

        if (fields.size() != 5) {
            std::cerr << "Skipping malformed line " << lineNumber
                      << ": expected 5 fields, got " << fields.size()
                      << " -> " << line << std::endl;
            continue;
        }

        try {
            VehicleData data;
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

            tokens.emplace_back(data);
        } catch (const std::exception& ex) {
            std::cerr << "Skipping malformed line " << lineNumber
                      << ": " << ex.what() << " -> " << line << std::endl;
        }
    }
    return true;
}

bool messageQueueSend(const VehicleData& data) {
    key_t key = MSG_QUEUE_KEY;

    int msgid = msgget(key, IPC_CREAT | 0666);  //0666 is for permission
    if (msgid == -1) {
        perror("msgget");
        return false;
    }

    Msg msg;
    msg.type = 1;
    std::snprintf(msg.text, sizeof(msg.text), "ID:%d,Time:%s,Speed:%.2f,Engine:%s,ErrorCode:%d",
        data.vehicleId, data.timestamp.c_str(), data.speed, data.engineOn ? "ON" : "OFF", static_cast<int>(data.errorCode));
    if (msgsnd(msgid, &msg, sizeof(msg.text), 0) == -1) {
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
    std::vector<VehicleData> vehicleDataList;
    if (!extractVehicleData(vehicleDataList)) {
        std::cerr << "Failed to extract vehicle data\n";
        return sendStatus::E_Error;
    }

    if (vehicleDataList.empty()) {
        std::cerr << "No vehicle data to send\n";
        return sendStatus::E_Error;
    }

    for (const auto& data : vehicleDataList) {
        if (!messageQueueSend(data)) {
            std::cerr << "unable to send mesage to receiverManager" <<std::endl;
            return sendStatus::E_Error;
        }
        std::cout << "data sent to receiverManager successfully" << std::endl;
    }

    if (!sendEmptyTerminationMessage()) {
        std::cerr << "unable to send termination message to receiverManager" << std::endl;
        return sendStatus::E_Error;
    }

    return sendStatus::E_OK;

}
