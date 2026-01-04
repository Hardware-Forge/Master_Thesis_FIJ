#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <iomanip>
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <omp.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ==========================================
// UTILITY: Binary File Comparison
// ==========================================
bool are_files_identical_binary(const fs::path& p1, const fs::path& p2) {
    std::ifstream f1(p1, std::ifstream::binary | std::ifstream::ate);
    std::ifstream f2(p2, std::ifstream::binary | std::ifstream::ate);

    if (f1.fail() || f2.fail()) return false;
    if (f1.tellg() != f2.tellg()) return false; // Size mismatch

    f1.seekg(0, std::ifstream::beg);
    f2.seekg(0, std::ifstream::beg);
    return std::equal(std::istreambuf_iterator<char>(f1.rdbuf()),
                      std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(f2.rdbuf()));
}

// ==========================================
// UTILITY: Image Logic
// ==========================================
struct DiffResult {
    bool is_image;
    bool has_diff;
    std::string desc;
};

DiffResult try_visual_diff(const fs::path& p_golden, const fs::path& p_inj, const fs::path& mask_out) {
    cv::Mat a = cv::imread(p_golden.string(), cv::IMREAD_UNCHANGED);
    cv::Mat b = cv::imread(p_inj.string(), cv::IMREAD_UNCHANGED);

    if (a.empty() || b.empty()) {
        return {false, false, ""}; 
    }

    if (a.size() != b.size() || a.type() != b.type()) {
        return {true, true, "Size/Type mismatch"};
    }

    if (a.depth() == CV_16U) {
        a.convertTo(a, CV_8U, 1.0 / 257.0);
        b.convertTo(b, CV_8U, 1.0 / 257.0);
    } else if (a.depth() == CV_32F || a.depth() == CV_64F) {
        a.convertTo(a, CV_8U, 255.0);
        b.convertTo(b, CV_8U, 255.0);
    }

    if (a.channels() == 1) {
        cv::cvtColor(a, a, cv::COLOR_GRAY2BGR);
        cv::cvtColor(b, b, cv::COLOR_GRAY2BGR);
    }

    cv::Mat diff;
    cv::absdiff(a, b, diff);

    cv::Mat mask;
    cv::cvtColor(diff, mask, cv::COLOR_BGR2GRAY);
    cv::threshold(mask, mask, 0, 255, cv::THRESH_BINARY);

    int count = cv::countNonZero(mask);

    if (count == 0) {
        return {true, false, "Images Identical (Visual)"};
    }

    double pct = (double)count / (double)mask.total() * 100.0;
    
    cv::imwrite(mask_out.string(), mask);

    std::ostringstream oss;
    oss << "Img Diff: " << count << " px (" << std::fixed << std::setprecision(4) << pct << "%)";
    return {true, true, oss.str()};
}

// ==========================================
// CORE ANALYSIS FUNCTION
// ==========================================

struct AnalyzeStats {
    int total_injected = 0;
    
    // Total counters
    int crashed = 0;
    int hanged = 0;
    int sdc = 0;
    int benign = 0;
    int errors = 0;

    // Split counters (Reg / Mem)
    int crashed_reg = 0; int crashed_mem = 0;
    int hanged_reg = 0;  int hanged_mem = 0;
    int sdc_reg = 0;     int sdc_mem = 0;
    int benign_reg = 0;  int benign_mem = 0;
};

struct CsvRecord {
    std::string index;
    std::string type;
    std::string location; // New field: "Register" or "Memory"
    std::string details;
    std::string json_file;
};

void analyze_injection_campaign(fs::path base_path, int expected_runs) {
    fs::path golden_dir = base_path / "no_inj/injection_0";
    fs::path diff_root = base_path / "diff";

    if (fs::exists(diff_root)) {
        fs::remove_all(diff_root);
    }
    fs::create_directory(diff_root);

    std::vector<CsvRecord> csv_records;
    AnalyzeStats stats;

    std::cout << "Reference: " << golden_dir << "\nStarting analysis (" << expected_runs << " expected runs)...\n";

    // Use OpenMP to parallelize the loop
    // 'stats' and 'csv_records' must be protected
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < expected_runs; ++i) {
        
        // --- LOCAL VARS ---
        CsvRecord local_rec; 
        bool rec_valid = false;
        
        // --- LOCAL STATS Delta ---
        // (We calculate delta for this run and merge at the end)
        // Simplification: we just use a critical section at the end of iteration
        // to update the global stats struct.

        fs::path inj_dir = base_path / ("injection_" + std::to_string(i));
        if (!fs::exists(inj_dir)) continue;

        std::string current_json_filename = "injection_" + std::to_string(i) + ".json";
        fs::path json_path = inj_dir / current_json_filename;

        // 1. Load JSON
        std::ifstream json_file(json_path);
        if (!json_file.good()) {
            #pragma omp critical(stats_update)
            {
                csv_records.push_back({std::to_string(i), "ERROR", "UNKNOWN", "JSON missing/corrupt", current_json_filename});
                stats.errors++;
            }
            continue;
        }

        json meta_data;
        try {
            json_file >> meta_data;
        } catch (const std::exception& e) {
            #pragma omp critical(stats_update)
            {
                csv_records.push_back({std::to_string(i), "ERROR", "UNKNOWN", "JSON Parse Error", current_json_filename});
                stats.errors++;
            }
            continue;
        }

        // 2. Filter Logic
        auto& res_block = meta_data["result"];
        if (res_block.value("fault_injected", 0) != 1) {
            continue;
        }

        // 3. Determine Location (Memory vs Register)
        int mem_flip_val = res_block.value("memory_flip", -1);
        if(mem_flip_val == -1) mem_flip_val = meta_data.value("memory_flip", 0);

        bool is_memory = (mem_flip_val == 1);
        std::string loc_str = is_memory ? "Memory" : "Register";

        int exit_code = res_block.value("exit_code", 0);
        int process_hanged = res_block.value("process_hanged", 0);

        std::string status_type = "BENIGN";
        std::string status_details = "";
        fs::path experiment_diff_dir = diff_root / ("diff_" + std::to_string(i));
        bool sdc_detected = false;

        // 4. Classification
        if (exit_code != 0) {
            if (process_hanged == 1) {
                status_type = "HANG";
                status_details = "Exit: " + std::to_string(exit_code) + ", Hanged: 1";
            } else {
                status_type = "CRASH";
                status_details = "Exit: " + std::to_string(exit_code);
            }
        } else {
            // SDC Check Logic
            std::vector<std::string> details_list;
            
            for (const auto& entry : fs::directory_iterator(golden_dir)) {
                if (entry.path().extension() == ".json") continue;

                fs::path g_file = entry.path();
                fs::path i_file = inj_dir / g_file.filename();

                bool file_mismatch = false;
                std::string file_note = "";

                if (!fs::exists(i_file)) {
                    file_mismatch = true;
                    file_note = "MISSING: " + g_file.filename().string();
                } else if (!are_files_identical_binary(g_file, i_file)) {
                    file_mismatch = true;
                    
                    if (!fs::exists(experiment_diff_dir)) {
                        fs::create_directories(experiment_diff_dir);
                    }

                    try {
                        std::string g_name = g_file.stem().string() + "_GOLDEN" + g_file.extension().string();
                        std::string i_name = i_file.stem().string() + "_INJ" + i_file.extension().string();
                        fs::copy_file(g_file, experiment_diff_dir / g_name, fs::copy_options::overwrite_existing);
                        fs::copy_file(i_file, experiment_diff_dir / i_name, fs::copy_options::overwrite_existing);

                        fs::path mask_path = experiment_diff_dir / ("diff_mask_" + g_file.filename().string());
                        DiffResult img_res = try_visual_diff(g_file, i_file, mask_path);

                        if (img_res.is_image) {
                            file_note = "SDC " + g_file.filename().string() + " [" + img_res.desc + "]";
                        } else {
                            file_note = "SDC " + g_file.filename().string() + " (Binary Mismatch)";
                        }

                    } catch(const fs::filesystem_error& e) {
                        file_note = "SDC (Copy Error): " + std::string(e.what());
                    }
                }

                if (file_mismatch) {
                    sdc_detected = true;
                    details_list.push_back(file_note);
                }
            }

            if (sdc_detected) {
                status_type = "SDC";
                for(size_t k=0; k<details_list.size(); ++k) {
                    status_details += details_list[k];
                    if(k < details_list.size()-1) status_details += " | ";
                }
            }
        }

        // 5. Update shared stats
        #pragma omp critical(stats_update)
        {
            stats.total_injected++;
            if (status_type == "BENIGN") {
                stats.benign++;
                if (is_memory) stats.benign_mem++; else stats.benign_reg++;
            } else if (status_type == "HANG") {
                stats.hanged++;
                if (is_memory) stats.hanged_mem++; else stats.hanged_reg++;
            } else if (status_type == "CRASH") {
                stats.crashed++;
                if (is_memory) stats.crashed_mem++; else stats.crashed_reg++;
            } else if (status_type == "SDC") {
                stats.sdc++;
                if (is_memory) stats.sdc_mem++; else stats.sdc_reg++;
            }

            if (status_type != "BENIGN") {
                if (!fs::exists(experiment_diff_dir)) {
                    fs::create_directories(experiment_diff_dir);
                }
                fs::copy_file(json_path, experiment_diff_dir / current_json_filename, fs::copy_options::overwrite_existing);

                csv_records.push_back({std::to_string(i), status_type, loc_str, status_details, current_json_filename});
            }
        }
    }

    // 6. Final Report (CSV)
    fs::path summary_path = diff_root / "summary.csv";
    std::ofstream csv(summary_path);
    
    // Updated Header
    csv << "index,type,location,details,json_file\n";
    
    // csv_records order is no longer guaranteed to be 0..N, but that's fine for CSV. 
    // If needed we could sort them.
    for (const auto& rec : csv_records) {
        csv << rec.index << "," << rec.type << "," << rec.location << "," << "\"" << rec.details << "\"" << "," << rec.json_file << "\n";
    }

    // --- SUMMARY STATISTICS ---
    auto get_pct = [&](int val) { return stats.total_injected > 0 ? (val * 100.0 / stats.total_injected) : 0.0; };

    csv << ",,,,\n"; // Spacer
    csv << "---,---,---,---\n";
    csv << "STATS,TOTAL INJECTIONS," << stats.total_injected << ",,\n";
    csv << "STATS,CRASHED," << stats.crashed << " (" << std::fixed << std::setprecision(2) << get_pct(stats.crashed) << "%),,\n";
    csv << "STATS,HANGED," << stats.hanged << " (" << std::fixed << std::setprecision(2) << get_pct(stats.hanged) << "%),,\n";
    csv << "STATS,SDC," << stats.sdc << " (" << std::fixed << std::setprecision(2) << get_pct(stats.sdc) << "%),,\n";
    csv << "STATS,BENIGN," << stats.benign << " (" << std::fixed << std::setprecision(2) << get_pct(stats.benign) << "%),,\n";
    
    // --- FAILURE BREAKDOWN TABLE ---
    csv << ",,,,\n"; // Spacer
    csv << "BREAKDOWN BY LOCATION,,,,\n";
    csv << "TYPE,TOTAL,REGISTER,MEMORY,\n";
    csv << "CRASH," << stats.crashed << "," << stats.crashed_reg << "," << stats.crashed_mem << ",\n";
    csv << "HANG," << stats.hanged << "," << stats.hanged_reg << "," << stats.hanged_mem << ",\n";
    csv << "SDC," << stats.sdc << "," << stats.sdc_reg << "," << stats.sdc_mem << ",\n";
    csv << "BENIGN," << stats.benign << "," << stats.benign_reg << "," << stats.benign_mem << ",\n";

    csv.close();

    std::cout << "\nAnalysis Complete.\n";
    std::cout << "Total:   " << stats.total_injected << "\n";
    std::cout << "Crashed: " << stats.crashed << " (Reg: " << stats.crashed_reg << ", Mem: " << stats.crashed_mem << ")\n";
    std::cout << "Hanged:  " << stats.hanged  << " (Reg: " << stats.hanged_reg  << ", Mem: " << stats.hanged_mem  << ")\n";
    std::cout << "SDC:     " << stats.sdc     << " (Reg: " << stats.sdc_reg     << ", Mem: " << stats.sdc_mem     << ")\n";
    std::cout << "Summary saved to: " << summary_path << std::endl;
}