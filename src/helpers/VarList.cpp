#include "VarList.hpp"
#include <ranges>
#include <algorithm>

static std::string removeBeginEndSpacesTabs(std::string str) {
    if (str.empty())
        return str;

    int countBefore = 0;
    while (str[countBefore] == ' ' || str[countBefore] == '\t') {
        countBefore++;
    }

    int countAfter = 0;
    while ((int)str.length() - countAfter - 1 >= 0 && (str[str.length() - countAfter - 1] == ' ' || str[str.length() - 1 - countAfter] == '\t')) {
        countAfter++;
    }

    str = str.substr(countBefore, str.length() - countBefore - countAfter);

    return str;
}

CVarList::CVarList(const std::string& in, const size_t lastArgNo, const char delim, const bool removeEmpty) {
    if (in.empty())
        m_vArgs.emplace_back("");

    std::string args{in};
    size_t      idx = 0;
    size_t      pos = 0;
    std::ranges::replace_if(
        args, [&](const char& c) { return delim == 's' ? std::isspace(c) : c == delim; }, 0);

    for (const auto& s : args | std::views::split(0)) {
        if (removeEmpty && s.empty())
            continue;
        if (++idx == lastArgNo) {
            m_vArgs.emplace_back(removeBeginEndSpacesTabs(in.substr(pos)));
            break;
        }
        pos += s.size() + 1;
        m_vArgs.emplace_back(removeBeginEndSpacesTabs(std::string_view{s}.data()));
    }
}

std::string CVarList::join(const std::string& joiner, size_t from, size_t to) const {
    size_t      last = to == 0 ? size() : to;

    std::string rolling;
    for (size_t i = from; i < last; ++i) {
        rolling += m_vArgs[i] + (i + 1 < last ? joiner : "");
    }

    return rolling;
}