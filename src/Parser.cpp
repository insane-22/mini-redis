#include "Parser.hpp"
#include <charconv>
#include <stdexcept>

std::pair<int, std::string_view> Parser::extractCount(std::string_view str) {
    int value = 0;
    auto res = std::from_chars(str.begin(), str.end(), value);
    if (res.ec == std::errc{}) {
        return { value, std::string_view(res.ptr + 2, str.end() - res.ptr - 2) };
    }
    throw std::runtime_error("Invalid count format");
}

std::pair<std::string_view, std::string_view> Parser::decodeBulkString(std::string_view msg) {
    auto [len, rem] = extractCount(msg.substr(1));
    if (rem.size() < len + 2 || rem[len] != '\r' || rem[len + 1] != '\n') {
        throw std::runtime_error("Invalid bulk string format");
    }
    return { rem.substr(0, len), rem.substr(len + 2) };
}

std::vector<std::string_view> Parser::decodeArray(std::string_view msg) {
    auto [count, rem] = extractCount(msg.substr(1));
    std::vector<std::string_view> elements;
    for (int i = 0; i < count; ++i) {
        if (rem.empty() || rem[0] != '$') {
            throw std::runtime_error("Expected bulk string");
        }
        auto [bulk, next] = decodeBulkString(rem);
        elements.push_back(bulk);
        rem = next;
    }
    return elements;
}

Command Parser::parse(std::string_view input) {
    if (input.empty() || input[0] != '*') {
        throw std::runtime_error("Invalid message format");
    }
    auto elements = decodeArray(input);
    if (elements.empty()) {
        throw std::runtime_error("Empty command array");
    }
    return { std::string(elements[0]), std::vector<std::string>(elements.begin() + 1, elements.end()) };
}
