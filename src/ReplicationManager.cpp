#include "ReplicationManager.hpp"
#include <sstream>

void ReplicationManager::addReplica(int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    replica_fds.push_back(fd);
}

void ReplicationManager::removeReplica(int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    replica_fds.erase(std::remove(replica_fds.begin(), replica_fds.end(), fd), replica_fds.end());
}

void ReplicationManager::propagateCommand(const std::vector<std::string>& cmdParts) {
    std::ostringstream oss;
    oss << "*" << cmdParts.size() << "\r\n";
    for (const auto& arg : cmdParts) {
        oss << "$" << arg.size() << "\r\n" << arg << "\r\n";
    }
    std::string resp = oss.str();

    std::lock_guard<std::mutex> lock(mtx);
    for (int fd : replica_fds) {
        send(fd, resp.c_str(), resp.size(), 0);
    }
}
