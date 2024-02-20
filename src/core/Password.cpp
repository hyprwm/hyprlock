#include "Password.hpp"
#include "hyprlock.hpp"

#include <unistd.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>

#include <thread>

struct pam_response* reply;

//
int conv(int num_msg, const struct pam_message** msg, struct pam_response** resp, void* appdata_ptr) {
    *resp = reply;
    return PAM_SUCCESS;
}

static void passwordCheckTimerCallback(std::shared_ptr<CTimer> self, void* data) {
    g_pHyprlock->onPasswordCheckTimer();
}

std::shared_ptr<CPassword::SVerificationResult> CPassword::verify(const std::string& pass) {

    std::shared_ptr<CPassword::SVerificationResult> result = std::make_shared<CPassword::SVerificationResult>(false);

    std::thread([this, result, pass]() {
        const pam_conv localConv = {conv, NULL};
        pam_handle_t*  handle    = NULL;

        int            ret = pam_start("su", getlogin(), &localConv, &handle);

        if (ret != PAM_SUCCESS) {
            result->success    = false;
            result->failReason = "pam_start failed";
            result->realized   = true;
            g_pHyprlock->addTimer(std::chrono::milliseconds(1), passwordCheckTimerCallback, nullptr);
            return;
        }

        reply = (struct pam_response*)malloc(sizeof(struct pam_response));

        reply->resp         = strdup(pass.c_str());
        reply->resp_retcode = 0;
        ret                 = pam_authenticate(handle, 0);

        if (ret != PAM_SUCCESS) {
            result->success    = false;
            result->failReason = ret == PAM_AUTH_ERR ? "Authentication failed" : "pam_authenticate failed";
            result->realized   = true;
            g_pHyprlock->addTimer(std::chrono::milliseconds(1), passwordCheckTimerCallback, nullptr);
            return;
        }

        ret = pam_end(handle, ret);

        result->success    = true;
        result->failReason = "Successfully authenticated";
        result->realized   = true;
        g_pHyprlock->addTimer(std::chrono::milliseconds(1), passwordCheckTimerCallback, nullptr);
    }).detach();

    return result;
}