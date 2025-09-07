#pragma once
#include <string_view>
#include <string>
#include <vector>

struct Command {
    std::string name;
    std::vector<std::string> args;
};

class Parser {
public:
    Command parse(std::string_view input);
private:
    std::pair<int, std::string_view> extractCount(std::string_view str);
    std::pair<std::string_view, std::string_view> decodeBulkString(std::string_view msg);
    std::vector<std::string_view> decodeArray(std::string_view msg);
};
