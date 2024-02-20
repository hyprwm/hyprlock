#include "Timer.hpp"

CTimer::CTimer(std::chrono::system_clock::duration timeout, std::function<void(std::shared_ptr<CTimer> self, void* data)> cb_, void* data_) : cb(cb_), data(data_) {
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

void CTimer::call(std::shared_ptr<CTimer> self) {
    cb(self, data);
}

float CTimer::leftMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(expires - std::chrono::system_clock::now()).count();
}