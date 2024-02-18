#include "Password.hpp"

#include <unistd.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>

struct pam_response* reply;

//
int conv(int num_msg, const struct pam_message** msg, struct pam_response** resp, void* appdata_ptr) {
    *resp = reply;
    return PAM_SUCCESS;
}

CPassword::SVerificationResult CPassword::verify(const std::string& pass) {
    const pam_conv localConv = {conv, NULL};
    pam_handle_t*  handle    = NULL;

    int            ret = pam_start("su", getlogin(), &localConv, &handle);

    if (ret != PAM_SUCCESS)
        return {false, "pam_start failed"};

    reply = (struct pam_response*)malloc(sizeof(struct pam_response));

    reply->resp         = strdup(pass.c_str());
    reply->resp_retcode = 0;
    ret                 = pam_authenticate(handle, 0);

    if (ret != PAM_SUCCESS)
        return {false, ret == PAM_AUTH_ERR ? "Authentication failed" : "pam_authenticate failed"};

    ret = pam_end(handle, ret);

    return {true, "Successfully authenticated"};
}