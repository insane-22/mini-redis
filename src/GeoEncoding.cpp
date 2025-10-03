#include "GeoEncoding.hpp"
#include <cmath>

static uint64_t spread_int32_to_int64(uint32_t v) {
    uint64_t result = v;
    result = (result | (result << 16)) & 0x0000FFFF0000FFFFULL;
    result = (result | (result << 8))  & 0x00FF00FF00FF00FFULL;
    result = (result | (result << 4))  & 0x0F0F0F0F0F0F0F0FULL;
    result = (result | (result << 2))  & 0x3333333333333333ULL;
    result = (result | (result << 1))  & 0x5555555555555555ULL;
    return result;
}

static uint64_t interleave(uint32_t x, uint32_t y) {
    return spread_int32_to_int64(x) | (spread_int32_to_int64(y) << 1);
}

uint64_t encode(double latitude, double longitude) {
    constexpr double LAT_RANGE = MAX_LATITUDE - MIN_LATITUDE;
    constexpr double LON_RANGE = MAX_LONGITUDE - MIN_LONGITUDE;

    double norm_lat = pow(2, 26) * (latitude - MIN_LATITUDE) / LAT_RANGE;
    double norm_lon = pow(2, 26) * (longitude - MIN_LONGITUDE) / LON_RANGE;

    uint32_t lat_int = static_cast<uint32_t>(norm_lat);
    uint32_t lon_int = static_cast<uint32_t>(norm_lon);

    return interleave(lat_int, lon_int);
}

uint32_t compact_int64_to_int32(uint64_t v) {
    v = v & 0x5555555555555555ULL;
    v = (v | (v >> 1)) & 0x3333333333333333ULL;
    v = (v | (v >> 2)) & 0x0F0F0F0F0F0F0F0FULL;
    v = (v | (v >> 4)) & 0x00FF00FF00FF00FFULL;
    v = (v | (v >> 8)) & 0x0000FFFF0000FFFFULL;
    v = (v | (v >> 16)) & 0x00000000FFFFFFFFULL;
    return static_cast<uint32_t>(v);
}

Coordinates convert_grid_numbers_to_coordinates(uint32_t grid_latitude_number, uint32_t grid_longitude_number) {
    double grid_latitude_min = MIN_LATITUDE + LATITUDE_RANGE * (grid_latitude_number / pow(2, 26));
    double grid_latitude_max = MIN_LATITUDE + LATITUDE_RANGE * ((grid_latitude_number + 1) / pow(2, 26));
    double grid_longitude_min = MIN_LONGITUDE + LONGITUDE_RANGE * (grid_longitude_number / pow(2, 26));
    double grid_longitude_max = MIN_LONGITUDE + LONGITUDE_RANGE * ((grid_longitude_number + 1) / pow(2, 26));
    
    Coordinates result;
    result.latitude = (grid_latitude_min + grid_latitude_max) / 2;
    result.longitude = (grid_longitude_min + grid_longitude_max) / 2;
    
    return result;
}

Coordinates decode(uint64_t geo_code) {
    uint64_t y = geo_code >> 1;
    uint64_t x = geo_code;
    
    uint32_t grid_latitude_number = compact_int64_to_int32(x);
    uint32_t grid_longitude_number = compact_int64_to_int32(y);
    
    return convert_grid_numbers_to_coordinates(grid_latitude_number, grid_longitude_number);
}
