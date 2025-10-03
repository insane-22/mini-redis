#include "GeoHandler.hpp"
#include <sstream>

GeoHandler::GeoHandler(SortedSetHandler* ssHandler)
    : sortedSetHandler(ssHandler) {}

bool GeoHandler::isGeoCommand(const std::string& cmd) {
    return cmd == "GEOADD";
}

void GeoHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "GEOADD") handleGeoAdd(args);
    else sortedSetHandler->sendResponse("-ERR Unsupported geo command\r\n");
}

void GeoHandler::handleGeoAdd(const std::vector<std::string>& args) {
    if (args.size() < 4) {
        sortedSetHandler->sendResponse("-ERR GEOADD requires key, longitude, latitude and member\r\n");
        return;
    }

    const std::string& key = args[0];
    double longitude = std::stod(args[1]);
    double latitude = std::stod(args[2]);
    const std::string& member = args[3];

    bool invalidLongitude = longitude < -180.0 || longitude > 180.0;
    bool invalidLatitude  = latitude  < -85.05112878 || latitude  > 85.05112878;

    if (invalidLongitude || invalidLatitude) {
        std::ostringstream err;
        err << "-ERR invalid ";
        if (invalidLongitude) err << "longitude";
        if (invalidLongitude && invalidLatitude) err << ",";
        if (invalidLatitude) err << "latitude";
        err << " value\r\n";
        sortedSetHandler->sendResponse(err.str());
        return;
    }

    sortedSetHandler->handleZAdd({key, "0", member});
}
