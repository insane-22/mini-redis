#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <sys/socket.h>

class ReplicationManager {
    std::vector<int> replica_fds; 
    std::mutex mtx;

public:
    void addReplica(int fd);
    void removeReplica(int fd);
    void propagateCommand(const std::vector<std::string>& cmdParts);
};
