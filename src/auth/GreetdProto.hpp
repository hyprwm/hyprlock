#include <cstdint>
#include <string>
#include <vector>
#include <variant>
#include <glaze/glaze.hpp>
#include <glaze/util/string_literal.hpp>

// GREETD PROTOCOL
enum eGreetdResponse : uint8_t {
    GREETD_RESPONSE_UNKNOWN = 0xff,
    GREETD_RESPONSE_SUCCESS = 0,
    GREETD_RESPONSE_ERROR   = 1,
    GREETD_RESPONSE_AUTH    = 2,
};

enum eGreetdErrorMessageType : uint8_t {
    GREETD_OK         = 0,
    GREETD_ERROR_AUTH = 1,
    GREETD_ERROR      = 2,
};

enum eGreetdAuthMessageType : uint8_t {
    GREETD_INITIAL      = 0,
    GREETD_AUTH_VISIBLE = 1,
    GREETD_AUTH_SECRET  = 2,
    GREETD_AUTH_INFO    = 3,
    GREETD_AUTH_ERROR   = 4,
};

// REQUEST TYPES
struct SGreetdCreateSession {
    std::string type     = "create_session";
    std::string username = "";
};

struct SGreetdPostAuthMessageResponse {
    std::string type     = "post_auth_message_response";
    std::string response = "";
};

struct SGreetdStartSession {
    std::string              type = "start_session";
    std::vector<std::string> cmd;
    std::vector<std::string> env;
};

struct SGreetdCancelSession {
    std::string type = "cancel_session";
};

// RESPONSE TYPES
struct SGreetdErrorResponse {
    eGreetdErrorMessageType error_type;
    std::string              description;
};

struct SGreetdAuthMessageResponse {
    eGreetdAuthMessageType auth_message_type;
    std::string            auth_message;
};

struct SGreetdSuccessResponse {
    char DUMMY; // Without any field in SGreetdSuccessResponse, I get unknown_key for "type".
};

// RESPONSE and REQUEST VARIANTS
using VGreetdRequest  = std::variant<SGreetdCreateSession, SGreetdPostAuthMessageResponse, SGreetdStartSession, SGreetdCancelSession>;
using VGreetdResponse = std::variant<SGreetdSuccessResponse, SGreetdErrorResponse, SGreetdAuthMessageResponse>;

template <>
struct glz::meta<eGreetdResponse> {
    static constexpr auto value = enumerate("success", GREETD_RESPONSE_SUCCESS, "error", GREETD_RESPONSE_ERROR, "auth_message", GREETD_RESPONSE_AUTH);
};

template <>
struct glz::meta<eGreetdAuthMessageType> {
    static constexpr auto value = enumerate("visible", GREETD_AUTH_VISIBLE, "secret", GREETD_AUTH_SECRET, "info", GREETD_AUTH_INFO, "error", GREETD_AUTH_ERROR);
};

template <>
struct glz::meta<eGreetdErrorMessageType> {
    static constexpr auto value = enumerate("auth_error", GREETD_ERROR_AUTH, "error", GREETD_ERROR);
};

template <>
struct glz::meta<VGreetdResponse> {
    static constexpr std::string_view tag = "type";
    static constexpr std::array       ids{"success", "error", "auth_message"};
};
