#pragma once

#include "../defines.hpp"
#include "CursorShape.hpp"
#include "wayland.hpp"
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

class CSeatManager {
  public:
    CSeatManager() = default;
    ~CSeatManager();

    void               registerSeat(SP<CCWlSeat> seat);
    void               registerCursorShape(SP<CCWpCursorShapeManagerV1> shape);
    bool               registered();

    SP<CCWlKeyboard>   m_pKeeb;
    SP<CCWlPointer>    m_pPointer;

    UP<CCursorShape>   m_pCursorShape;

    xkb_context*       m_pXKBContext      = nullptr;
    xkb_keymap*        m_pXKBKeymap       = nullptr;
    xkb_state*         m_pXKBState        = nullptr;
    xkb_compose_state* m_pXKBComposeState = nullptr;

  private:
    SP<CCWlSeat> m_pSeat;
};

inline UP<CSeatManager> g_pSeatManager = makeUnique<CSeatManager>();
