#pragma once
#include <string>
#include <vector>
#include "Parser.hpp"
#include "KvStoreHandler.hpp"
#include "ListStoreHandler.hpp"
#include "StreamStoreHandler.hpp"

class Handler {
    int client_fd;
    KvStoreHandler kvHandler;
    ListStoreHandler listHandler;
    StreamStoreHandler streamHandler;

    bool in_transaction = false;
    std::vector<std::pair<std::string, std::vector<std::string>>> queued_commands;

public:
    explicit Handler(int client_fd);
    void handleMessage(const std::string& message);

private:
    void sendResponse(const std::string& response);
    void handleTypeCommand(const std::vector<std::string>& args);
    void executeQueuedCommand(const std::string& cmd, const std::vector<std::string>& args);
};
