#include "Handler.hpp"
#include <iostream>
#include <sys/socket.h>
#include <unordered_map>
#include <chrono>
#include <optional>
#include<mutex>
#include<condition_variable>

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

struct ValueWithExpiry {
    std::string value;
    std::optional<TimePoint> expiry;
};

int64_t getCurrentTimeMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

Handler::Handler(int client_fd) : client_fd(client_fd) {}
static std::unordered_map<std::string, ValueWithExpiry> kv_store;
static std::unordered_map<std::string, std::vector<std::string>> list_store;
static std::unordered_map<std::string, std::vector<std::pair<std::string, std::unordered_map<std::string, std::string>>>> stream_store;
static std::mutex store_mutex;
static std::condition_variable cv;

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
                sendResponse("-ERR ECHO requires an argument\r\n");
            }
        } else if (cmd.name == "SET") {
            handleSetCommand(cmd.args);
        } else if (cmd.name == "GET") {
            handleGetCommand(cmd.args); 
        } else if(cmd.name == "RPUSH") {
            handleRpushCommand(cmd.args);
        } else if(cmd.name =="LRANGE"){
            handleLrangeCommand(cmd.args);
        } else if(cmd.name =="LPUSH"){
            handleLpushCommand(cmd.args);
        } else if(cmd.name =="LLEN"){
            handleLlenCommand(cmd.args);
        } else if(cmd.name =="LPOP"){
            handleLpopCommand(cmd.args);
        } else if (cmd.name == "BLPOP"){
            handleBlpopCommand(cmd.args);
        } else if (cmd.name == "TYPE"){
            handleTypeCommand(cmd.args);
        } else if (cmd.name == "XADD"){
            handleXaddCommand(cmd.args);
        } else {
            sendResponse("-ERR Unknown command\r\n");
        }
    } catch (const std::exception& e) {
        sendResponse("-ERR " + std::string(e.what()) + "\r\n");
    }
}

void Handler::handleSetCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        sendResponse("-ERR SET requires a key and value\r\n");
        return;
    }

    const std::string& key = tokens[0];
    const std::string& value = tokens[1];

    std::optional<TimePoint> expiry = std::nullopt;

    if (tokens.size() >= 4) {
        std::string option = tokens[2];
        std::transform(option.begin(), option.end(), option.begin(), ::toupper);
        if (option == "PX") {
            try {
                int64_t ms = std::stoll(tokens[3]);
                expiry = Clock::now() + std::chrono::milliseconds(ms);
            } catch (...) {
                sendResponse("-ERR Invalid PX value\r\n");
                return;
            }
        }
    }

    kv_store[key] = { value, expiry };
    sendResponse("+OK\r\n");
}

void Handler::handleGetCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 1) {
        sendResponse("-ERR GET requires a key\r\n");
        return;
    }

    const std::string& key = tokens[0];
    auto it = kv_store.find(key);

    if (it != kv_store.end()) {
        if (it->second.expiry && Clock::now() >= it->second.expiry.value()) {
            kv_store.erase(it);
            sendResponse("$-1\r\n");
            return;
        }
        const std::string& value = it->second.value;
        sendResponse("$" + std::to_string(value.size()) + "\r\n" + value + "\r\n");
    } else {
        sendResponse("$-1\r\n"); 
    }
}

void Handler::handleTypeCommand(const std::vector<std::string>&tokens){
    if (tokens.size() < 1) {
        sendResponse("-ERR GET requires a key\r\n");
        return;
    }

    const std::string& key = tokens[0];
    auto it = kv_store.find(key);
    auto it2 = stream_store.find(key);

    if(it != kv_store.end()){
        sendResponse("+string\r\n");
    }else if(it2 !=  stream_store.end()){
        sendResponse("+stream\r\n");
    }else{
        sendResponse("+none\r\n");
    }
}

void Handler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}

void Handler::handleRpushCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        sendResponse("-ERR RPUSH requires a key and at least one value\r\n");
        return;
    }

    const std::string& key = tokens[0];
    std::vector<std::string> values(tokens.begin() + 1, tokens.end());

    {
        std::lock_guard<std::mutex> lock(store_mutex);
        auto& list = list_store[key];
        list.insert(list.end(), values.begin(), values.end());
        cv.notify_all();
    }

    sendResponse(":" + std::to_string(list_store[key].size()) + "\r\n");
}   

void Handler::handleLrangeCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        sendResponse("-ERR LRANGE requires a key, start, and stop\r\n");
        return;
    }

    const std::string& key = tokens[0];
    int start, stop;

    try {
        start = std::stoi(tokens[1]);
        stop = std::stoi(tokens[2]);
    } catch (...) {
        sendResponse("-ERR Invalid start or stop value\r\n");
        return;
    }

    auto it = list_store.find(key);
    if (it == list_store.end()) {
        sendResponse("*0\r\n");
        return;
    }

    const auto& list = it->second;
    int list_size = list.size();

    if (start < 0) start += list_size;
    if (stop < 0) stop += list_size;

    if (start < 0) start = 0;
    if (stop >= list_size) stop = list_size - 1;
    if (start > stop || start >= list_size) {
        sendResponse("*0\r\n");
        return;
    }

    int range_size = stop - start + 1;
    std::string response = "*" + std::to_string(range_size) + "\r\n";
    for (int i = start; i <= stop; ++i) {
        response += "$" + std::to_string(list[i].size()) + "\r\n" + list[i] + "\r\n";
    }

    sendResponse(response);
}

void Handler::handleLpushCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        sendResponse("-ERR LPUSH requires a key and at least one value\r\n");
        return;
    }

    const std::string& key = tokens[0];
    std::vector<std::string> values(tokens.begin() + 1, tokens.end());
    reverse(values.begin(), values.end());

    {
        std::lock_guard<std::mutex> lock(store_mutex);
        auto& list = list_store[key];
        list.insert(list.begin(), values.begin(), values.end());
        cv.notify_all();
    }

    sendResponse(":" + std::to_string(list_store[key].size()) + "\r\n");
}

void Handler::handleLlenCommand(const std::vector<std::string>&tokens){
    if (tokens.size() < 1) {
        sendResponse("-ERR LLEN requires a key\r\n");
        return;
    }

    const std::string& key = tokens[0];

    auto it = list_store.find(key);
    if (it == list_store.end()) {
        sendResponse(":0\r\n");
        return;
    }

    auto& list = list_store[key];
    sendResponse(":" + std::to_string(list.size()) + "\r\n");
}

void Handler::handleLpopCommand(const std::vector<std::string>&tokens){
    if(tokens.size()<1){
        sendResponse("-ERR LPOP requires a key\r\n");
        return;
    }

    const std::string& key = tokens[0];

    auto it = list_store.find(key);
    if (it == list_store.end()) {
        sendResponse("$-1\r\n");
        return;
    }

    auto& list = list_store[key];
    if(tokens.size()==1){
        std::string value = list[0];
        list.erase(list.begin(),list.begin()+1);
        sendResponse("$" + std::to_string(value.size()) + "\r\n" + value + "\r\n");
    }else{
        int size=stoi(tokens[1]);
        if(size>list.size()) size=list.size();
        std::vector<std::string> values(list.begin(), list.begin() + size);
        list.erase(list.begin(),list.begin()+size);

        std::string response = "*" + std::to_string(size) + "\r\n";
        for (int i=0;i<size;i++) {
            response += "$" + std::to_string(values[i].size()) + "\r\n" + values[i] + "\r\n";
        }
        sendResponse(response);
    }
}

void Handler::handleBlpopCommand(const std::vector<std::string>&tokens){
    if (tokens.size() < 2) {
        sendResponse("-ERR BLPOP requires a key and timeout\r\n");
        return;
    }

    const std::string& key = tokens[0];
    double timeout=std::stod(tokens[1]);

    std::unique_lock<std::mutex> lock(store_mutex);
    auto list_has_data = [&]() {
        return !list_store[key].empty();
    };

    if(!list_has_data()){
        if(timeout==0){
            cv.wait(lock, list_has_data);
        }else{
            if(!cv.wait_for(lock, std::chrono::duration<double>(timeout), list_has_data)) {
                sendResponse("*-1\r\n");
                return;
            }
        }
    }

    auto it = list_store.find(key);
    if(it == list_store.end() || it->second.empty()) {
        sendResponse("*-1\r\n");
        return;
    }

    std::string value = it->second.front();
    it->second.erase(it->second.begin());

    std::string response = "*2\r\n";
    response += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
    response += "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
    sendResponse(response);
}

void Handler::handleXaddCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3 || tokens.size() % 2 != 0) {
        sendResponse("-ERR XADD requires a key, ID, and field-value pairs\r\n");
        return;
    }

    const std::string& key = tokens[0];
    const std::string& id = tokens[1];
    std::string timePart;
    std::string seqPart; 

    if(id=="*"){
        timePart = "*";
        seqPart = "*";
    }else{
        size_t dash = id.find('-');
        if (dash == std::string::npos) {
            sendResponse("-ERR Invalid ID format\r\n");
            return;
        }
        timePart = id.substr(0, dash);
        seqPart = id.substr(dash + 1);
    }

    int64_t ms;
    int64_t seq;

    auto& stream = stream_store[key];
    int64_t current_ms = getCurrentTimeMs();

    if(timePart == "*"){
        ms = current_ms;
    }else{
        try {
            ms = std::stoll(timePart);
        } catch (...) {
            sendResponse("-ERR Invalid time part\r\n");
            return;
        }
    }

    if(seqPart == "*"){
        seq = 0;
        if (!stream.empty()) {
            const std::string& last_id_str = stream.back().first;
            size_t last_dash = last_id_str.find('-');
            int64_t last_ms = std::stoll(last_id_str.substr(0, last_dash));
            int64_t last_seq = std::stoll(last_id_str.substr(last_dash + 1));

            if (last_ms == ms) {
                seq = last_seq + 1;
            }
        }
        if (ms == 0) {
            seq = 1;
        }
    }else{
        try {
            seq = std::stoll(seqPart);
        } catch (...) {
            sendResponse("-ERR Invalid sequence part\r\n");
            return;
        }
    }
    
    if (ms == 0 && seq == 0) {
        sendResponse("-ERR The ID specified in XADD must be greater than 0-0\r\n");
        return;
    }

    if(!stream.empty()){
        const std::string& last_id_str = stream.back().first;
        size_t last_dash = last_id_str.find('-');
        int64_t last_ms = std::stoll(last_id_str.substr(0, last_dash));
        int64_t last_seq = std::stoll(last_id_str.substr(last_dash + 1));

        if (ms < last_ms || (ms == last_ms && seq <= last_seq)) {
            sendResponse("-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n");
            return;
        }
    }

    std::unordered_map<std::string, std::string> fields;
    for (size_t i = 2; i < tokens.size(); i += 2) {
        fields[tokens[i]] = tokens[i + 1];
    }
    std::string final_id = std::to_string(ms) + "-" + std::to_string(seq);
    stream.push_back({final_id, fields});
    sendResponse("$" + std::to_string(final_id.size()) + "\r\n" + final_id + "\r\n");
}
