#include "Handler.hpp"
#include <iostream>
#include <sys/socket.h>

Handler::Handler(int client_fd) : client_fd(client_fd) {}

void Handler::handleMessage(const std::string& message) {
    Parser parser;
    try {
        Command cmd = parser.parse(message);
        if (cmd.name == "PING") {
            sendResponse("+PONG\r\n");
        } else if (cmd.name == "ECHO") {
            if (!cmd.args.empty()) {
                sendResponse("$" + std::to_string(cmd.args[0].size()) + "\r\n" + cmd.args[0] + "\r\n");
            } else {
                sendResponse("-Error: ECHO requires an argument\r\n");
            }
        } else {
            sendResponse("-Error: Unknown command\r\n");
        }
    } catch (const std::exception& e) {
        sendResponse("-Error: " + std::string(e.what()) + "\r\n");
    }
}

void Handler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}
