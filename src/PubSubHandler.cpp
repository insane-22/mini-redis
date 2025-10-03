#include "PubSubHandler.hpp"
#include <algorithm>
#include <iostream>

std::unordered_map<int, std::unordered_set<std::string>> PubSubHandler::client_channels;
std::mutex PubSubHandler::store_mutex;
std::unordered_map<std::string, std::unordered_set<int>> PubSubHandler::channel_subscribers;

PubSubHandler::PubSubHandler(int client_fd) : client_fd(client_fd) {}

bool PubSubHandler::isPubSubCommand(const std::string& cmd) {
    return cmd == "SUBSCRIBE" || cmd=="PING" || cmd == "UNSUBSCRIBE" || cmd == "PUBLISH";
}

void PubSubHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "SUBSCRIBE") handleSubscribe(args);
    else if (cmd == "PING") handlePing();
    else if (cmd == "UNSUBSCRIBE") handleUnsubscribe(args);
    else if (cmd == "PUBLISH") handlePublish(args);
    else {
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
        channel_subscribers[channel].insert(client_fd);
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

void PubSubHandler::handleUnsubscribe(const std::vector<std::string>& args) {
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(store_mutex);
        auto it = client_channels.find(client_fd);
        if (it != client_channels.end()) {
            if (!args.empty()) {
                for (const auto& channel : args) {
                    it->second.erase(channel);
                    channel_subscribers[channel].erase(client_fd);
                    if (channel_subscribers[channel].empty()) channel_subscribers.erase(channel);
                }
            } else {
                for (const auto& ch : it->second) channel_subscribers[ch].erase(client_fd);
                it->second.clear();
            }
            count = static_cast<int>(it->second.size());
            if (it->second.empty()) subscribed_mode = false;
        }
    }

    if (!args.empty()) {
        for (const auto& channel : args) {
            std::string response = "*3\r\n";
            response += "$11\r\nunsubscribe\r\n";
            response += "$" + std::to_string(channel.size()) + "\r\n" + channel + "\r\n";
            response += ":" + std::to_string(count) + "\r\n";
            sendResponse(response);
        }
    }
}

void PubSubHandler::handlePublish(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        sendResponse("-ERR PUBLISH requires channel and message\r\n");
        return;
    }

    const std::string& channel = args[0];
    const std::string& message = args[1];
    int delivered = 0;

    {
        std::lock_guard<std::mutex> lock(store_mutex);
        auto it = channel_subscribers.find(channel);
        if (it != channel_subscribers.end()) {
            delivered = static_cast<int>(it->second.size());
            for (int fd : it->second) {
                if (fd == client_fd) continue; // optional: skip sender
                std::string resp = "*3\r\n";
                resp += "$7\r\nmessage\r\n";
                resp += "$" + std::to_string(channel.size()) + "\r\n" + channel + "\r\n";
                resp += "$" + std::to_string(message.size()) + "\r\n" + message + "\r\n";
                send(fd, resp.c_str(), resp.size(), 0);
            }
        }
    }

    sendResponse(":" + std::to_string(delivered) + "\r\n");
}



void PubSubHandler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}
