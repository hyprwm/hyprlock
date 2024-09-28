#include "IWidget.hpp"
#include "../../helpers/Log.hpp"
#include "../../core/hyprlock.hpp"
#include "src/core/Auth.hpp"
#include <chrono>
#include <unistd.h>
#include <pwd.h>
#include <hyprutils/string/VarList.hpp>

using namespace Hyprutils::String;

#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 190100
#pragma comment(lib, "date-tz")
#include <date/tz.h>
namespace std {
    namespace chrono {
        using date::current_zone;
    }
}
#endif

Vector2D IWidget::posFromHVAlign(const Vector2D& viewport, const Vector2D& size, const Vector2D& offset, const std::string& halign, const std::string& valign, const double& ang) {

    // offset after rotation for alignment
    Vector2D rot;
    if (ang != 0)
        rot = (size - size.rotated(ang)) / 2.0;

    Vector2D pos = offset;
    if (halign == "center")
        pos.x += viewport.x / 2.0 - size.x / 2.0;
    else if (halign == "left")
        pos.x += 0 - rot.x;
    else if (halign == "right")
        pos.x += viewport.x - size.x + rot.x;
    else if (halign != "none")
        Debug::log(ERR, "IWidget: invalid halign {}", halign);

    if (valign == "center")
        pos.y += viewport.y / 2.0 - size.y / 2.0;
    else if (valign == "top")
        pos.y += viewport.y - size.y + rot.y;
    else if (valign == "bottom")
        pos.y += 0 - rot.y;
    else if (valign != "none")
        Debug::log(ERR, "IWidget: invalid valign {}", valign);

    return pos;
}

static void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

static void replaceAllAttempts(std::string& str) {

    const size_t      ATTEMPTS = g_pHyprlock->getPasswordFailedAttempts();
    const std::string STR      = std::to_string(ATTEMPTS);
    size_t            pos      = 0;

    while ((pos = str.find("$ATTEMPTS", pos)) != std::string::npos) {
        if (str.substr(pos, 10).ends_with('[') && str.substr(pos).contains(']')) {
            const std::string REPL = str.substr(pos + 10, str.find_first_of(']', pos) - 10 - pos);
            if (ATTEMPTS == 0) {
                str.replace(pos, 11 + REPL.length(), REPL);
                pos += REPL.length();
            } else {
                str.replace(pos, 11 + REPL.length(), STR);
                pos += STR.length();
            }
        } else {
            str.replace(pos, 9, STR);
            pos += STR.length();
        }
    }
}

static void replaceAllLayout(std::string& str) {

    const auto        LAYOUTIDX  = g_pHyprlock->m_uiActiveLayout;
    const auto        LAYOUTNAME = xkb_keymap_layout_get_name(g_pHyprlock->m_pXKBKeymap, LAYOUTIDX);
    const std::string STR        = LAYOUTNAME ? LAYOUTNAME : "error";
    size_t            pos        = 0;

    while ((pos = str.find("$LAYOUT", pos)) != std::string::npos) {
        if (str.substr(pos, 8).ends_with('[') && str.substr(pos).contains(']')) {
            const std::string REPL = str.substr(pos + 8, str.find_first_of(']', pos) - 8 - pos);
            const CVarList    LANGS(REPL);
            const std::string LANG = LANGS[LAYOUTIDX].empty() ? STR : LANGS[LAYOUTIDX] == "!" ? "" : LANGS[LAYOUTIDX];
            str.replace(pos, 9 + REPL.length(), LANG);
            pos += LANG.length();
        } else {
            str.replace(pos, 7, STR);
            pos += STR.length();
        }
    }
}

static std::string getTime() {
    const auto current_zone = std::chrono::current_zone();
    const auto HHMMSS       = std::chrono::hh_mm_ss{current_zone->to_local(std::chrono::system_clock::now()) -
                                              std::chrono::floor<std::chrono::days>(current_zone->to_local(std::chrono::system_clock::now()))};
    const auto HRS          = HHMMSS.hours().count();
    const auto MINS         = HHMMSS.minutes().count();
    return (HRS < 10 ? "0" : "") + std::to_string(HRS) + ":" + (MINS < 10 ? "0" : "") + std::to_string(MINS);
}

static std::string getTime12h() {
    const auto current_zone = std::chrono::current_zone();
    const auto HHMMSS       = std::chrono::hh_mm_ss{current_zone->to_local(std::chrono::system_clock::now()) -
                                              std::chrono::floor<std::chrono::days>(current_zone->to_local(std::chrono::system_clock::now()))};
    const auto HRS          = HHMMSS.hours().count();
    const auto MINS         = HHMMSS.minutes().count();
    return (HRS % 12 < 10 ? "0" : "") + std::to_string(HRS % 12) + ":" + (MINS < 10 ? "0" : "") + std::to_string(MINS) + (HRS < 12 ? " AM" : " PM");
}

IWidget::SFormatResult IWidget::formatString(std::string in) {

    auto  uidPassword = getpwuid(getuid());
    char* username    = uidPassword->pw_name;
    char* user_gecos  = uidPassword->pw_gecos;

    if (!username)
        Debug::log(ERR, "Error in formatString, username null. Errno: ", errno);

    if (!user_gecos)
        Debug::log(WARN, "Error in formatString, user_gecos null. Errno: ", errno);

    IWidget::SFormatResult result;
    replaceAll(in, "$DESC", std::string{user_gecos ? user_gecos : ""});
    replaceAll(in, "$USER", std::string{username ? username : ""});
    replaceAll(in, "<br/>", std::string{"\n"});

    if (in.contains("$TIME12")) {
        replaceAll(in, "$TIME12", getTime12h());
        result.updateEveryMs = result.updateEveryMs != 0 && result.updateEveryMs < 1000 ? result.updateEveryMs : 1000;
    }

    if (in.contains("$TIME")) {
        replaceAll(in, "$TIME", getTime());
        result.updateEveryMs = result.updateEveryMs != 0 && result.updateEveryMs < 1000 ? result.updateEveryMs : 1000;
    }

    if (in.contains("$FAIL")) {
        const auto FAIL = g_pAuth->getLastFailText();
        replaceAll(in, "$FAIL", FAIL.has_value() ? FAIL.value() : "");
        result.allowForceUpdate = true;
    }

    if (in.contains("$PROMPT")) {
        const auto PROMPT = g_pAuth->getLastPrompt();
        replaceAll(in, "$PROMPT", PROMPT.has_value() ? PROMPT.value() : "");
        result.allowForceUpdate = true;
    }

    if (in.contains("$ATTEMPTS")) {
        replaceAllAttempts(in);
        result.allowForceUpdate = true;
    }

    if (in.contains("$LAYOUT")) {
        replaceAllLayout(in);
        result.allowForceUpdate = true;
    }

    if (in.starts_with("cmd[") && in.contains("]")) {
        // this is a command
        CVarList vars(in.substr(4, in.find_first_of(']') - 4), 0, ',', true);

        for (const auto& v : vars) {
            if (v.starts_with("update:")) {
                try {
                    if (v.substr(7).contains(':')) {
                        auto str                = v.substr(v.substr(7).find_first_of(':') + 8);
                        result.allowForceUpdate = str == "true" || std::stoull(str) == 1;
                    }

                    result.updateEveryMs = std::stoull(v.substr(7));
                } catch (std::exception& e) { Debug::log(ERR, "Error parsing {} in cmd[]", v); }
            } else {
                Debug::log(ERR, "Unknown prop in string format {}", v);
            }
        }

        result.alwaysUpdate = true;
        in                  = in.substr(in.find_first_of(']') + 1);
        result.cmd          = true;
    }

    result.formatted = in;
    return result;
}
