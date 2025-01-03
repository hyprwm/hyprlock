#pragma once

/*
  This is a "json parser" intended to parse/serialize for the greetd protocol.
  It makes the following assumptions:
  - Data only contains strings or vectors of strings
  - Data is not nested

  For example:
  {
    "key1": "value1",
    "key2": ["value2", "value3"]
  }
*/

#include <cstdio>
#include <format>
#include <hyprutils/string/String.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>
#include <variant>

namespace NNotJson {
    using VJsonValue = std::variant<std::string, std::vector<std::string>>;

    struct SObject {
        std::unordered_map<std::string, VJsonValue> values;
    };

    struct SError {
        enum eStatus : u_int8_t {
            NOT_JSON_OK,
            NOT_JSON_ERROR,
        } status = NOT_JSON_OK;

        std::string message = "";
    };

    inline std::pair<SObject, SError> parse(const std::string& data) {
        static constexpr const std::string sinkChars = " \t\n\r,:{}";

        SObject                            result{};

        std::string                        key{};
        std::vector<std::string>           array;
        bool                               parsingArray = false;
        for (size_t i = 0; i < data.size(); i++) {
            if (sinkChars.find(data[i]) != std::string::npos)
                continue;

            switch (data[i]) {
                case '"': {
                    // find the next quote that is not escaped
                    size_t end = i + 1;
                    for (; end < data.size(); end++) {
                        if (data[end] == '\\') {
                            end++;
                            continue;
                        }
                        if (data[end] == '"')
                            break;
                    }

                    if (end == data.size())
                        return {result,
                                {
                                    .status  = SError::NOT_JSON_ERROR,
                                    .message = "Expected closing quote, but reached end of input",
                                }};

                    std::string val{data.data() + i + 1, end - (i + 1)};
                    Hyprutils::String::replaceInString(val, "\\\"", "\"");
                    if (key.empty())
                        key = val;
                    else if (parsingArray)
                        array.emplace_back(val);
                    else {
                        result.values.emplace(key, val);
                        key.clear();
                    }

                    i = end;
                } break;
                case '[': {
                    parsingArray = true;
                    if (key.empty())
                        return {result,
                                {
                                    .status  = SError::NOT_JSON_ERROR,
                                    .message = "Expected key before array",
                                }};
                } break;
                case ']': {
                    result.values.emplace(std::string{key}, array);
                    key          = std::string_view{};
                    parsingArray = false;
                    array.clear();
                } break;
                case '\0':
                    return {result,
                            {
                                .status  = SError::NOT_JSON_ERROR,
                                .message = "Encountered null byte ???",
                            }};
                default:
                    return {result,
                            {
                                .status  = SError::NOT_JSON_ERROR,
                                .message = std::format("Unexpected character \"{}\"", data[i]),
                            }};
            }
        };

        return {result, {}};
    }

    inline std::string serializeString(const std::string& in) {
        std::string escaped = in;
        Hyprutils::String::replaceInString(escaped, "\"", "\\\"");
        return std::format("\"{}\"", escaped);
    }

    inline std::string serializeArray(const std::vector<std::string>& in) {
        std::stringstream result;
        result << "[";
        for (const auto& item : in) {
            result << serializeString(item) << ",";
        }
        result.seekp(-1, std::ios_base::end);
        result << "]";
        return result.str();
    }

    inline std::string serialize(const SObject& obj) {
        std::stringstream result;
        result << "{";

        for (const auto& [key, value] : obj.values) {
            result << std::format("\"{}\":", key);
            if (std::holds_alternative<std::string>(value))
                result << serializeString(std::get<std::string>(value)) << ",";
            else
                result << serializeArray(std::get<std::vector<std::string>>(value)) << ",";
        }

        result.seekp(-1, std::ios_base::end);
        result << "}";

        return result.str();
    }
}
