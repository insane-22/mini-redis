#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <optional>

struct RdbEntry {
    std::string value;
    std::optional<int64_t> expiry;
};

class RdbReader {
public:
    explicit RdbReader(const std::string &filepath);
    bool load();
    std::vector<std::string> getKeys(int db = 0) const;
    std::optional<std::string> getValue(int db, const std::string &key) const;
    const std::unordered_map<int, std::unordered_map<std::string, RdbEntry>>& getAllEntries() const;

private:
    std::string filepath_;
    std::unordered_map<int, std::unordered_map<std::string, RdbEntry>> db_data_;
    std::optional<int64_t> pending_expiry_;

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
