#include "fij.hpp"

#include <iostream>

void run_campaigns_from_config(
    const std::string &config_path,
    const std::string &device,
    int pre_delay_ms,
    int max_retries,
    int retry_delay_ms,
    bool verbose
) {
    auto jobs = load_fij_jobs_from_file(config_path);

    if (verbose) {
        std::cout << "[+] Loaded " << jobs.size() << " jobs from " << config_path << "\n";
    }

    for (std::size_t idx = 0; idx < jobs.size(); ++idx) {
        const auto &job = jobs[idx];
        if (verbose) {
            std::cout << "\n[+] Running job " << (idx + 1) << "/" << jobs.size() << ":\n"
                      << "\n  baseline_runs = " << job.baseline_runs << "\n"
                      << "    path   = " << job.path << "\n"
                      << "    args   = " << job.args << "\n"
                      << "    runs   = " << job.runs << "\n";
        }

        run_injection_campaign(
            device,
            job.params,
            job.runs,
            job.baseline_runs,
            pre_delay_ms,
            max_retries,
            retry_delay_ms,
            verbose,
            job.workers
        );
    }
}
