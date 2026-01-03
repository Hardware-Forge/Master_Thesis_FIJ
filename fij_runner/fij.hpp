#pragma once

#include <chrono>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/fij.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// Helpers for working with fij_params char arrays
// -----------------------------------------------------------------------------

std::string cstr_from_fixed(const char *buf, std::size_t n);

template <std::size_t N>
inline std::string cstr_from_array(const char (&buf)[N]) {
    return cstr_from_fixed(buf, N);
}

template <std::size_t N>
inline void set_cstring(char (&field)[N], const std::string &value) {
    std::size_t max_len = N;
    std::size_t len = value.size();
    if (len >= max_len) {
        len = max_len - 1;
    }
    std::memcpy(field, value.data(), len);
    field[len] = '\0';
    if (len + 1 < max_len) {
        std::memset(field + len + 1, 0, max_len - len - 1);
    }
}

void set_process_name_from_path(struct fij_params &p);

// -----------------------------------------------------------------------------
// Reg name mapping & param defaults
// -----------------------------------------------------------------------------

int  reg_name_to_id(const std::string &name);
void fij_params_apply_defaults(struct fij_params &p);

// -----------------------------------------------------------------------------
// Basic utilities
// -----------------------------------------------------------------------------

int bool_int(const json &v);

void apply_field_if_present(
    struct fij_params &p,
    const json &cfg,
    const char *key,
    int fij_params::*field,
    bool boolean = false
);

// -----------------------------------------------------------------------------
// High-level types
// -----------------------------------------------------------------------------

struct FijJob {
    std::string path;      // executable path
    std::string args;      // argument string
    int runs;              // number of runs
    int baseline_runs;
    struct fij_params params;
    int workers;
};

struct CampaignResult {
    int   baseline_runs;
    int   baseline_success;
    double baseline_min_ms;
    int   max_delay_ms;
    int   injection_requested;
    int   injection_success;
    double avg_ms;
    double std_ms;
    std::vector<double> inj_times_ms;
};

// -----------------------------------------------------------------------------
// Path/logging helpers
// -----------------------------------------------------------------------------

fs::path create_dir_in_path(const fs::path &base_path, const std::string &final_folder);

void log_injection_iteration(
    const fs::path &base_path,
    int i,
    double dt_seconds,
    const struct fij_result &res
);

std::string label_from_params(const struct fij_params &p);

// -----------------------------------------------------------------------------
// Config / job building
// -----------------------------------------------------------------------------

std::vector<FijJob> build_fij_jobs_from_config(const json &config);

std::vector<FijJob> load_fij_jobs_from_file(const std::string &config_path);

// -----------------------------------------------------------------------------
// Campaign runner
// -----------------------------------------------------------------------------

CampaignResult run_injection_campaign(
    const std::string &device,
    struct fij_params base_params,
    int runs,
    int baseline_runs,
    int pre_delay_ms,
    int max_retries,
    int retry_delay_ms,
    bool verbose      = true,
    int max_workers   = 1
);

void run_campaigns_from_config(
    const std::string &config_path,
    const std::string &device      = "/dev/fij",
    int pre_delay_ms               = 50,
    int max_retries                = 5,
    int retry_delay_ms             = 50,
    bool verbose                   = true
);

// --------------------------------------------------------------------------
// Campaign Analyzer
// --------------------------------------------------------------------------

void analyze_injection_campaign(fs::path base_path_str);

