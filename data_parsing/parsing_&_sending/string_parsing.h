#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstdint>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <memory>

const std::string DATA_FILE_PATH = "vehicle_data.txt";
constexpr key_t MSG_QUEUE_KEY = 0x2222;

struct Msg {
    long type;
    char text[256];
};

enum class EngineStatus : uint8_t {
    OK = 0,
    InvalidFormat,
    E_SensorFailure,
    E_Overheat,
    E_Unknown,
};

enum class sendStatus : uint8_t {
    E_OK = 0,
    E_Error,
};

struct VehicleData {
    int vehicleId;
    std::string timestamp;
    double speed;
    bool engineOn;
    EngineStatus errorCode;
};

class VehicleDataParser {
public:
    static VehicleDataParser* getInstance();
    bool extractVehicleData(std::vector<VehicleData>& tokens);
    sendStatus parseAndSend();
private:
    VehicleDataParser() = default;
    VehicleDataParser(const VehicleDataParser&) = delete;
    VehicleDataParser& operator=(const VehicleDataParser&) = delete;
    ~VehicleDataParser() = default;
};
    
