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
        } else if (name == "MULTI") {
            if (in_transaction) {
                sendResponse("-ERR MULTI calls cannot be nested\r\n");
            } else {
                in_transaction = true;
                queued_commands.clear();
                sendResponse("+OK\r\n");
            }
        } else if (name == "EXEC") {
            if (!in_transaction) {
                sendResponse("-ERR EXEC without MULTI\r\n");
            } else {
                in_transaction = false;
                if (queued_commands.empty()) {
                    sendResponse("*0\r\n");
                } else {
                    sendResponse("*" + std::to_string(queued_commands.size()) + "\r\n");
                    for (auto& [qname, qargs] : queued_commands) {
                        executeQueuedCommand(qname, qargs);
                    }
                }
                queued_commands.clear();
            }
        } else if (name == "DISCARD") {
            if (!in_transaction) {
                sendResponse("-ERR DISCARD without MULTI\r\n");
            } else {
                in_transaction = false;
                queued_commands.clear();
                sendResponse("+OK\r\n");
            }
        } else if (name == "TYPE") {
            handleTypeCommand(cmd.args);
        } else if (kvHandler.isKvCommand(name) ||
                   listHandler.isListCommand(name) ||
                   streamHandler.isStreamCommand(name)) {
            if (in_transaction) {
                queued_commands.emplace_back(name, cmd.args);
                sendResponse("+QUEUED\r\n");
            } else {
                if (kvHandler.isKvCommand(name)) {
                    kvHandler.handleCommand(name, cmd.args);
                } else if (listHandler.isListCommand(name)) {
                    listHandler.handleCommand(name, cmd.args);
                } else if (streamHandler.isStreamCommand(name)) {
                    streamHandler.handleCommand(name, cmd.args);
                }
            }
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

void Handler::executeQueuedCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (kvHandler.isKvCommand(cmd)) {
        kvHandler.handleCommand(cmd, args);
    } else if (listHandler.isListCommand(cmd)) {
        listHandler.handleCommand(cmd, args);
    } else if (streamHandler.isStreamCommand(cmd)) {
        streamHandler.handleCommand(cmd, args);
    } else {
        sendResponse("-ERR Unknown command\r\n");
    }
}

void Handler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}
