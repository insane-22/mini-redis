#include "KvStoreHandler.hpp"
#include <iostream>
#include <algorithm>
#include <sys/socket.h>

using Clock = std::chrono::steady_clock;

std::unordered_map<std::string, ValueWithExpiry> KvStoreHandler::kv_store;
std::mutex KvStoreHandler::store_mutex;

KvStoreHandler::KvStoreHandler(int client_fd) : client_fd(client_fd) {}

bool KvStoreHandler::isKvCommand(const std::string& cmd) {
    return cmd == "SET" || cmd == "GET" || cmd == "TYPE";
}

void KvStoreHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "SET") handleSet(args);
    else if (cmd == "GET") handleGet(args);
    else if (cmd == "TYPE") handleType(args);
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

void KvStoreHandler::handleType(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        sendResponse("-ERR TYPE requires a key\r\n");
        return;
    }

    const std::string& key = tokens[0];
    std::lock_guard<std::mutex> lock(store_mutex);
    if (kv_store.find(key) != kv_store.end())
        sendResponse("+string\r\n");
    else
        sendResponse("+none\r\n");
}

void KvStoreHandler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}
