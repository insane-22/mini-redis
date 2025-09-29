#include "ListStoreHandler.hpp"
#include <algorithm>
#include <iostream>
#include <chrono>
#include <sys/socket.h>
#include <unordered_set>

std::unordered_map<std::string, std::vector<std::string>> ListStoreHandler::list_store;
std::mutex ListStoreHandler::store_mutex;
std::condition_variable ListStoreHandler::cv;

ListStoreHandler::ListStoreHandler(int client_fd) : client_fd(client_fd) {}

bool ListStoreHandler::isListCommand(const std::string& cmd) {
    return cmd == "LPUSH" || cmd == "RPUSH" || cmd == "LRANGE" ||
           cmd == "LLEN" || cmd == "LPOP" || cmd == "BLPOP";
}

bool ListStoreHandler::isWriteCommand(const std::string& cmd) {
    static const std::unordered_set<std::string> writeCommands = {"LPUSH", "RPUSH"};
    return writeCommands.count(cmd) > 0;
}

void ListStoreHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "LPUSH") handleLpush(args);
    else if (cmd == "RPUSH") handleRpush(args);
    else if (cmd == "LRANGE") handleLrange(args);
    else if (cmd == "LLEN") handleLlen(args);
    else if (cmd == "LPOP") handleLpop(args);
    else if (cmd == "BLPOP") handleBlpop(args);
}
void ListStoreHandler::handleRpush(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        sendResponse("-ERR RPUSH requires a key and at least one value\r\n");
        return;
    }

    const std::string& key = tokens[0];
    std::vector<std::string> values(tokens.begin() + 1, tokens.end());
    size_t new_size;

    {
        std::lock_guard<std::mutex> lock(store_mutex);
        auto& list = list_store[key];
        list.insert(list.end(), values.begin(), values.end());
        new_size = list.size();
        cv.notify_all();
    }

    sendResponse(":" + std::to_string(new_size) + "\r\n");
}

void ListStoreHandler::handleLpush(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        sendResponse("-ERR LPUSH requires a key and at least one value\r\n");
        return;
    }

    const std::string& key = tokens[0];
    std::vector<std::string> values(tokens.begin() + 1, tokens.end());
    std::reverse(values.begin(), values.end());
    size_t new_size;

    {
        std::lock_guard<std::mutex> lock(store_mutex);
        auto& list = list_store[key];
        list.insert(list.begin(), values.begin(), values.end());
        new_size = list.size();
        cv.notify_all();
    }

    sendResponse(":" + std::to_string(new_size) + "\r\n");
}

void ListStoreHandler::handleLrange(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        sendResponse("-ERR LRANGE requires a key, start, and stop\r\n");
        return;
    }

    const std::string& key = tokens[0];
    int start, stop;

    try {
        start = std::stoi(tokens[1]);
        stop = std::stoi(tokens[2]);
    } catch (...) {
        sendResponse("-ERR Invalid start or stop value\r\n");
        return;
    }

    std::lock_guard<std::mutex> lock(store_mutex);
    auto it = list_store.find(key);
    if (it == list_store.end()) {
        sendResponse("*0\r\n");
        return;
    }

    const auto& list = it->second;
    int list_size = list.size();

    if (start < 0) start += list_size;
    if (stop < 0) stop += list_size;
    if (start < 0) start = 0;
    if (stop >= list_size) stop = list_size - 1;
    if (start > stop || start >= list_size) {
        sendResponse("*0\r\n");
        return;
    }

    int range_size = stop - start + 1;
    std::string response = "*" + std::to_string(range_size) + "\r\n";
    for (int i = start; i <= stop; ++i) {
        response += "$" + std::to_string(list[i].size()) + "\r\n" + list[i] + "\r\n";
    }

    sendResponse(response);
}

void ListStoreHandler::handleLlen(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        sendResponse("-ERR LLEN requires a key\r\n");
        return;
    }

    const std::string& key = tokens[0];
    std::lock_guard<std::mutex> lock(store_mutex);
    auto it = list_store.find(key);
    if (it == list_store.end()) {
        sendResponse(":0\r\n");
        return;
    }

    sendResponse(":" + std::to_string(it->second.size()) + "\r\n");
}

void ListStoreHandler::handleLpop(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        sendResponse("-ERR LPOP requires a key\r\n");
        return;
    }

    const std::string& key = tokens[0];
    std::lock_guard<std::mutex> lock(store_mutex);
    auto it = list_store.find(key);
    if (it == list_store.end() || it->second.empty()) {
        sendResponse("$-1\r\n");
        return;
    }

    auto& list = it->second;
    if (tokens.size() == 1) {
        std::string value = list.front();
        list.erase(list.begin());
        sendResponse("$" + std::to_string(value.size()) + "\r\n" + value + "\r\n");
    } else {
        int size = std::stoi(tokens[1]);
        if (size > list.size()) size = list.size();
        std::vector<std::string> values(list.begin(), list.begin() + size);
        list.erase(list.begin(), list.begin() + size);

        std::string response = "*" + std::to_string(size) + "\r\n";
        for (const auto& val : values) {
            response += "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
        }
        sendResponse(response);
    }
}

void ListStoreHandler::handleBlpop(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        sendResponse("-ERR BLPOP requires a key and timeout\r\n");
        return;
    }

    const std::string& key = tokens[0];
    double timeout = std::stod(tokens[1]);

    std::unique_lock<std::mutex> lock(store_mutex);
    auto list_has_data = [&]() {
        auto it = list_store.find(key);
        return it != list_store.end() && !it->second.empty();
    };

    if (!list_has_data()) {
        if (timeout == 0) {
            cv.wait(lock, list_has_data);
        } else {
            if (!cv.wait_for(lock, std::chrono::duration<double>(timeout), list_has_data)) {
                sendResponse("*-1\r\n");
                return;
            }
        }
    }

    auto it = list_store.find(key);
    if (it == list_store.end() || it->second.empty()) {
        sendResponse("*-1\r\n");
        return;
    }

    std::string value = it->second.front();
    it->second.erase(it->second.begin());

    std::string response = "*2\r\n";
    response += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
    response += "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
    sendResponse(response);
}

bool ListStoreHandler::hasKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(store_mutex);
    auto it = list_store.find(key);
    if (it != list_store.end()) {
        return true;
    }
    return false;
}

void ListStoreHandler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}