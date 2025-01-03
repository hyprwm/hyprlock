#include <print>
#include <variant>
#include "shared.hpp"
#include "../src/helpers/NotJson.hpp"

int main() {
    const auto in  = R"({"type":"asdf","array":["a","b","c"]})";
    int        ret = 0;

    auto [result, error] = NNotJson::parse(in);
    EXPECT(error.status, NNotJson::SError::NOT_JSON_OK);

    EXPECT(result.values.size(), 2);
    EXPECT(std::holds_alternative<std::string>(result.values["type"]), true);
    EXPECT(std::get<std::string>(result.values["type"]), "asdf");

    EXPECT(std::holds_alternative<std::vector<std::string>>(result.values["array"]), true);
    const auto vec = std::get<std::vector<std::string>>(result.values["array"]);
    EXPECT(vec.size(), 3);
    EXPECT(vec[0], std::string{"a"});
    EXPECT(vec[1], std::string{"b"});
    EXPECT(vec[2], std::string{"c"});

    const auto serialized = NNotJson::serialize(result);

    std::print("serialized: {}\n", serialized);
    // order is not guaranteed
    EXPECT(serialized == in || serialized == R"({"array":["a","b","c"],"type":"asdf"})", true);

    const auto in2         = R"({"type":"auth_message","auth_message_type":"secret","auth_message":"Password: "})";
    auto [result2, error2] = NNotJson::parse(in2);

    EXPECT(error2.status, NNotJson::SError::NOT_JSON_OK);
    EXPECT(result2.values.size(), 3);
    EXPECT(std::holds_alternative<std::string>(result2.values["type"]), true);
    EXPECT(std::get<std::string>(result2.values["type"]), "auth_message");

    const auto in3         = R"({ "type:"asdf" })";
    auto [result3, error3] = NNotJson::parse(in3);

    EXPECT(error3.status, NNotJson::SError::NOT_JSON_ERROR);
    EXPECT(error3.message, "Unexpected character \"a\"");

    const auto in4         = R"({"type":"a\"s\"df"})";
    auto [result4, error4] = NNotJson::parse(in4);

    EXPECT(error4.status, NNotJson::SError::NOT_JSON_OK);
    EXPECT(result4.values.size(), 1);
    EXPECT(std::holds_alternative<std::string>(result4.values["type"]), true);
    EXPECT(std::get<std::string>(result4.values["type"]), "a\"s\"df");

    const auto serialized4 = NNotJson::serialize(result4);
    EXPECT(serialized4, in4);

    const auto in5         = R"({" *~@#$%^&*()_+=><?/\a":" *~@#$%^&*()_+=><?/\a"})";
    auto [result5, error5] = NNotJson::parse(in5);

    EXPECT(error5.status, NNotJson::SError::NOT_JSON_OK);
    EXPECT(result5.values.size(), 1);
    EXPECT(std::holds_alternative<std::string>(result5.values[" *~@#$%^&*()_+=><?/\\a"]), true);
    EXPECT(std::get<std::string>(result5.values[" *~@#$%^&*()_+=><?/\\a"]), " *~@#$%^&*()_+=><?/\\a");
    std::print("serialized5: {}\n", NNotJson::serialize(result5));

    return ret;
}
