#include "Handler.hpp"
#include <iostream>
#include <sys/socket.h>
#include <unordered_map>

Handler::Handler(int client_fd) : client_fd(client_fd) {}
static std::unordered_map<std::string, std::string> kv_store;

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
    kv_store[key] = value;
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
        const std::string& value = it->second;
        sendResponse("$" + std::to_string(value.size()) + "\r\n" + value + "\r\n");
    } else {
        sendResponse("$-1\r\n");
    }
}

void Handler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}
