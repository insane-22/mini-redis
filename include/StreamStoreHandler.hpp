#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>

class StreamStoreHandler {
public:
    explicit StreamStoreHandler(int client_fd);

    bool isStreamCommand(const std::string& cmd);
    bool isWriteCommand(const std::string& cmd);
    void handleCommand(const std::string& cmd, const std::vector<std::string>& args);
    bool hasKey(const std::string& key);
    std::string typeName() const { return "stream"; }

private:
    int client_fd;

    void handleXadd(const std::vector<std::string>& args);
    void handleXrange(const std::vector<std::string>& args);
    void handleXread(const std::vector<std::string>& args);

    void sendResponse(const std::string& response);
    int64_t getCurrentTimeMs();

    using StreamEntry = std::pair<std::string, std::unordered_map<std::string, std::string>>;
    static std::unordered_map<std::string, std::vector<StreamEntry>> stream_store;
    static std::mutex store_mutex;
    static std::unordered_map<std::string, std::condition_variable> stream_cvs;

};
