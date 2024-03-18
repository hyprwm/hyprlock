#include "Password.hpp"
#include "hyprlock.hpp"
#include "../helpers/Log.hpp"

#include <unistd.h>
#include <pwd.h>
#include <security/pam_appl.h>
#if __has_include(<security/pam_misc.h>)
#include <security/pam_misc.h>
#endif

#include <cstring>
#include <thread>

//
int conv(int num_msg, const struct pam_message** msg, struct pam_response** resp, void* appdata_ptr) {
    const char*          pass      = static_cast<const char*>(appdata_ptr);
    struct pam_response* pam_reply = static_cast<struct pam_response*>(calloc(num_msg, sizeof(struct pam_response)));

    for (int i = 0; i < num_msg; ++i) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
            case PAM_PROMPT_ECHO_ON: pam_reply[i].resp = strdup(pass); break;
            case PAM_ERROR_MSG: Debug::log(ERR, "PAM: {}", msg[i]->msg); break;
            case PAM_TEXT_INFO: Debug::log(LOG, "PAM: {}", msg[i]->msg); break;
        }
    }
    *resp = pam_reply;
    return PAM_SUCCESS;
}

static void passwordCheckTimerCallback(std::shared_ptr<CTimer> self, void* data) {
    g_pHyprlock->onPasswordCheckTimer();
}

std::shared_ptr<CPassword::SVerificationResult> CPassword::verify(const std::string& pass) {

    std::shared_ptr<CPassword::SVerificationResult> result = std::make_shared<CPassword::SVerificationResult>(false);

    std::thread([this, result, pass]() {
        auto auth = [&](std::string auth) -> bool {
            const pam_conv localConv = {conv, (void*)pass.c_str()};
            pam_handle_t*  handle    = NULL;
            auto uidPassword = getpwuid(getuid());

            int            ret = pam_start(auth.c_str(), uidPassword->pw_name, &localConv, &handle);

            if (ret != PAM_SUCCESS) {
                result->success    = false;
                result->failReason = "pam_start failed";
                Debug::log(ERR, "auth: pam_start failed for {}", auth);
                return false;
            }

            ret = pam_authenticate(handle, 0);

            if (ret != PAM_SUCCESS) {
                result->success    = false;
                result->failReason = ret == PAM_AUTH_ERR ? "Authentication failed" : "pam_authenticate failed";
                Debug::log(ERR, "auth: {} for {}", result->failReason, auth);
                return false;
            }

            ret = pam_end(handle, ret);

            result->success    = true;
            result->failReason = "Successfully authenticated";
            Debug::log(LOG, "auth: authenticated for {}", auth);

            return true;
        };

        result->realized = auth("hyprlock") || auth("su") || true;
        g_pHyprlock->addTimer(std::chrono::milliseconds(1), passwordCheckTimerCallback, nullptr);
    }).detach();

    return result;
}
