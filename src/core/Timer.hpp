#pragma once

#include <chrono>
#include <functional>
#include "../defines.hpp"

class CTimer {
  public:
    CTimer(std::chrono::system_clock::duration timeout, std::function<void(ASP<CTimer> self, void* data)> cb_, void* data_, bool force);

    void  cancel();
    bool  passed();
    bool  canForceUpdate();

    float leftMs();

    bool  cancelled();
    void  call(ASP<CTimer> self);

  private:
    std::function<void(ASP<CTimer> self, void* data)> cb;
    void*                                             data = nullptr;
    std::chrono::system_clock::time_point             expires;
    bool                                              wasCancelled     = false;
    bool                                              allowForceUpdate = false;
};
