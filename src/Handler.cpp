#include "Handler.hpp"
#include <iostream>
#include <sys/socket.h>
#include <unordered_map>
#include <chrono>
#include <optional>

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

struct ValueWithExpiry {
    std::string value;
    std::optional<TimePoint> expiry;
};

Handler::Handler(int client_fd) : client_fd(client_fd) {}
static std::unordered_map<std::string, ValueWithExpiry> kv_store;
static std::unordered_map<std::string, std::vector<std::string>> list_store;

void Handler::handleMessage(const std::string& message) {
    Parser parser;
    try {
        Command cmd = parser.parse(message);
        if (cmd.name == "PING") {
            sendResponse("+PONG\r\n");
        } else if (cmd.name == "ECHO") {
            if (!cmd.args.empty()) {
                sendResponse("$" + std::to_string(cmd.args[0].size()) + "\r\n" + cmd.args[0] + "\r\n");
            } else {
                sendResponse("-Error: ECHO requires an argument\r\n");
            }
        } else if (cmd.name == "SET") {
            handleSetCommand(cmd.args);
        } else if (cmd.name == "GET") {
            handleGetCommand(cmd.args); 
        } else if(cmd.name == "RPUSH") {
            handleRpushCommand(cmd.args);
        } else if(cmd.name =="LRANGE"){
            handleLrangeCommand(cmd.args);
        } else if(cmd.name =="LPUSH"){
            handleLpushCommand(cmd.args);
        } else if(cmd.name =="LLEN"){
            handleLlenCommand(cmd.args);
        } else {
            sendResponse("-Error: Unknown command\r\n");
        }
    } catch (const std::exception& e) {
        sendResponse("-Error: " + std::string(e.what()) + "\r\n");
    }
}

void Handler::handleSetCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        sendResponse("-Error: SET requires a key and value\r\n");
        return;
    }

    const std::string& key = tokens[0];
    const std::string& value = tokens[1];

    std::optional<TimePoint> expiry = std::nullopt;

    if (tokens.size() >= 4) {
        std::string option = tokens[2];
        std::transform(option.begin(), option.end(), option.begin(), ::toupper);
        if (option == "PX") {
            try {
                int64_t ms = std::stoll(tokens[3]);
                expiry = Clock::now() + std::chrono::milliseconds(ms);
            } catch (...) {
                sendResponse("-Error: Invalid PX value\r\n");
                return;
            }
        }
    }

    kv_store[key] = { value, expiry };
    sendResponse("+OK\r\n");
}

void Handler::handleGetCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 1) {
        sendResponse("-Error: GET requires a key\r\n");
        return;
    }

    const std::string& key = tokens[0];
    auto it = kv_store.find(key);

    if (it != kv_store.end()) {
        if (it->second.expiry && Clock::now() >= it->second.expiry.value()) {
            kv_store.erase(it);
            sendResponse("$-1\r\n");
            return;
        }
        const std::string& value = it->second.value;
        sendResponse("$" + std::to_string(value.size()) + "\r\n" + value + "\r\n");
    } else {
        sendResponse("$-1\r\n"); 
    }
}

void Handler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}

void Handler::handleRpushCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        sendResponse("-Error: RPUSH requires a key and at least one value\r\n");
        return;
    }

    const std::string& key = tokens[0];
    std::vector<std::string> values(tokens.begin() + 1, tokens.end());

    auto& list = list_store[key];
    list.insert(list.end(), values.begin(), values.end());

    sendResponse(":" + std::to_string(list.size()) + "\r\n");
}   

void Handler::handleLrangeCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        sendResponse("-Error: LRANGE requires a key, start, and stop\r\n");
        return;
    }

    const std::string& key = tokens[0];
    int start, stop;

    try {
        start = std::stoi(tokens[1]);
        stop = std::stoi(tokens[2]);
    } catch (...) {
        sendResponse("-Error: Invalid start or stop value\r\n");
        return;
    }

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

void Handler::handleLpushCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        sendResponse("-Error: LPUSH requires a key and at least one value\r\n");
        return;
    }

    const std::string& key = tokens[0];
    std::vector<std::string> values(tokens.begin() + 1, tokens.end());
    reverse(values.begin(), values.end());

    auto& list = list_store[key];
    list.insert(list.begin(), values.begin(), values.end());

    sendResponse(":" + std::to_string(list.size()) + "\r\n");
}

void Handler::handleLlenCommand(const std::vector<std::string>&tokens){
    if (tokens.size() < 1) {
        sendResponse("-Error: LLEN requires a key\r\n");
        return;
    }

    const std::string& key = tokens[0];

    auto it = list_store.find(key);
    if (it == list_store.end()) {
        sendResponse("*0\r\n");
        return;
    }

    auto& list = list_store[key];
    sendResponse(":" + std::to_string(list.size()) + "\r\n");
}
