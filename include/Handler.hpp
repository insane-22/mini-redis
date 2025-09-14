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
    void handleRpushCommand(const std::vector<std::string>& tokens);
    void handleLrangeCommand(const std::vector<std::string>& tokens);
    void handleLpushCommand(const std::vector<std::string>& tokens);
    void handleLlenCommand(const std::vector<std::string>& tokens);
    void handleLpopCommand(const std::vector<std::string>& tokens);
    void handleBlpopCommand(const std::vector<std::string>& tokens);
    void handleTypeCommand(const std::vector<std::string>& tokens);
    void handleXaddCommand(const std::vector<std::string>& tokens);
};
