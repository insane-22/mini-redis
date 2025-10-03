#include "SortedSetHandler.hpp"
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <optional>
#include <sys/socket.h>
#include <algorithm>

SortedSetHandler::SortedSetHandler(int client_fd) : client_fd(client_fd) {}

bool SortedSetHandler::isSortedSetCommand(const std::string& cmd) {
    return cmd == "ZADD" || cmd == "ZRANK" || cmd == "ZRANGE" ||
        cmd == "ZCARD" || cmd == "ZSCORE" || cmd == "ZREM";
}

void SortedSetHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "ZADD") handleZAdd(args);
    else if(cmd == "ZRANK") handleZRank(args);
    else if(cmd == "ZRANGE") handleZRange(args);
    else if(cmd == "ZCARD") handleZCard(args);
    else if(cmd == "ZSCORE") handleZScore(args);
    else if(cmd == "ZREM") handleZRem(args);
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
        auto it = zset.lookup.find(member);
        if (it != zset.lookup.end()) {
            zset.ordered.erase({it->second, member});
        } else {
            added = true;
        }
        zset.lookup[member] = score;
        zset.ordered[{score, member}] = member;
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

    int rank = 0;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(store_mutex);

        auto it = sorted_sets.find(key);
        if (it == sorted_sets.end()) {
            sendResponse("$-1\r\n");
            return;
        }

        const auto& zset = it->second;

        auto lookupIt = zset.lookup.find(member);
        if (lookupIt == zset.lookup.end()) {
            sendResponse("$-1\r\n");
            return;
        }
        for (const auto& [score_member, mem] : zset.ordered) {
            if (mem == member) {
                found = true;
                break;
            }
            rank++;
        }
    }

    if (!found) {
        sendResponse("$-1\r\n");
    } else {
        sendResponse(":" + std::to_string(rank) + "\r\n");
    }
}

void SortedSetHandler::handleZRange(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        sendResponse("-ERR ZRANGE requires key, start and stop\r\n");
        return;
    }

    const std::string& key = args[0];
    int start = std::stoi(args[1]);
    int stop = std::stoi(args[2]);

    std::vector<std::string> result;

    {
        std::lock_guard<std::mutex> lock(store_mutex);

        auto it = sorted_sets.find(key);
        if (it == sorted_sets.end()) {
            sendResponse("*0\r\n"); 
            return;
        }

        const auto& zset = it->second;
        int n = static_cast<int>(zset.ordered.size());

        if (start < 0) start = n + start;
        if (stop < 0) stop = n + stop;

        if (start < 0) start = 0;
        if (stop < 0) stop = 0;
        if (stop >= n) stop = n - 1;

        if (start > stop || start >= n) {
            sendResponse("*0\r\n");
            return;
        }

        int idx = 0;
        for (const auto& [score_member, mem] : zset.ordered) {
            if (idx >= start && idx <= stop) {
                result.push_back(mem);
            }
            if (idx > stop) break;
            idx++;
        }
    }

    std::ostringstream resp;
    resp << "*" << result.size() << "\r\n";
    for (const auto& mem : result) {
        resp << "$" << mem.size() << "\r\n" << mem << "\r\n";
    }

    sendResponse(resp.str());
}

void SortedSetHandler::handleZCard(const std::vector<std::string>& args) {
    if (args.size() < 1) {
        sendResponse("-ERR ZCARD requires key\r\n");
        return;
    }

    const std::string& key = args[0];
    int card = 0;

    {
        std::lock_guard<std::mutex> lock(store_mutex);

        auto it = sorted_sets.find(key);
        if (it != sorted_sets.end()) {
            card = static_cast<int>(it->second.lookup.size());
        }
    }

    sendResponse(":" + std::to_string(card) + "\r\n");
}

void SortedSetHandler::handleZScore(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        sendResponse("-ERR ZSCORE requires key and member\r\n");
        return;
    }

    const std::string& key = args[0];
    const std::string& member = args[1];
    std::optional<double> score;

    {
        std::lock_guard<std::mutex> lock(store_mutex);

       auto it = sorted_sets.find(key);
        if (it == sorted_sets.end()) {
            sendResponse("$-1\r\n"); 
            return;
        }

        auto mit = it->second.lookup.find(member);
        if (mit == it->second.lookup.end()) {
            sendResponse("$-1\r\n");
            return;
        }

        score = mit->second; 
    }

    std::ostringstream oss;
    oss.precision(17);
    oss << *score;
    std::string scoreStr = oss.str();

    std::string result = "$" + std::to_string(scoreStr.size()) + "\r\n" + scoreStr + "\r\n";
    sendResponse(result);
}

void SortedSetHandler::handleZRem(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        sendResponse("-ERR ZREM requires key and member\r\n");
        return;
    }

    const std::string& key = args[0];
    const std::string& member = args[1];
    bool removed = false;

    {
        std::lock_guard<std::mutex> lock(store_mutex);

        auto it = sorted_sets.find(key);
        if (it == sorted_sets.end()) {
            sendResponse(":0\r\n");
            return;
        }

        auto& zset = it->second;
        auto mit = zset.lookup.find(member);
        if (mit != zset.lookup.end()) {
            double score = mit->second;
            zset.ordered.erase({score, member});
            zset.lookup.erase(mit);
            removed = true;
        }
    }

    sendResponse(":" + std::to_string(removed ? 1 : 0) + "\r\n");
}

std::optional<double> SortedSetHandler::getScore(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(store_mutex);

    auto it = sorted_sets.find(key);
    if (it == sorted_sets.end()) return std::nullopt;

    auto mit = it->second.lookup.find(member);
    if (mit == it->second.lookup.end()) return std::nullopt;

    return mit->second;
}

void SortedSetHandler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}
