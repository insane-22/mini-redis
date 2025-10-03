#pragma once
#include <cstdint>

constexpr double MIN_LATITUDE = -85.05112878;
constexpr double MAX_LATITUDE = 85.05112878;
constexpr double MIN_LONGITUDE = -180.0;
constexpr double MAX_LONGITUDE = 180.0;

constexpr double LATITUDE_RANGE = MAX_LATITUDE - MIN_LATITUDE;
constexpr double LONGITUDE_RANGE = MAX_LONGITUDE - MIN_LONGITUDE;

struct Coordinates {
    double latitude;
    double longitude;
};

uint64_t encode(double latitude, double longitude);
Coordinates decode(uint64_t geo_code);
