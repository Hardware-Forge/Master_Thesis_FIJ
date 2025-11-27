#include "fij_ioctls.hpp"

#include <chrono>
#include <iostream>
#include <system_error>
#include <thread>

#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

// These helpers are private to this translation unit.
namespace {

void ioctl_checked(int fd, unsigned long request, void *arg) {
    if (::ioctl(fd, request, arg) == -1) {
        throw std::system_error(errno, std::generic_category(), "ioctl");
    }
}

// Simple blocking run, used by run_with_retries().
std::pair<double, struct fij_result> run_once(
    const std::string &device,
    const struct fij_params &base_params,
    bool no_injection,
    int max_delay_ms
) {
    struct fij_exec msg{};
    msg.params = base_params;

    msg.params.no_injection = no_injection ? 1 : 0;
    if (!no_injection) {
        msg.params.max_delay_ms = max_delay_ms;
    }

    auto start = std::chrono::steady_clock::now();
    int fd = ::open(device.c_str(), O_RDWR);
    if (fd == -1) {
        throw std::system_error(errno, std::generic_category(), "open");
    }

    try {
        ioctl_checked(fd, IOCTL_EXEC_AND_FAULT, &msg);
    } catch (...) {
        ::close(fd);
        throw;
    }

    ::close(fd);
    auto end = std::chrono::steady_clock::now();

    double dt = std::chrono::duration<double>(end - start).count();
    return {dt, msg.result};
}

} // namespace

// -----------------------------------------------------------------------------
// Public helpers used by run_injection_campaign
// -----------------------------------------------------------------------------

namespace fij_detail {

std::pair<double, struct fij_result> run_send_and_poll(
    const std::string &device,
    struct fij_params base_params,
    int iteration_index,
    int max_delay_ms,
    int no_injection,
    int pre_delay_ms,
    int max_retries,
    int retry_delay_ms,
    int poll_interval_ms
) {

    if (pre_delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pre_delay_ms));
    }

    base_params.no_injection = no_injection;
    base_params.max_delay_ms = max_delay_ms;

    auto start = std::chrono::steady_clock::now();
    int fd = ::open(device.c_str(), O_RDWR);
    if (fd == -1) {
        throw std::system_error(errno, std::generic_category(), "open");
    }

    try {
        int attempt = 0;
        while (true) {
            if (::ioctl(fd, IOCTL_SEND_MSG, &base_params) == -1) {
                if (errno == EBUSY && attempt < max_retries) {
                    ++attempt;
                    std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                    continue;
                }
                throw std::system_error(errno, std::generic_category(), "IOCTL_SEND_MSG");
            }
            break;
        }

        struct fij_result result{};
        int killed=0;
        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if(elapsed_ms >= (10*max_delay_ms) && !no_injection && !killed) {
                ioctl(fd, IOCTL_KILL_TARGET);
                std::cout << "Process is being killed";
                killed = 1;
            }

            if (::ioctl(fd, IOCTL_RECEIVE_MSG, &result) == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
                    continue;
                }
                if (errno == EBUSY && max_retries > 0) {
                    --max_retries;
                    std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                    continue;
                }
                throw std::system_error(errno, std::generic_category(), "IOCTL_RECEIVE_MSG");
            }
            break;
        }

        auto end = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(end - start).count();
        ::close(fd);
        return {dt, result};
    } catch (...) {
        ::close(fd);
        throw;
    }
}

std::pair<double, struct fij_result> run_with_retries(
    const std::string &device,
    const struct fij_params &base_params,
    bool no_injection,
    int max_delay_ms,
    int pre_delay_ms,
    int max_retries,
    int retry_delay_ms
) {
    if (pre_delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pre_delay_ms));
    }

    int attempt = 0;
    while (true) {
        try {
            return run_once(device, base_params, no_injection, max_delay_ms);
        } catch (const std::system_error &e) {
            if (e.code().value() == EBUSY && attempt < max_retries) {
                ++attempt;
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                continue;
            }
            throw;
        }
    }
}

} // namespace fij_detail
