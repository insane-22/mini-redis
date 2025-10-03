#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <mutex>
#include <sys/socket.h>

class PubSubHandler {
public:
    explicit PubSubHandler(int client_fd);

    bool isPubSubCommand(const std::string& cmd);
    void handleCommand(const std::string& cmd, const std::vector<std::string>& args);
    bool inSubscribedMode() const { return subscribed_mode; }

private:
    int client_fd;
    bool subscribed_mode = false;

    void handleSubscribe(const std::vector<std::string>& args);
    void handlePing();
    void handleUnsubscribe(const std::vector<std::string>& args);
    void handlePublish(const std::vector<std::string>& args);
    void sendResponse(const std::string& response);

    static std::unordered_map<int, std::unordered_set<std::string>> client_channels;
    static std::mutex store_mutex;
    static std::unordered_map<std::string, std::unordered_set<int>> channel_subscribers;
};