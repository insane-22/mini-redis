#pragma once
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <mutex>

class SortedSetHandler {
public:
    explicit SortedSetHandler(int client_fd);

    bool isSortedSetCommand(const std::string& cmd);
    void handleCommand(const std::string& cmd, const std::vector<std::string>& args);

private:
    int client_fd;

    std::unordered_map<std::string, std::map<std::string, double>> sorted_sets;
    std::mutex store_mutex;

    void handleZAdd(const std::vector<std::string>& args);
    void handleZRank(const std::vector<std::string>& args);
    void sendResponse(const std::string& response);
};
