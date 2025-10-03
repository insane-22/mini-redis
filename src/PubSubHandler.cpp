#include "PubSubHandler.hpp"
// #include <sstream>
#include <algorithm>
#include <iostream>

std::unordered_map<int, std::unordered_set<std::string>> PubSubHandler::client_channels;
std::mutex PubSubHandler::store_mutex;

PubSubHandler::PubSubHandler(int client_fd) : client_fd(client_fd) {}

bool PubSubHandler::isPubSubCommand(const std::string& cmd) {
    return cmd == "SUBSCRIBE";
}

void PubSubHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "SUBSCRIBE") {
        handleSubscribe(args);
    } else {
        sendResponse("-ERR Unsupported PUB/SUB command\r\n");
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

    std::string response = "*3\r\n$9\r\nsubscribe\r\n";
    response += "$" + std::to_string(channel.size()) + "\r\n" + channel + "\r\n";
    response += ":" + std::to_string(count) + "\r\n";

    sendResponse(response);
}

void PubSubHandler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}
