#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include "Hyprpaper.hpp"
#include "Log.hpp"
#include "VarList.hpp"

std::unordered_map<std::string, std::string> hyprpaperRequest() {
    std::unordered_map<std::string, std::string> resp;
    resp.clear();

    const auto HISENV       = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const auto SERVERSOCKET = socket(AF_UNIX, SOCK_STREAM, 0);

    if (SERVERSOCKET < 0) {
        Debug::log(ERR, "IPC: Couldn't open a socket");
        return resp;
    }

    sockaddr_un serverAddress = {0};
    serverAddress.sun_family  = AF_UNIX;

    const std::string SOCKETPATH = HISENV ? "/tmp/hypr/" + std::string{HISENV} + "/.hyprpaper.sock" : "/tmp/hypr/.hyprpaper.sock";
    strncpy(serverAddress.sun_path, SOCKETPATH.c_str(), sizeof(serverAddress.sun_path) - 1);

    if (connect(SERVERSOCKET, (sockaddr*)&serverAddress, SUN_LEN(&serverAddress)) < 0) {
        Debug::log(ERR, "IPC: Couldn't connect to {}", SOCKETPATH);
        return resp;
    }

    auto sizeWritten = write(SERVERSOCKET, "listactive", 10);

    if (sizeWritten < 0) {
        Debug::log(ERR, "IPC: Couldn't write");
        return resp;
    }

    char buffer[8192] = {0};

    sizeWritten = read(SERVERSOCKET, buffer, 8192);

    if (sizeWritten < 0) {
        Debug::log(ERR, "IPC: Couldn't read");
        return resp;
    }

    close(SERVERSOCKET);

    for (const auto& pair : CVarList((std::string)buffer, 0, '\n')) {
        const CVarList    MONWP(pair, 2, '=', true);
        const std::string MON = MONWP[0].starts_with("desc:") ? MONWP[0].substr(5) : MONWP[0];
        if (MONWP.size() == 2)
            resp[MON] = MONWP[1];
    }

    return resp;
}

std::string hyprpaperGetResourceId(const std::unordered_map<std::string, std::string>& map, const std::string& port, const std::string& desc) {

    if (map.contains(port))
        return "hyprpaper:" + port + ":" + map.at(port);

    for (const auto& pair : map) {
        if (desc.starts_with(pair.first))
            return "hyprpaper:" + pair.first + ":" + pair.second;
    }

    return "";
}
