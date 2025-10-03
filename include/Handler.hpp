#pragma once
#include <string>
#include <vector>
#include "KvStoreHandler.hpp"
#include "ListStoreHandler.hpp"
#include "StreamStoreHandler.hpp"
#include "ReplicationManager.hpp"
#include "Rdb.hpp"
#include "Parser.hpp"
#include "RdbReader.hpp"
#include "PubSubHandler.hpp"
#include "SortedSetHandler.hpp"

class Handler {
    int client_fd;
    bool isReplica;
    bool in_transaction = false;
    std::string rdb_dir;
    std::string rdb_filename;

    RdbReader rdbReader;
    KvStoreHandler kvHandler;
    ListStoreHandler listHandler;
    StreamStoreHandler streamHandler;
    PubSubHandler pubSubHandler;
    SortedSetHandler sortedSetHandler;
    ReplicationManager* replManager = nullptr;

    std::vector<std::pair<std::string, std::vector<std::string>>> queued_commands;

public:
    Handler(int client_fd, bool replica, ReplicationManager* rm = nullptr, const std::string& dir = "./", const std::string& filename = "dump.rdb");

    void handleMessage(const std::string& message);
    void handleTypeCommand(const std::vector<std::string>& args);
    void executeCommand(const std::string& name, const std::vector<std::string>& args);
    void executeQueuedCommand(const std::string& cmd, const std::vector<std::string>& args);
    void propagateIfWrite(const std::string& name, const std::vector<std::string>& args);
    void sendResponse(const std::string& response);
};
