#include "Timer.hpp"

CTimer::CTimer(std::chrono::system_clock::duration timeout, std::function<void(ASP<CTimer> self, void* data)> cb_, void* data_, bool force) :
    cb(cb_), data(data_), allowForceUpdate(force) {
    expires = std::chrono::system_clock::now() + timeout;
}

bool CTimer::passed() {
    return std::chrono::system_clock::now() > expires;
}

void CTimer::cancel() {
    wasCancelled = true;
}

bool CTimer::cancelled() {
    return wasCancelled;
}

void CTimer::call(ASP<CTimer> self) {
    cb(self, data);
}

float CTimer::leftMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(expires - std::chrono::system_clock::now()).count();
}

bool CTimer::canForceUpdate() {
    return allowForceUpdate;
}
