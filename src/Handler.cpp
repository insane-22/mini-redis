#include "Handler.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <sstream>
#include <iostream>

Handler::Handler(int client_fd, bool replica, ReplicationManager* rm, const std::string& dir, const std::string& filename)
    : client_fd(client_fd), isReplica(replica),
      rdbReader((dir.empty() ? filename : (dir + "/" + filename))),   
      kvHandler(client_fd, &rdbReader),
      listHandler(client_fd),
      streamHandler(client_fd), replManager(rm), 
      pubSubHandler(client_fd),
      sortedSetHandler(client_fd),
      rdb_dir(dir), rdb_filename(filename) {
        rdbReader.load();
      }

void Handler::handleMessage(const std::string& message) {
    Parser parser;
    try {
        Command cmd = parser.parse(message);
        std::string name = cmd.name;

        if (pubSubHandler.inSubscribedMode()) {
            if (!pubSubHandler.isPubSubCommand(name) && name != "QUIT" && name != "RESET") {
                sendResponse("-ERR Can't execute '" + name + "': only (P|S)SUBSCRIBE / (P|S)UNSUBSCRIBE / PING / QUIT / RESET are allowed in this context\r\n");
                return;
            }
        }

        if (name == "ECHO") {
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
                        executeCommand(qname, qargs);
                        propagateIfWrite(qname, qargs);
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
        } else if (name == "REPLCONF") {
            sendResponse("+OK\r\n");
        } else if (name == "PSYNC") {
            if (cmd.args.size() == 2 && cmd.args[0] == "?" && cmd.args[1] == "-1") {
                std::string replid = "8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb";
                std::string response = "+FULLRESYNC " + replid + " 0\r\n";
                sendResponse(response);
                size_t rdb_len = Rdb::emptyRdbLen();
                std::string header = "$" + std::to_string(rdb_len) + "\r\n";
                send(client_fd, header.c_str(), header.size(), 0);
                send(client_fd, Rdb::emptyRdbData(), rdb_len, 0);
                if (replManager) replManager->addReplica(client_fd);
            } else {
                sendResponse("-ERR invalid PSYNC args\r\n");
            }
        } else if (kvHandler.isKvCommand(name) ||
                   listHandler.isListCommand(name) ||
                   streamHandler.isStreamCommand(name) ||
                   pubSubHandler.isPubSubCommand(name) || 
                   sortedSetHandler.isSortedSetCommand(name)) {
            if (in_transaction) {
                queued_commands.emplace_back(name, cmd.args);
                sendResponse("+QUEUED\r\n");
            } else {
                executeCommand(name, cmd.args);
                propagateIfWrite(name, cmd.args);
            }
        } else if (name == "INFO" || name == "info") {
            if (cmd.args.size() == 1 && cmd.args[0] == "replication") {
                std::string info;
                if (!isReplica) {
                    std::string replid = "8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb"; 
                    info  = "role:master\r\n";
                    info += "master_replid:" + replid + "\r\n";
                    info += "master_repl_offset:0";
                } else {
                    info = "role:slave";
                }
                std::string response = "$" + std::to_string(info.size()) + "\r\n" + info + "\r\n";
                sendResponse(response);            
            }
        } else if (name == "CONFIG" && !cmd.args.empty() && cmd.args[0] == "GET") {
            if (cmd.args.size() < 2) {
                sendResponse("-ERR CONFIG GET requires a parameter\r\n");
            } else {
                std::string param = cmd.args[1];
                std::string value;

                if (param == "dir") value = rdb_dir;
                else if (param == "dbfilename") value = rdb_filename;
                else value = "";
    
                std::string resp = "*2\r\n";
                resp += "$" + std::to_string(param.size()) + "\r\n" + param + "\r\n";
                resp += "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";

                sendResponse(resp);
            }
        } else {
            sendResponse("-ERR Unknown command\r\n");
        }

    } catch (const std::exception& e) {
        sendResponse("-ERR " + std::string(e.what()) + "\r\n");
    }
}

void Handler::executeCommand(const std::string& name, const std::vector<std::string>& args) {
    if (kvHandler.isKvCommand(name)) kvHandler.handleCommand(name, args);
    else if (listHandler.isListCommand(name)) listHandler.handleCommand(name, args);
    else if (streamHandler.isStreamCommand(name)) streamHandler.handleCommand(name, args);
    else if (pubSubHandler.isPubSubCommand(name)) pubSubHandler.handleCommand(name, args);
    else if (sortedSetHandler.isSortedSetCommand(name)) sortedSetHandler.handleCommand(name, args);
}

void Handler::propagateIfWrite(const std::string& name, const std::vector<std::string>& args) {
    if (!replManager) return;

    bool isWrite = (kvHandler.isWriteCommand(name) ||
                    listHandler.isWriteCommand(name) ||
                    streamHandler.isWriteCommand(name));
    if (!isWrite) return;

    std::vector<std::string> cmdParts = {name};
    cmdParts.insert(cmdParts.end(), args.begin(), args.end());
    replManager->propagateCommand(cmdParts);
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
    executeCommand(cmd, args);
}

void Handler::sendResponse(const std::string& response) {
    size_t total = 0;
    while (total < response.size()) {
        ssize_t sent = send(client_fd, response.data() + total, response.size() - total, 0);
        if (sent <= 0) {
            break;
        }
        total += static_cast<size_t>(sent);
    }
}
