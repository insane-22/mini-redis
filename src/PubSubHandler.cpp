#include "PubSubHandler.hpp"
// #include <sstream>
#include <algorithm>
#include <iostream>

std::unordered_map<int, std::unordered_set<std::string>> PubSubHandler::client_channels;
std::mutex PubSubHandler::store_mutex;

PubSubHandler::PubSubHandler(int client_fd) : client_fd(client_fd) {}

bool PubSubHandler::isPubSubCommand(const std::string& cmd) {
    return cmd == "SUBSCRIBE" || cmd=="PING";
}

void PubSubHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "SUBSCRIBE") {
        handleSubscribe(args);
    } else if (cmd == "PING") {
        handlePing();
    } else {
        if (subscribed_mode) {
            sendResponse("-ERR Can't execute '" + cmd + "': only (P|S)SUBSCRIBE / (P|S)UNSUBSCRIBE / PING / QUIT / RESET are allowed in this context\r\n");
        } else {
            sendResponse("-ERR Unsupported PUB/SUB command\r\n");
        }
    }
}

void PubSubHandler::handleSubscribe(const std::vector<std::string>& args) {
    if (args.empty()) {
        sendResponse("-ERR SUBSCRIBE requires a channel name\r\n");
        return;
    }

    const std::string& channel = args[0];

    int count = 0;
    {
        std::lock_guard<std::mutex> lock(store_mutex);
        auto& channels = client_channels[client_fd];
        channels.insert(channel);
        count = static_cast<int>(channels.size());
    }

    subscribed_mode = true;

    std::string response = "*3\r\n";
    response += "$9\r\nsubscribe\r\n";
    response += "$" + std::to_string(channel.size()) + "\r\n" + channel + "\r\n";
    response += ":" + std::to_string(count) + "\r\n";

    sendResponse(response);
}

void PubSubHandler::handlePing() {
    if (subscribed_mode) {
        sendResponse("*2\r\n$4\r\npong\r\n$0\r\n\r\n");
    } else {
        sendResponse("+PONG\r\n");
    }
}

void PubSubHandler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}
