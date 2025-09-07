#pragma once
#include "Parser.hpp"

class Handler {
    int client_fd;
public:
    Handler(int client_fd);
    void handleMessage(const std::string& message);
private:
    void sendResponse(const std::string& response);
    void handleSetCommand(const std::vector<std::string>& tokens);
    void handleGetCommand(const std::vector<std::string>& tokens);
};
