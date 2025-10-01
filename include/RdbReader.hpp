#pragma once
#include <string>
#include <vector>
#include <unordered_map>

class RdbReader {
public:
    explicit RdbReader(const std::string &filepath);
    bool load();
    std::vector<std::string> getKeys(int db = 0) const;

private:
    std::string filepath_;
    std::unordered_map<int, std::vector<std::string>> keys_by_db_;

    bool parseFile(std::ifstream &in);
    std::string readN(std::ifstream &in, size_t n);
    bool tryReadByte(std::ifstream &in, uint8_t &out);
    uint8_t readByte(std::ifstream &in);
    void skipBytes(std::ifstream &in, size_t n);
    uint32_t readUInt32LE(std::ifstream &in);
    uint64_t readUInt64LE(std::ifstream &in);
    uint32_t readUInt32BE(std::ifstream &in);
    uint64_t readUInt64BE(std::ifstream &in);
    uint64_t readLength(std::ifstream &in, bool &isEncoded, int &encType);
    std::string readString(std::ifstream &in);
};
