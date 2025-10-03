#pragma once
#include "SortedSetHandler.hpp"
#include <vector>
#include <string>

class GeoHandler {
public:
    explicit GeoHandler(SortedSetHandler* ssHandler);

    bool isGeoCommand(const std::string& cmd);
    void handleCommand(const std::string& cmd, const std::vector<std::string>& args);

private:
    SortedSetHandler* sortedSetHandler;

    void handleGeoAdd(const std::vector<std::string>& args);
    void handleGeoPos(const std::vector<std::string>& args);
};
