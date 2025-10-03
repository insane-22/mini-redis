#include "RdbReader.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <cstring>
#include <iostream>

RdbReader::RdbReader(const std::string &filepath) : filepath_(filepath) {}

bool RdbReader::load() {
    std::ifstream in(filepath_, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "RdbReader: file not found: " << filepath_ << " (continuing without load)\n";
        return true;
    }
    try {
        if (!parseFile(in)) {
            std::cerr << "RdbReader: parseFile returned false for " << filepath_ << "\n";
        }
    } catch (const std::exception &ex) {
        std::cerr << "RdbReader: exception while parsing: " << ex.what() << "\n";
        db_data_.clear();
    } catch (...) {
        std::cerr << "RdbReader: unknown exception while parsing\n";
        db_data_.clear();
    }
    in.close();
    return true;
}

const std::unordered_map<int, std::unordered_map<std::string, RdbEntry>>& RdbReader::getAllEntries() const {
    return db_data_;
}

std::vector<std::string> RdbReader::getKeys(int db) const {
    std::vector<std::string> result;
    auto it = db_data_.find(db);
    if (it == db_data_.end()) return result;

    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto &kv : it->second) {
        const RdbEntry &entry = kv.second;
        if (entry.expiry.has_value()) {
            if (entry.expiry.value() <= now_ms) continue; // expired -> skip
        }
        result.push_back(kv.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<std::string> RdbReader::getValue(int db, const std::string &key) const {
    auto it = db_data_.find(db);
    if (it == db_data_.end()) return std::nullopt;
    auto it2 = it->second.find(key);
    if (it2 == it->second.end()) return std::nullopt;

    if (it2->second.expiry.has_value()) {
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        if (now > it2->second.expiry.value()) {
            return std::nullopt; 
        }
    }
    return it2->second.value;
}

bool RdbReader::parseFile(std::ifstream &in) {
    std::string header = readN(in, 9);
    if (header.size() != 9 || header.rfind("REDIS", 0) != 0) {
        return false;
    }

    int current_db = 0;

    while (true) {
        uint8_t op;
        if (!tryReadByte(in, op)) break;

        if (op == 0xFF) { 
            skipBytes(in, 8);
            break;
        } else if (op == 0xFE) { 
            bool e=false; int t=0;
            uint64_t dbid = readLength(in, e, t);
            current_db = static_cast<int>(dbid);
            continue;
        } else if (op == 0xFB) { 
            bool e=false; int t=0;
            readLength(in, e, t); 
            readLength(in, e, t); 
            continue;
        } else if (op == 0xFA) { 
            readString(in);
            readString(in);
            continue;
        }

        if (op == 0xFC) { 
            uint64_t ts = readUInt64LE(in);
            pending_expiry_ = static_cast<int64_t>(ts);
            if (!tryReadByte(in, op)) break;
        } else if (op == 0xFD) { 
            uint32_t ts = readUInt32LE(in);
            pending_expiry_ = static_cast<int64_t>(ts) * 1000;
            if (!tryReadByte(in, op)) break;
        }

        if (op == 0x00) { 
            std::string key = readString(in);
            std::string value = readString(in);
            RdbEntry entry;
            entry.value = std::move(value);
            entry.expiry = pending_expiry_;
            if (entry.expiry.has_value()) {
                int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                if (entry.expiry.value() <= now) {
                    pending_expiry_.reset();
                    continue;
                }
            }
            db_data_[current_db][std::move(key)] = std::move(entry);
            pending_expiry_.reset();
        } else if (op == 0x01 || op == 0x02 || op == 0x03 ||    op == 0x04 || op == 0x05) {
            bool e=false; int t=0;
            uint64_t len = readLength(in, e, t);
            for (uint64_t i=0;i<len;i++) {
                readString(in);
                if (op == 0x03 || op == 0x04 || op == 0x05) readString(in);
            }
        } else {
            throw std::runtime_error("Unsupported RDB object type encountered");
        }
    }

    return true;
}

std::string RdbReader::readN(std::ifstream &in, size_t n) {
    std::string s;
    s.resize(n);
    in.read(&s[0], static_cast<std::streamsize>(n));
    if (static_cast<size_t>(in.gcount()) != n) throw std::runtime_error("Unexpected EOF reading bytes");
    return s;
}

bool RdbReader::tryReadByte(std::ifstream &in, uint8_t &out) {
    char c;
    if (!in.get(c)) return false;
    out = static_cast<uint8_t>(static_cast<unsigned char>(c));
    return true;
}

uint8_t RdbReader::readByte(std::ifstream &in) {
    uint8_t b;
    if (!tryReadByte(in, b)) throw std::runtime_error("Unexpected EOF reading byte");
    return b;
}

void RdbReader::skipBytes(std::ifstream &in, size_t n) {
    in.ignore(static_cast<std::streamsize>(n));
}

uint32_t RdbReader::readUInt32LE(std::ifstream &in) {
    uint32_t r = 0;
    for (int i = 0; i < 4; ++i) r |= (static_cast<uint32_t>(readByte(in)) << (8 * i));
    return r;
}

uint64_t RdbReader::readUInt64LE(std::ifstream &in) {
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i) r |= (static_cast<uint64_t>(readByte(in)) << (8 * i));
    return r;
}

uint32_t RdbReader::readUInt32BE(std::ifstream &in) {
    uint32_t r = 0;
    r |= static_cast<uint32_t>(readByte(in)) << 24;
    r |= static_cast<uint32_t>(readByte(in)) << 16;
    r |= static_cast<uint32_t>(readByte(in)) << 8;
    r |= static_cast<uint32_t>(readByte(in));
    return r;
}

uint64_t RdbReader::readUInt64BE(std::ifstream &in) {
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i) {
        r = (r << 8) | static_cast<uint64_t>(readByte(in));
    }
    return r;
}

uint64_t RdbReader::readLength(std::ifstream &in, bool &isEncoded, int &encType) {
    isEncoded = false;
    encType = -1;
    uint8_t first = readByte(in);
    uint8_t top = (first & 0xC0) >> 6;
    if (top == 0) {
        return static_cast<uint64_t>(first & 0x3F);
    } else if (top == 1) {
        uint8_t second = readByte(in);
        return ((static_cast<uint64_t>(first & 0x3F) << 8) | static_cast<uint64_t>(second));
    } else if (top == 2) {
        return static_cast<uint64_t>(readUInt32BE(in));
    } else {
        isEncoded = true;
        encType = static_cast<int>(first & 0x3F);
        return 0;
    }
}

std::string RdbReader::readString(std::ifstream &in) {
    bool enc=false; int encType=0;
    uint64_t len = readLength(in, enc, encType);
    if (enc) {
        if (encType == 0) {
            int8_t v = static_cast<int8_t>(readByte(in));
            return std::to_string((int)v);
        } else if (encType == 1) {
            uint8_t b0 = readByte(in);
            uint8_t b1 = readByte(in);
            int16_t v = static_cast<int16_t>(static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8));
            return std::to_string((int)v);
        } else if (encType == 2) {
            uint32_t v = readUInt32LE(in);
            return std::to_string(v);
        } else if (encType == 3) {
            bool t=false; int dummy=0;
            uint64_t clen = readLength(in, t, dummy);
            uint64_t olen = readLength(in, t, dummy);
            skipBytes(in, static_cast<size_t>(clen));
            throw std::runtime_error("Encountered LZF-compressed string; unsupported in this RdbReader.");
        } else {
            throw std::runtime_error("Unknown string encoding type");
        }
    } else {
        if (len == 0) return std::string();
        std::string s;
        s.resize(len);
        in.read(&s[0], static_cast<std::streamsize>(len));
        if (static_cast<uint64_t>(in.gcount()) != len) throw std::runtime_error("Unexpected EOF reading string payload");
        return s;
    }
}
