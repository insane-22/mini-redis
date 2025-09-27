#include "Handler.hpp"
#include <sys/socket.h>

Handler::Handler(int client_fd)
    : client_fd(client_fd),
      kvHandler(client_fd),
      listHandler(client_fd),
      streamHandler(client_fd) {}

void Handler::handleMessage(const std::string& message) {
    Parser parser;
    try {
        Command cmd = parser.parse(message);
        std::string name = cmd.name;

        if (name == "PING") {
            sendResponse("+PONG\r\n");
        } else if (name == "ECHO") {
            if (!cmd.args.empty())
                sendResponse("$" + std::to_string(cmd.args[0].size()) + "\r\n" + cmd.args[0] + "\r\n");
            else
                sendResponse("-ERR ECHO requires an argument\r\n");
        } else if (name == "TYPE") {
            handleTypeCommand(cmd.args);
        } else if (kvHandler.isKvCommand(name)) {
            kvHandler.handleCommand(name, cmd.args);
        } else if (listHandler.isListCommand(name)) {
            listHandler.handleCommand(name, cmd.args);
        } else if (streamHandler.isStreamCommand(name)) {
            streamHandler.handleCommand(name, cmd.args);
        } else {
            sendResponse("-ERR Unknown command\r\n");
        }
    } catch (const std::exception& e) {
        sendResponse("-ERR " + std::string(e.what()) + "\r\n");
    }
}

void Handler::handleTypeCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        sendResponse("-ERR TYPE requires a key\r\n");
        return;
    }

    const std::string& key = args[0];

    if (kvHandler.hasKey(key)) {
        sendResponse("+" + kvHandler.typeName() + "\r\n");
    } else if (listHandler.hasKey(key)) {
        sendResponse("+" + listHandler.typeName() + "\r\n");
    } else if (streamHandler.hasKey(key)) {
        sendResponse("+" + streamHandler.typeName() + "\r\n");
    } else {
        sendResponse("+none\r\n");
    }
}

void Handler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}
