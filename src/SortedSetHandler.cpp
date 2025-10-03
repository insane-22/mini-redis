#include "SortedSetHandler.hpp"
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <optional>
#include <sys/socket.h>

SortedSetHandler::SortedSetHandler(int client_fd) : client_fd(client_fd) {}

bool SortedSetHandler::isSortedSetCommand(const std::string& cmd) {
    return cmd == "ZADD" || cmd == "ZRANK";
}

void SortedSetHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "ZADD") handleZAdd(args);
    else if(cmd == "ZRANK") handleZRank(args);
    else sendResponse("-ERR Unsupported sorted set command\r\n");
}

void SortedSetHandler::handleZAdd(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        sendResponse("-ERR ZADD requires key, score and member\r\n");
        return;
    }

    const std::string& key = args[0];
    double score = std::stod(args[1]);
    const std::string& member = args[2];

    bool added = false;
    {
        std::lock_guard<std::mutex> lock(store_mutex);

        auto& zset = sorted_sets[key];
        if (zset.find(member) == zset.end()) {
            added = true;   
        }
        zset[member] = score;
    }

    sendResponse(":" + std::to_string(added ? 1 : 0) + "\r\n");
}

void SortedSetHandler::handleZRank(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        sendResponse("-ERR ZRANK requires key and member\r\n");
        return;
    }

    const std::string& key = args[0];
    const std::string& member = args[1];

    std::vector<std::pair<double, std::string>> sorted_members;
    {
        std::lock_guard<std::mutex> lock(store_mutex);

        auto it = sorted_sets.find(key);
        if (it == sorted_sets.end()) {
            sendResponse("$-1\r\n"); 
            return;
        }
        for (const auto& [m, s] : it->second) {
            sorted_members.emplace_back(s, m);
        }
    }

    std::sort(sorted_members.begin(), sorted_members.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });

    int rank = 0;
    bool found = false;
    for (size_t i = 0; i < sorted_members.size(); ++i) {
        if (sorted_members[i].second == member) {
            rank = static_cast<int>(i);
            found = true;
            break;
        }
    }

    if (!found) {
        sendResponse("$-1\r\n"); 
    } else {
        sendResponse(":" + std::to_string(rank) + "\r\n");
    }
}

void SortedSetHandler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}
