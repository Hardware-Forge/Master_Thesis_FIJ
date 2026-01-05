#include "fij.hpp"
#include "fij_ioctls.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <omp.h>

#include <filesystem>

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// run_injection_campaign – single campaign
// -----------------------------------------------------------------------------

CampaignResult run_injection_campaign(
    const std::string &device,
    struct fij_params base_params,
    int runs,
    int baseline_runs,
    int pre_delay_ms,
    int max_retries,
    int retry_delay_ms,
    bool verbose,
    int max_workers
) {
    (void)max_workers; // currently unused, sequential execution

    if (!fs::exists(device)) {
        throw std::system_error(ENOENT, std::generic_category(),
                                "Device " + device + " does not exist");
    }

    if (runs <= 0) {
        throw std::invalid_argument("runs must be > 0");
    }
    if (baseline_runs <= 0) {
        throw std::invalid_argument("baseline_runs must be > 0");
    }
    // here the baseline runs are manually fixed to minimum 3. the first 2 runs are not considered since they are warmup runs
    if (baseline_runs <= 2) {
        baseline_runs = 3;
    }

    std::string label = label_from_params(base_params);

    std::string raw_path = cstr_from_array(base_params.process_path);
    if (!raw_path.empty() && !fs::exists(raw_path)) {
        throw std::system_error(ENOENT, std::generic_category(),
                                "Target path " + raw_path + " does not exist");
    }

    if (verbose) {
        std::cout << "=== Campaign start for: " << label << "\n";
        std::cout << "  runs=" << runs << "\n";
        std::cout << "  device=" << device << "\n\n";
    }

    auto cstr = [](const auto &arr) {
        return cstr_from_array(arr);
    };

    std::regex slug_re("[^A-Za-z0-9._-]+");
    auto slug = [&](const std::string &s) {
        std::string trimmed = s;
        std::string out = std::regex_replace(trimmed, slug_re, "_");
        while (!out.empty() && out.front() == '_') out.erase(out.begin());
        while (!out.empty() && out.back() == '_') out.pop_back();
        for (auto &c : out) c = std::tolower(c);
        return out;
    };

    std::string filename = fs::path(cstr(base_params.process_path)).filename().string();
    std::string args_str = cstr(base_params.process_args);

    std::string logs_folder;
    {
        std::vector<std::string> parts;
        parts.push_back(slug(filename));
        if (!args_str.empty()) {
            parts.push_back("+");
            parts.push_back(slug(args_str));
        }
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) logs_folder += "_";
            logs_folder += parts[i];
        }

        // Fix long filenames
        if (logs_folder.length() > 100) {
            std::hash<std::string> hasher;
            std::size_t h = hasher(logs_folder);
            std::stringstream ss;
            ss << std::hex << h;
            // truncate original string but keep hash for uniqueness
            logs_folder = logs_folder.substr(0, 100) + "_" + ss.str();
        }
    }

    fs::path campaign_path = create_dir_in_path("../fij_logs", logs_folder);

    std::string args_template = cstr(base_params.process_args);

    // ---------------- Phase 1: baseline ----------------

    fs::path no_inj_path = campaign_path / "no_inj";
    fs::create_directories(no_inj_path);

    if (verbose) {
        std::cout << "Phase 1: running " << baseline_runs
                  << " baseline IOCTL calls (no_injection=1)\n";
    }

    std::vector<double> baseline_times;
    std::vector<struct fij_result> baseline_results;
    baseline_times.reserve(baseline_runs);
    baseline_results.reserve(baseline_runs);

    // decide how many threads to use
    int num_threads = max_workers;
    if (num_threads <= 0) {
        // fallback to OpenMP default if max_workers is not set/negative
        num_threads = std::max(1, omp_get_max_threads());
    }

    #pragma omp parallel for num_threads(num_threads) schedule(dynamic)
    for (int i = 0; i < baseline_runs; ++i) {
        try {
            // Per-run directory: ../fij_logs/<campaign>/no_inj/injection_i
            fs::path run_dir = no_inj_path / ("injection_" + std::to_string(i));
            fs::create_directories(run_dir);

            // Expand arguments with {campaign} and {run} placeholders
            std::string expanded_args = args_template;
            {
                std::string campaign_placeholder = "{campaign}";
                std::string run_placeholder      = "{run}";
                std::string campaign_str         = no_inj_path.string();
                std::string run_str              = std::to_string(i);

                std::size_t pos = 0;
                while ((pos = expanded_args.find(campaign_placeholder, pos)) != std::string::npos) {
                    expanded_args.replace(pos, campaign_placeholder.size(), campaign_str);
                    pos += campaign_str.size();
                }
                pos = 0;
                while ((pos = expanded_args.find(run_placeholder, pos)) != std::string::npos) {
                    expanded_args.replace(pos, run_placeholder.size(), run_str);
                    pos += run_str.size();
                }
            }

            // Per-run copy of params to avoid races
            struct fij_params per_run_params = base_params;
            set_cstring(per_run_params.process_args, expanded_args);

            fs::path run_log_path = run_dir / "log.txt";
            set_cstring(per_run_params.log_path, run_log_path.string());
            per_run_params.iteration_number = i;

            // max_delay_ms = 0 here: baseline, no injection window needed.
            auto [dt, res] = fij_detail::run_send_and_poll(
                device,
                per_run_params,
                i,              // iteration index / tag
                0,              // max_delay_ms (unused for baseline, no injection)
                1,          // no_injection = 1  so the kernel does not inject
                pre_delay_ms,
                max_retries,
                retry_delay_ms
            );

            // Collect results (protect vector push_back)
            #pragma omp critical(baseline_collect)
            {
                baseline_times.push_back(dt);
                baseline_results.push_back(res);
            }

            // Logging (I/O needs its own critical section to avoid garbling)
            #pragma omp critical(fij_io)
            {
                if (verbose /*&& ( (i + 1) % 20 == 0 || i == baseline_runs - 1 )*/) {
                    std::cout << "  Baseline run " << (i + 1) << "/" << baseline_runs
                              << ": " << (dt * 1000.0) << " ms\n";
                }
            }

        } catch (const std::system_error &e) {
            if (verbose) {
                #pragma omp critical(fij_io)
                {
                    std::cerr << "  Baseline run " << (i + 1)
                              << " failed: " << e.what() << "\n";
                }
            }
        }
    }

    if (baseline_times.empty()) {
        throw std::runtime_error("All baseline runs failed for target " + label +
                                 "; cannot determine max_delay_ms.");
    }
    
    double tot_time = 0;
    for (size_t i = 2; i < baseline_times.size(); ++i) {
        tot_time += baseline_times[i];
    }
    double avg_s  = tot_time / baseline_times.size();
    double avg_ms = avg_s * 1000.0;

    int max_delay_ms = static_cast<int>(std::round(avg_ms));
    if (max_delay_ms <= 0) max_delay_ms = 1;

    if (verbose) {
        std::cout << "\nBaseline summary:\n";
        std::cout << "  Successful baseline runs: " << baseline_times.size()
                  << "/" << baseline_runs << "\n";
        std::cout << "  Average baseline time: " << (max_delay_ms) << " ms\n";
    }

    // ---------------- Phase 2: injection ----------------

    auto campaign_start = std::chrono::steady_clock::now();

    if (verbose) {
        std::cout << "\nPhase 2: running " << runs
                  << " IOCTL calls with injection (no_injection=0, max_delay_ms="
                  << max_delay_ms << ")\n";
    }

    std::vector<double> inj_times(runs, -1.0);
    std::vector<struct fij_result> inj_results(runs);

    #pragma omp parallel for num_threads(num_threads) schedule(dynamic)
    for (int i = 0; i < runs; ++i) {

        bool successful_injection = false;

        while (!successful_injection) {

            try {
                fs::path run_dir = campaign_path / ("injection_" + std::to_string(i));
    
                fs::create_directories(run_dir);
    
                std::string expanded_args = args_template;
                {
                    std::string campaign_placeholder = "{campaign}";
                    std::string run_placeholder      = "{run}";
                    std::string campaign_str         = campaign_path.string();
                    std::string run_str              = std::to_string(i);
    
                    std::size_t pos = 0;
                    while ((pos = expanded_args.find(campaign_placeholder, pos)) != std::string::npos) {
                        expanded_args.replace(pos, campaign_placeholder.size(), campaign_str);
                        pos += campaign_str.size();
                    }
                    pos = 0;
                    while ((pos = expanded_args.find(run_placeholder, pos)) != std::string::npos) {
                        expanded_args.replace(pos, run_placeholder.size(), run_str);
                        pos += run_str.size();
                    }
                }
    
                struct fij_params per_run_params = base_params;  // per-iteration copy
                set_cstring(per_run_params.process_args, expanded_args);
    
                fs::path run_log_path = run_dir / "log.txt";
                set_cstring(per_run_params.log_path, run_log_path.string());
                per_run_params.iteration_number = i;
    
                auto [dt, res] = fij_detail::run_send_and_poll(
                    device,
                    per_run_params,
                    i,
                    max_delay_ms,
                    0,              // no_injection = 0, the function has to inject a fault
                    pre_delay_ms,
                    max_retries,
                    retry_delay_ms
                );

                if( res.fault_injected ) {

                    successful_injection = true;

                    inj_times[i]   = dt;
                    inj_results[i] = res;
        
                    if ( (i + 1) % 100 == 0 || i == runs - 1 ) {
                        std::cout << "dt=" << dt
                                << "s, target=" << res.target_tgid
                                << ", duration=" << res.injection_time_ns
                                << ", ec=" << res.exit_code
                                << " iteration number = " << res.iteration_number << "\n";
        
                        
        
                        if (verbose) {
                            std::cout << "  Injection run " << (i + 1) << "/" << runs
                                    << ": " << (dt * 1000.0) << " ms\n";
                        }
                    }
        
                    log_injection_iteration(campaign_path, i, dt, res);
                }
    
                
            } catch (const std::system_error &e) {
                if (verbose) {
                    #pragma omp critical(fij_io)
                    {
                        std::cerr << "  Injection run " << (i + 1)
                                << " failed: " << e.what() << "\n";
                    }
                }
            }
        }
    }

    std::vector<double> successful_times;
    for (double t : inj_times) {
        if (t >= 0.0) successful_times.push_back(t);
    }

    if (successful_times.empty()) {
        throw std::runtime_error("All injection runs failed for target " + label + ".");
    }

    double avg = 0.0;
    for (double t : successful_times) avg += t;
    avg /= successful_times.size();

    double var = 0.0;
    if (successful_times.size() > 1) {
        for (double t : successful_times) {
            double d = t - avg;
            var += d * d;
        }
        var /= (successful_times.size() - 1);
    }
    double stddev = std::sqrt(var);

    auto   campaign_end   = std::chrono::steady_clock::now();
    double campaign_total = std::chrono::duration<double>(campaign_end - campaign_start).count();

    if (verbose) {
        std::cout << "\nInjection summary:\n";
        std::cout << "  Successful runs: " << successful_times.size()
                  << "/" << runs << "\n";
        std::cout << "  Average: " << (avg * 1000.0) << " ms\n";
        std::cout << "  Std dev: " << (stddev * 1000.0) << " ms\n";
        std::cout << "  Campaign time: " << campaign_total << "\n";
        std::cout << "=== Campaign end ===\n\n";
    }

    CampaignResult cr;
    cr.baseline_runs       = baseline_runs;
    cr.baseline_success    = static_cast<int>(baseline_times.size());
    cr.max_delay_ms        = max_delay_ms;
    cr.injection_requested = runs;
    cr.injection_success   = static_cast<int>(successful_times.size());
    cr.avg_ms              = avg * 1000.0;
    cr.std_ms              = stddev * 1000.0;

    for (double t : successful_times) {
        cr.inj_times_ms.push_back(t * 1000.0);
    }

    analyze_injection_campaign(campaign_path, runs);

    return cr;
}
