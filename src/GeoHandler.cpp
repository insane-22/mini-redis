#include "GeoHandler.hpp"
#include "GeoEncoding.hpp"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cmath>

GeoHandler::GeoHandler(SortedSetHandler* ssHandler)
    : sortedSetHandler(ssHandler) {}

bool GeoHandler::isGeoCommand(const std::string& cmd) {
    return cmd == "GEOADD" || cmd == "GEOPOS" || cmd == "GEODIST" || cmd == "GEOSEARCH";
}

void GeoHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "GEOADD") handleGeoAdd(args);
    else if (cmd == "GEOPOS") handleGeoPos(args);
    else if (cmd == "GEODIST") handleGeoDis(args);
    else if (cmd == "GEOSEARCH") handleGeoSearch(args);
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

void GeoHandler::handleGeoDis(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        sortedSetHandler->sendResponse("-ERR GEODIST requires key, member1 and member2\r\n");
        return;
    }

    const std::string& key = args[0];
    const std::string& member1 = args[1];
    const std::string& member2 = args[2];

    auto scoreOpt1 = sortedSetHandler->getScore(key, member1);
    auto scoreOpt2 = sortedSetHandler->getScore(key, member2);

    if (!scoreOpt1.has_value() || !scoreOpt2.has_value()) {
        sortedSetHandler->sendResponse("$-1\r\n");  
        return;
    }

    uint64_t score1 = static_cast<uint64_t>(scoreOpt1.value());
    uint64_t score2 = static_cast<uint64_t>(scoreOpt2.value());

    Coordinates coords1 = decode(score1);
    Coordinates coords2 = decode(score2);

    double distance = haversine(coords1.latitude, coords1.longitude, coords2.latitude, coords2.longitude);

    std::ostringstream distSs;
    distSs << std::setprecision(17) << distance;
    std::string distStr = distSs.str();

    std::ostringstream resp;
    resp << "$" << distStr.size() << "\r\n" << distStr << "\r\n";
    sortedSetHandler->sendResponse(resp.str());
}

double GeoHandler::haversine(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6372797.560856; 
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;

    lat1 = lat1 * M_PI / 180.0;
    lat2 = lat2 * M_PI / 180.0;

    double a = sin(dLat/2) * sin(dLat/2) + sin(dLon/2) * sin(dLon/2) * cos(lat1) * cos(lat2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));

    return R * c;
}   

void GeoHandler::handleGeoSearch(const std::vector<std::string>& args){
    if (args.size() < 7) {
        sortedSetHandler->sendResponse("-ERR GEOSEARCH requires key FROMLONLAT <lon> <lat> BYRADIUS <radius> <unit>\r\n");
        return;
    }

    const std::string& key = args[0];

    if (args[1] != "FROMLONLAT") {
        sortedSetHandler->sendResponse("-ERR Only FROMLONLAT mode supported\r\n");
        return;
    }

    double centerLon = std::stod(args[2]);
    double centerLat = std::stod(args[3]);

    if (args[4] != "BYRADIUS") {
        sortedSetHandler->sendResponse("-ERR Only BYRADIUS search supported\r\n");
        return;
    }

    double radius = std::stod(args[5]);
    std::string unit = args[6];

    if (unit == "m") {
        // already meters
    } else if (unit == "km") {
        radius *= 1000.0;
    } else if (unit == "mi") {
        radius *= 1609.344;
    } else if (unit == "ft") {
        radius *= 0.3048;
    } else {
        sortedSetHandler->sendResponse("-ERR Unsupported unit\r\n");
        return;
    }

    auto membersWithScores = sortedSetHandler->getAllWithScores(key);

    std::vector<std::string> results;

    for (const auto& [member, score] : membersWithScores) {
        uint64_t scoreVal = static_cast<uint64_t>(score);
        Coordinates coords = decode(scoreVal);
        double distance = haversine(centerLat, centerLon, coords.latitude, coords.longitude);

        if (distance <= radius) {
            results.push_back(member);
        }
    }

    std::ostringstream resp;
    resp << "*" << results.size() << "\r\n";
    for (const auto& m : results) {
        resp << "$" << m.size() << "\r\n" << m << "\r\n";
    }

    sortedSetHandler->sendResponse(resp.str());
}