#pragma once

#include <memory>
#include <string>
#include <atomic>

class CPassword {
  public:
    struct SVerificationResult {
        std::atomic<bool> realized   = false;
        bool              success    = false;
        std::string       failReason = "";
    };

    std::shared_ptr<SVerificationResult> verify(const std::string& pass);
};

inline std::unique_ptr<CPassword> g_pPassword = std::make_unique<CPassword>();