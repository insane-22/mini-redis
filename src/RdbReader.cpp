#include "RdbReader.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

RdbReader::RdbReader(const std::string &filepath) : filepath_(filepath) {}

bool RdbReader::load() {
    std::ifstream in(filepath_, std::ios::binary);
    if (!in.is_open()) {
        return true;
    }
    try {
        bool ok = parseFile(in);
        (void)ok;
    } catch (...) {
        keys_by_db_.clear();
    }
    in.close();
    return true;
}

std::vector<std::string> RdbReader::getKeys(int db) const {
    auto it = keys_by_db_.find(db);
    if (it == keys_by_db_.end()) return {};
    return it->second;
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
            (void)readUInt64LE(in);
            if (!tryReadByte(in, op)) break;
        } else if (op == 0xFD) { 
            (void)readUInt32LE(in);
            if (!tryReadByte(in, op)) break;
        }

        if (op == 0x00) { 
            std::string key = readString(in);
            std::string value = readString(in);
            keys_by_db_[current_db].push_back(std::move(key));
        } else if (op == 0x01) { 
            bool e=false; int t=0;
            uint64_t len = readLength(in, e, t);
            for (uint64_t i=0;i<len;i++) readString(in);
        } else if (op == 0x02) { 
            bool e=false; int t=0;
            uint64_t len = readLength(in, e, t);
            for (uint64_t i=0;i<len;i++) readString(in);
        } else if (op == 0x03 || op == 0x05) { 
            bool e=false; int t=0;
            uint64_t len = readLength(in, e, t);
            for (uint64_t i=0;i<len;i++) { readString(in); readString(in); }
        } else if (op == 0x04) {
            bool e=false; int t=0;
            uint64_t len = readLength(in, e, t);
            for (uint64_t i=0;i<len;i++) { readString(in); readString(in); }
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
        return ((static_cast<uint64_t>(first & 0x3F) << 8) | second);
    } else if (top == 2) {
        if (first == 0x80) return static_cast<uint64_t>(readUInt32BE(in));
        if (first == 0x81) return readUInt64BE(in);
        return static_cast<uint64_t>(readUInt32BE(in));
    } else { // top == 3 => special encoding
        isEncoded = true;
        encType = first & 0x3F;
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
