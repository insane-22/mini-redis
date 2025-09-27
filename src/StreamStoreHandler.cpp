#include "StreamStoreHandler.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <sys/socket.h>
#include <cctype>


static std::condition_variable global_cv;
std::unordered_map<std::string, std::vector<StreamStoreHandler::StreamEntry>> StreamStoreHandler::stream_store;
std::mutex StreamStoreHandler::store_mutex;
std::unordered_map<std::string, std::condition_variable>StreamStoreHandler::stream_cvs;

StreamStoreHandler::StreamStoreHandler(int client_fd) : client_fd(client_fd) {}

bool StreamStoreHandler::isStreamCommand(const std::string& cmd) {
    return cmd == "XADD" || cmd == "XRANGE" || cmd == "XREAD";
}

void StreamStoreHandler::handleCommand(const std::string& cmd, const std::vector<std::string>& args) {
    if (cmd == "XADD") handleXadd(args);
    else if (cmd == "XRANGE") handleXrange(args);
    else if (cmd == "XREAD") handleXread(args);
}

int64_t StreamStoreHandler::getCurrentTimeMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void StreamStoreHandler::handleXadd(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3 || tokens.size() % 2 != 0) {
        sendResponse("-ERR XADD requires a key, ID, and field-value pairs\r\n");
        return;
    }

    const std::string& key = tokens[0];
    const std::string& id = tokens[1];
    std::string timePart, seqPart;

    if(id == "*") {
        timePart = "*"; seqPart = "*";
    } else {
        size_t dash = id.find('-');
        if(dash == std::string::npos) { sendResponse("-ERR Invalid ID format\r\n"); return; }
        timePart = id.substr(0, dash);
        seqPart = id.substr(dash + 1);
    }

    int64_t ms, seq;
    std::string final_id;
    {
        std::lock_guard<std::mutex> lock(store_mutex);
        auto& stream = stream_store[key];
        int64_t current_ms = getCurrentTimeMs();

        if(timePart == "*") ms = current_ms; else ms = std::stoll(timePart);
        if(seqPart == "*") {
            seq = 0;
            if (!stream.empty()) {
                size_t last_dash = stream.back().first.find('-');
                int64_t last_ms = std::stoll(stream.back().first.substr(0, last_dash));
                int64_t last_seq = std::stoll(stream.back().first.substr(last_dash + 1));
                if (last_ms == ms) seq = last_seq + 1;
            }
            if (ms == 0) seq = 1;
        } else seq = std::stoll(seqPart);

        if (ms == 0 && seq == 0) { sendResponse("-ERR The ID must be greater than 0-0\r\n"); return; }

        if (!stream.empty()) {
            size_t last_dash = stream.back().first.find('-');
            int64_t last_ms = std::stoll(stream.back().first.substr(0, last_dash));
            int64_t last_seq = std::stoll(stream.back().first.substr(last_dash + 1));
            if (ms < last_ms || (ms == last_ms && seq <= last_seq)) {
                sendResponse("ERR The ID specified in XADD is equal or smaller than the target stream top item");
                return;
            }
        }

        std::unordered_map<std::string, std::string> fields;
        for (size_t i = 2; i < tokens.size(); i += 2)
            fields[tokens[i]] = tokens[i+1];

        final_id = std::to_string(ms) + "-" + std::to_string(seq);
        stream.push_back({final_id, fields});

        stream_cvs[key].notify_all();
        global_cv.notify_all();
    } 
    sendResponse("$" + std::to_string(final_id.size()) + "\r\n" + final_id + "\r\n");
}

void StreamStoreHandler::handleXrange(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) { sendResponse("-ERR XRANGE requires key, start, end\r\n"); return; }

    const std::string& key = tokens[0];
    std::string start_id = tokens[1], end_id = tokens[2];

    std::lock_guard<std::mutex> lock(store_mutex);
    auto it = stream_store.find(key);
    if (it == stream_store.end() || it->second.empty()) { sendResponse("*0\r\n"); return; }
    auto& stream = it->second;

    auto parse_id = [](const std::string& id, int64_t& ms, int64_t& seq, bool is_start) {
        size_t dash = id.find('-');
        if (dash == std::string::npos) { ms = std::stoll(id); seq = is_start ? 0 : INT64_MAX; }
        else { ms = std::stoll(id.substr(0, dash)); seq = std::stoll(id.substr(dash + 1)); }
    };

    int64_t start_ms, start_seq, end_ms, end_seq;
    if (start_id == "-") { start_ms = 0; start_seq = 0; } else parse_id(start_id, start_ms, start_seq, true);
    if (end_id == "+") { end_ms = INT64_MAX; end_seq = INT64_MAX; } else parse_id(end_id, end_ms, end_seq, false);

    std::vector<std::pair<std::string, std::unordered_map<std::string,std::string>>> result;
    for (auto& entry : stream) {
        size_t dash = entry.first.find('-');
        int64_t ms = std::stoll(entry.first.substr(0, dash));
        int64_t seq = std::stoll(entry.first.substr(dash + 1));
        if ((ms > start_ms || (ms == start_ms && seq >= start_seq)) &&
            (ms < end_ms || (ms == end_ms && seq <= end_seq)))
            result.push_back(entry);
    }

    std::string response = "*" + std::to_string(result.size()) + "\r\n";
    for (auto& entry : result) {
        response += "*2\r\n$" + std::to_string(entry.first.size()) + "\r\n" + entry.first + "\r\n";
        response += "*" + std::to_string(entry.second.size() * 2) + "\r\n";
        for (auto& kv : entry.second) {
            response += "$" + std::to_string(kv.first.size()) + "\r\n" + kv.first + "\r\n";
            response += "$" + std::to_string(kv.second.size()) + "\r\n" + kv.second + "\r\n";
        }
    }
    sendResponse(response);
}

void StreamStoreHandler::handleXread(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        sendResponse("-ERR XREAD syntax error\r\n");
        return;
    }

    int64_t block_ms = 0;
    size_t idx = 0;

    while(idx < tokens.size() && tokens[idx] != "streams") {
        if((tokens[idx] == "BLOCK" || tokens[idx] == "block") && idx + 1 < tokens.size()) {
            try {
                block_ms = std::stoll(tokens[idx + 1]);
            } catch (...) { sendResponse("-ERR invalid BLOCK value\r\n"); return; }
            idx += 2;
        } else {
            idx++;
        }
    }

    if(idx >= tokens.size() || tokens[idx] != "streams") {
        sendResponse("-ERR XREAD syntax error\r\n"); 
        return;
    }
    idx++; 

    if ((tokens.size() - idx) % 2 != 0) {
        sendResponse("-ERR XREAD syntax error\r\n");
        return;
    }

    size_t mid = (tokens.size() - idx) / 2 + idx;
    std::vector<std::string> keys(tokens.begin() + idx, tokens.begin() + mid);
    std::vector<std::string> ids(tokens.begin() + mid, tokens.end());

    if (keys.size() != ids.size()) {
        sendResponse("-ERR Number of keys and IDs must match\r\n");
        return;
    }

    std::unique_lock<std::mutex> lock(store_mutex);

    std::vector<int64_t> last_ms(keys.size(), 0), last_seq(keys.size(), -1);
    for (size_t i = 0; i < ids.size(); ++i) {
        const std::string &last_id = ids[i];
        size_t dash = last_id.find('-');
        if (dash != std::string::npos) {
            last_ms[i] = std::stoll(last_id.substr(0, dash));
            last_seq[i] = std::stoll(last_id.substr(dash + 1));
        } else {
            last_ms[i] = std::stoll(last_id);
            last_seq[i] = -1;
        }
    }

    auto collect = [&]() {
        std::vector<std::vector<std::pair<std::string,std::unordered_map<std::string,std::string>>>> out(keys.size());
        for (size_t i = 0; i < keys.size(); ++i) {
            const std::string &key = keys[i];
            auto it = stream_store.find(key);
            if (it == stream_store.end() || it->second.empty()) continue;
            auto &stream = it->second;
            for (auto &entry : stream) {
                size_t e_dash = entry.first.find('-');
                int64_t ms = std::stoll(entry.first.substr(0, e_dash));
                int64_t seq = std::stoll(entry.first.substr(e_dash + 1));
                if ((ms > last_ms[i]) || (ms == last_ms[i] && seq > last_seq[i])) {
                    out[i].push_back(entry);
                }
            }
        }
        return out;
    };

    auto all_results = collect();

    if (block_ms > 0) {
        bool awakened_by_data = global_cv.wait_for(lock, std::chrono::milliseconds(block_ms), [&]() -> bool {
            for (size_t i = 0; i < keys.size(); ++i) {
                auto it = stream_store.find(keys[i]);
                if (it == stream_store.end()) continue;
                auto &stream = it->second;
                if (stream.empty()) continue;
                size_t e_dash = stream.back().first.find('-');
                int64_t ms = std::stoll(stream.back().first.substr(0, e_dash));
                int64_t seq = std::stoll(stream.back().first.substr(e_dash + 1));
                if ((ms > last_ms[i]) || (ms == last_ms[i] && seq > last_seq[i])) return true;
            }
            return false;
        });

        all_results = collect();
    }

    std::vector<std::string> stream_parts;
    for (size_t i = 0; i < keys.size(); ++i) {
        auto &results = all_results[i];
        if (results.empty()) continue;

        std::string part;
        part += "*2\r\n"; // [key, entries]
        part += "$" + std::to_string(keys[i].size()) + "\r\n" + keys[i] + "\r\n";
        part += "*" + std::to_string(results.size()) + "\r\n";

        for (auto &entry : results) {
            part += "*2\r\n"; // [id, fields]
            part += "$" + std::to_string(entry.first.size()) + "\r\n" + entry.first + "\r\n";
            part += "*" + std::to_string(entry.second.size() * 2) + "\r\n";
            for (auto &kv : entry.second) {
                part += "$" + std::to_string(kv.first.size()) + "\r\n" + kv.first + "\r\n";
                part += "$" + std::to_string(kv.second.size()) + "\r\n" + kv.second + "\r\n";
            }
        }
        stream_parts.push_back(part);
    }

    std::string response;
    if (stream_parts.empty()) {
        response = "*-1\r\n"; 
    } else {
        response = "*" + std::to_string(stream_parts.size()) + "\r\n";
        for (auto &p : stream_parts) response += p;
    }

    lock.unlock(); 
    sendResponse(response);
}

void StreamStoreHandler::sendResponse(const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}