#include "Password.hpp"
#include "../helpers/Log.hpp"
#include "hyprlock.hpp"

#include <security/_pam_types.h>
#include <security/pam_appl.h>
#include <sys/socket.h>
#include <unistd.h>
#if __has_include(<security/pam_misc.h>)
#include <security/pam_misc.h>
#endif

#include <cstring>
#include <expected>
#include <thread>

class AuthClient {
public:
  int sock;
  AuthClient(int sockfd) { sock = sockfd; };
  std::optional<std::string> question(const struct pam_message *msg) {
    char data[10240];

    // TODO: maybe use protobufs or extra class?
    int mlen = strlen(msg->msg);
    std::vector<char> packet(strlen(msg->msg) + sizeof(int));
    const char *style =
        static_cast<const char *>(static_cast<const void *>(&(msg->msg_style)));
    copy(style, style + sizeof(int), back_inserter(packet));
    copy(msg->msg, msg->msg + mlen, back_inserter(packet));

    if (send(this->sock, packet.data(), mlen + sizeof(int), 0) < 0) {
      Debug::log(ERR, "auth: failed at sending question due to: \n{}\n",
                 std::strerror(errno));
      return {};
    }
    if (recv(this->sock, data, 10240, 0) < 0) {
      Debug::log(ERR, "auth: failed at retrieving response due to: \n{}\n",
                 std::strerror(errno));
      return {};
    }
    // TODO: should I clear data? overwrite with zeros

    // the response ist always just a string
    return std::string(data);
  }
};

int conv(int num_msg, const struct pam_message **msgs,
         struct pam_response **response, void *appdata_ptr) {
  int count = 0;
  std::vector<struct pam_response *> replies(num_msg);

  const auto authcl = (AuthClient *)appdata_ptr;

  if (num_msg <= 0)
    return PAM_CONV_ERR;

  for (count = 0; count < num_msg; ++count) {
    auto reply = authcl->question(msgs[count]);
    if (reply.has_value()) {
      struct pam_response r = {reply.value().data(), 0};
      replies[count] = &r;
    } else {
      for (struct pam_response *r : replies) {
        // TODO: safe erasure?

        /*
         * NOTE:pam_conv(3) - It is the caller's responsibility to release both,
         * this array and the responses themselves, using free(3). Note,
         * *resp is a struct pam_response array and not an array of pointers.
         */
        free(r->resp);
        free(r);
      }
      return PAM_CONV_ERR;
    }
  }
  *response = *replies.data();
  return PAM_SUCCESS;
}

static void passwordCheckTimerCallback(std::shared_ptr<CTimer> self,
                                       void *data) {
  g_pHyprlock->onPasswordCheckTimer();
}

std::shared_ptr<CPassword::SVerificationResult>
CPassword::verify(const std::string &pass) {

  std::shared_ptr<CPassword::SVerificationResult> result =
      std::make_shared<CPassword::SVerificationResult>(false);

  std::thread([result, pass]() {
    auto auth = [&](std::string auth) -> bool {
      const pam_conv localConv = {conv, (void *)pass.c_str()};
      pam_handle_t *handle = NULL;

      int ret = pam_start(auth.c_str(), getlogin(), &localConv, &handle);

      if (ret != PAM_SUCCESS) {
        result->success = false;
        result->failReason = "pam_start failed";
        Debug::log(ERR, "auth: pam_start failed for {}", auth);
        return false;
      }

      ret = pam_authenticate(handle, 0);

      if (ret != PAM_SUCCESS) {
        result->success = false;
        result->failReason = ret == PAM_AUTH_ERR ? "Authentication failed"
                                                 : "pam_authenticate failed";
        Debug::log(ERR, "auth: {} for {}", result->failReason, auth);
        return false;
      }

      ret = pam_end(handle, ret);

      result->success = true;
      result->failReason = "Successfully authenticated";
      Debug::log(LOG, "auth: authenticated for {}", auth);

      return true;
    };

    result->realized = auth("hyprlock") || auth("su") || true;
    g_pHyprlock->addTimer(std::chrono::milliseconds(1),
                          passwordCheckTimerCallback, nullptr);
  }).detach();

  return result;
}
