#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <chrono>

struct ValueWithExpiry {
    std::string value;
    std::optional<std::chrono::steady_clock::time_point> expiry;
};

class KvStoreHandler {
public:
    explicit KvStoreHandler(int client_fd);

    bool isKvCommand(const std::string& cmd);
    void handleCommand(const std::string& cmd, const std::vector<std::string>& args);
    bool hasKey(const std::string& key);
    std::string typeName() const { return "string"; }

private:
    int client_fd;
    void handleSet(const std::vector<std::string>& args);
    void handleGet(const std::vector<std::string>& args);
    void handleIncr(const std::vector<std::string>& args);

    void sendResponse(const std::string& response);

    static std::unordered_map<std::string, ValueWithExpiry> kv_store;
    static std::mutex store_mutex;
};
