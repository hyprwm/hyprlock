#pragma once

#include <memory>
#include <string>

class CPassword {
  public:
    struct SVerificationResult {
        bool        success    = false;
        std::string failReason = "";
    };

    SVerificationResult verify(const std::string& pass);
};

inline std::unique_ptr<CPassword> g_pPassword = std::make_unique<CPassword>();