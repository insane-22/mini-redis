#pragma once
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <mutex>
#include<optional>

struct ZSet {
    std::map<std::pair<double, std::string>, std::string> ordered;   
    std::unordered_map<std::string, double> lookup;                 
};

class SortedSetHandler {
public:
    explicit SortedSetHandler(int client_fd);

    bool isSortedSetCommand(const std::string& cmd);
    void handleCommand(const std::string& cmd, const std::vector<std::string>& args);
    void handleZAdd(const std::vector<std::string>& args);
    void handleZRank(const std::vector<std::string>& args);
    void handleZRange(const std::vector<std::string>& args);
    void handleZCard(const std::vector<std::string>& args);
    void handleZScore(const std::vector<std::string>& args);
    void handleZRem(const std::vector<std::string>& args);
    void sendResponse(const std::string& response);
    std::optional<double> getScore(const std::string& key, const std::string& member);

private:
    int client_fd;

    std::unordered_map<std::string, ZSet> sorted_sets;
    std::mutex store_mutex;

};
