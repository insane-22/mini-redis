#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>

class ListStoreHandler {
public:
    explicit ListStoreHandler(int client_fd);

    bool isListCommand(const std::string& cmd);
    void handleCommand(const std::string& cmd, const std::vector<std::string>& args);
    bool hasKey(const std::string& key);
    std::string typeName() const { return "list"; }

private:
    int client_fd;
    void handleRpush(const std::vector<std::string>& args);
    void handleLpush(const std::vector<std::string>& args);
    void handleLrange(const std::vector<std::string>& args);
    void handleLlen(const std::vector<std::string>& args);
    void handleLpop(const std::vector<std::string>& args);
    void handleBlpop(const std::vector<std::string>& args);

    void sendResponse(const std::string& response);

    static std::unordered_map<std::string, std::vector<std::string>> list_store;
    static std::mutex store_mutex;
    static std::condition_variable cv;
};
