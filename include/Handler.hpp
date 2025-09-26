#pragma once
#include <string>
#include <vector>
#include "Parser.hpp"
#include "KvStoreHandler.hpp"
#include "ListStoreHandler.hpp"
#include "StreamStoreHandler.hpp"

class Handler {
    int client_fd;

public:
    explicit Handler(int client_fd);
    void handleMessage(const std::string& message);

private:
    void sendResponse(const std::string& response);

    KvStoreHandler kvHandler;
    ListStoreHandler listHandler;
    StreamStoreHandler streamHandler;
};
