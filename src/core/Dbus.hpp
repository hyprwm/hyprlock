#include <sdbus-c++/sdbus-c++.h>
#include <memory>

#include "../defines.hpp"

class CDbus {
  public:
    CDbus(bool enable);
    ~CDbus() = default;

    // nullptr if enable is false
    std::shared_ptr<sdbus::IConnection> m_connection        = nullptr;
    std::unique_ptr<sdbus::IProxy>      m_freedesktopLogin1 = nullptr;
};

inline UP<CDbus> g_dbus;
