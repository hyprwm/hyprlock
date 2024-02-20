#pragma once

#include <chrono>
#include <functional>

class CTimer {
  public:
    CTimer(std::chrono::system_clock::duration timeout, std::function<void(std::shared_ptr<CTimer> self, void* data)> cb_, void* data_);

    void  cancel();
    bool  passed();

    float leftMs();

    bool  cancelled();
    void  call(std::shared_ptr<CTimer> self);

  private:
    std::function<void(std::shared_ptr<CTimer> self, void* data)> cb;
    void*                                                         data = nullptr;
    std::chrono::system_clock::time_point                         expires;
    bool                                                          wasCancelled = false;
};