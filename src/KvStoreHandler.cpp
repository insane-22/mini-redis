#include "KvStoreHandler.hpp"
#include <iostream>
#include <algorithm>
#include <sys/socket.h>
#include <unordered_set>
#include <set>
#include <chrono>
#include <sstream>
#include "RdbReader.hpp"

using Clock = std::chrono::steady_clock;

std::unordered_map<std::string, ValueWithExpiry> KvStoreHandler::kv_store;
std::mutex KvStoreHandler::store_mutex;

KvStoreHandler::KvStoreHandler(int client_fd, RdbReader* rdb): client_fd(client_fd), rdbReader(rdb) {}

bool KvStoreHandler::isKvCommand(const std::string& cmd) {
    return cmd == "SET" || cmd == "GET" || cmd == "INCR" || cmd == "KEYS";
}

bool KvStoreHandler::isWriteCommand(const std::string& cmd) {
    static const std::unordered_set<std::string> writeCommands = {"SET", "INCR"};
    return writeCommands.count(cmd) > 0;
}

void KvStoreHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "SET") handleSet(args);
    else if (cmd == "GET") handleGet(args);
    else if (cmd == "INCR") handleIncr(args);
    else if (cmd == "KEYS") handleKeys(args);
}

int64_t getCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void KvStoreHandler::handleSet(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        sendResponse("-ERR SET requires key and value\r\n");
        return;
    }

    const std::string& key = tokens[0];
    const std::string& value = tokens[1];
    std::optional<Clock::time_point> expiry = std::nullopt;

    if (tokens.size() >= 4) {
        std::string option = tokens[2];
        std::transform(option.begin(), option.end(), option.begin(), ::toupper);
        if (option == "PX") {
            try {
                int64_t ms = std::stoll(tokens[3]);
                expiry = Clock::now() + std::chrono::milliseconds(ms);
            } catch (...) {
                sendResponse("-ERR Invalid PX value\r\n");
                return;
            }
        }
    }

    std::lock_guard<std::mutex> lock(store_mutex);
    kv_store[key] = {value, expiry};
    sendResponse("+OK\r\n");
}

void KvStoreHandler::handleGet(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        sendResponse("-ERR GET requires a key\r\n");
        return;
    }

    const std::string& key = tokens[0];

    if (rdbReader) {
        auto val = rdbReader->getValue(0, key);
        if (val.has_value()) {
            sendResponse("$" + std::to_string(val->size()) + "\r\n" + *val + "\r\n");
            return;
        }
    }

    std::lock_guard<std::mutex> lock(store_mutex);
    auto it = kv_store.find(key);
    if (it != kv_store.end()) {
        if (it->second.expiry && Clock::now() >= it->second.expiry.value()) {
            kv_store.erase(it);
            sendResponse("$-1\r\n");
            return;
        }
        sendResponse("$" + std::to_string(it->second.value.size()) + "\r\n" + it->second.value + "\r\n");
    } else {
        sendResponse("$-1\r\n");
    }
}

void KvStoreHandler::handleKeys(const std::vector<std::string>& tokens) {
    if (tokens.size() != 1 || tokens[0] != "*") {
        sendResponse("-ERR Only KEYS * supported\r\n");
        return;
    }

    std::set<std::string> keys_set;
    if (rdbReader) {
        auto rdbKeys = rdbReader->getKeys(0);
        for (const auto &k : rdbKeys) keys_set.insert(k);
    }

    {
        std::lock_guard<std::mutex> lock(store_mutex);
        for (auto it = kv_store.begin(); it != kv_store.end();) {
            if (it->second.expiry && Clock::now() >= it->second.expiry.value()) {
                it = kv_store.erase(it);
            } else {
                keys_set.insert(it->first);
                ++it;
            }
        }
    }

    std::ostringstream out;
    out << "*" << keys_set.size() << "\r\n";
    for (const auto &k : keys_set) {
        out << "$" << k.size() << "\r\n" << k << "\r\n";
    }
    sendResponse(out.str());
}

bool KvStoreHandler::hasKey(const std::string& key) {
    {
        std::lock_guard<std::mutex> lock(store_mutex);
        auto it = kv_store.find(key);
        if (it != kv_store.end()) {
            if (it->second.expiry && Clock::now() >= it->second.expiry.value()) {
                kv_store.erase(it);
                return false;
            }
            return true;
        }
    }
    if (rdbReader) {
        auto v = rdbReader->getValue(0, key);
        return v.has_value();
    }
    return false;
}

void KvStoreHandler::handleIncr(const std::vector<std::string>& tokens) {
    if (tokens.size() != 1) {
        sendResponse("-ERR INCR requires exactly one key\r\n");
        return;
    }

    const std::string& key = tokens[0];

    std::lock_guard<std::mutex> lock(store_mutex);
    auto it = kv_store.find(key);
    if (it != kv_store.end()) {
        if (it->second.expiry && Clock::now() >= it->second.expiry.value()) {
            kv_store.erase(it);
            kv_store[key] = {"1", std::nullopt};
            sendResponse(":1\r\n");
            return;
        }
        try {
            int64_t current_value = std::stoll(it->second.value);
            current_value++;
            it->second.value = std::to_string(current_value);
            sendResponse(":" + std::to_string(current_value) + "\r\n");
        } catch (...) {
            sendResponse("-ERR value is not an integer or out of range\r\n");
        }
    } else {
        kv_store[key] = {"1", std::nullopt};
        sendResponse(":1\r\n");
    }
}

void KvStoreHandler::sendResponse(const std::string& response) {
    size_t total = 0;
    while (total < response.size()) {
        ssize_t sent = send(client_fd, response.data() + total, response.size() - total, 0);
        if (sent <= 0) {
            break;
        }
        total += static_cast<size_t>(sent);
    }
}
