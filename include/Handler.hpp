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

public:
    explicit Handler(int client_fd);
    void handleMessage(const std::string& message);

private:
    void sendResponse(const std::string& response);
    void handleTypeCommand(const std::vector<std::string>& args);

};
