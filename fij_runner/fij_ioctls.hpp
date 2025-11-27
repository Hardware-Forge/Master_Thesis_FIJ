#pragma once

#include <string>
#include <utility>

#include "fij.hpp"

// Helper IOCTL wrappers used by run_injection_campaign.
// Kept in a small "detail" namespace so they don't leak into your public API.
namespace fij_detail {

std::pair<double, struct fij_result> run_with_retries(
    const std::string &device,
    const struct fij_params &base_params,
    bool no_injection,
    int max_delay_ms,
    int pre_delay_ms   = 50,
    int max_retries    = 5,
    int retry_delay_ms = 50
);

std::pair<double, struct fij_result> run_send_and_poll(
    const std::string &device,
    struct fij_params base_params,
    int iteration_index,
    int max_delay_ms,
    int no_injection,
    int pre_delay_ms     = 50,
    int max_retries      = 5,
    int retry_delay_ms   = 50,
    int poll_interval_ms = 5
);

} // namespace fij_detail
