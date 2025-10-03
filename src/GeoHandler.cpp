#include "GeoHandler.hpp"
#include "GeoEncoding.hpp"
#include <sstream>
#include <iostream>
#include <iomanip>

GeoHandler::GeoHandler(SortedSetHandler* ssHandler)
    : sortedSetHandler(ssHandler) {}

bool GeoHandler::isGeoCommand(const std::string& cmd) {
    return cmd == "GEOADD" || cmd == "GEOPOS";
}

void GeoHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "GEOADD") handleGeoAdd(args);
    else if (cmd == "GEOPOS") handleGeoPos(args);
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

    uint64_t score = encode(latitude, longitude);
    sortedSetHandler->handleZAdd({key, std::to_string(static_cast<double>(score)), member});
}

void GeoHandler::handleGeoPos(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        sortedSetHandler->sendResponse("-ERR GEOPOS requires key and at least one member\r\n");
        return;
    }

    const std::string& key = args[0];
    std::vector<std::string> members(args.begin() + 1, args.end());

    std::ostringstream resp;
    resp << "*" << members.size() << "\r\n";

    for (const auto& member : members) {
        auto scoreOpt = sortedSetHandler->getScore(key, member); 
        if (!scoreOpt.has_value()) {
            resp << "*-1\r\n";  
            continue;
        }

        uint64_t score = static_cast<uint64_t>(scoreOpt.value());
        Coordinates coords = decode(score);

        std::ostringstream lonSs, latSs;
        lonSs << std::setprecision(17) << coords.longitude;
        latSs << std::setprecision(17) << coords.latitude;

        std::string lonStr = lonSs.str();
        std::string latStr = latSs.str();

        resp << "*2\r\n";
        resp << "$" << lonStr.size() << "\r\n" << lonStr << "\r\n";
        resp << "$" << latStr.size() << "\r\n" << latStr << "\r\n";
    }

    sortedSetHandler->sendResponse(resp.str());
}