#include "fij.hpp"

std::vector<FijJob> build_fij_jobs_from_config(const json &config) {
    json global_defaults = config.value("defaults", json::object());
    json targets         = config.value("targets",  json::array());
    std::string base_path = config.value("base_path", std::string());
    int workers = config.value("workers", 1);

    std::vector<FijJob> jobs;

    for (const auto &t : targets) {
        if (!t.contains("path")) {
            throw std::runtime_error("Each target must have a 'path'");
        }

        std::string raw_path = t["path"].get<std::string>();
        std::string path = raw_path;
        if (!base_path.empty()) {
            std::string placeholder = "{base_path}";
            std::size_t pos = 0;
            while ((pos = path.find(placeholder, pos)) != std::string::npos) {
                path.replace(pos, placeholder.size(), base_path);
                pos += base_path.size();
            }
        }

        json target_defaults = global_defaults;
        if (t.contains("defaults") && t["defaults"].is_object()) {
            for (auto it = t["defaults"].begin(); it != t["defaults"].end(); ++it) {
                target_defaults[it.key()] = it.value();
            }
        }

        json args_list = t.value("args", json::array());
        if (args_list.empty()) {
            args_list = json::array({ json::object() });
        }

        for (const auto &arg_cfg : args_list) {
            json merged = global_defaults;
            if (t.contains("defaults") && t["defaults"].is_object()) {
                for (auto it = t["defaults"].begin(); it != t["defaults"].end(); ++it) {
                    merged[it.key()] = it.value();
                }
            }
            for (auto it = arg_cfg.begin(); it != arg_cfg.end(); ++it) {
                merged[it.key()] = it.value();
            }

            int runs = merged.value("runs", 1);
            if (runs <= 0) continue;

            struct fij_params p{};
            set_cstring(p.process_path, path);
            set_process_name_from_path(p);

            // Args: "value" or "args"
            std::string arg_val;
            if (merged.contains("value")) {
                arg_val = merged["value"].get<std::string>();
            } else if (merged.contains("args")) {
                arg_val = merged["args"].get<std::string>();
            }

            if (!base_path.empty()) {
                std::string placeholder = "{base_path}";
                std::size_t pos = 0;
                while ((pos = arg_val.find(placeholder, pos)) != std::string::npos) {
                    arg_val.replace(pos, placeholder.size(), base_path);
                    pos += base_path.size();
                }
            }
            set_cstring(p.process_args, arg_val);

            // numeric / boolean
            apply_field_if_present(p, merged, "weight_mem",   &fij_params::weight_mem);
            apply_field_if_present(p, merged, "min_delay_ms", &fij_params::min_delay_ms);
            apply_field_if_present(p, merged, "max_delay_ms", &fij_params::max_delay_ms);

            apply_field_if_present(p, merged, "only_mem",     &fij_params::only_mem,     true);
            apply_field_if_present(p, merged, "no_injection", &fij_params::no_injection, true);
            apply_field_if_present(p, merged, "all_threads",  &fij_params::all_threads,  true);

            if (merged.contains("thread")) {
                p.thread_present = 1;
                p.thread = static_cast<int>(merged["thread"].get<long long>());
            }

            if (merged.contains("nprocess")) {
                p.process_present = 1;
                p.nprocess = static_cast<int>(merged["nprocess"].get<long long>());
            }

            if (merged.contains("pc")) {
                p.target_pc_present = 1;
                if (merged["pc"].is_string()) {
                    std::string v = merged["pc"].get<std::string>();
                    p.target_pc = std::stoi(v, nullptr, 0);
                } else {
                    p.target_pc = static_cast<int>(merged["pc"].get<long long>());
                }
            }

            if (merged.contains("reg")) {
                std::string regname = merged["reg"].get<std::string>();
                std::cout << "regname=" << regname;
                std::cout << "regid= " << reg_name_to_id(regname);
                p.target_reg = reg_name_to_id(regname);
            }

            if (merged.contains("bit")) {
                p.reg_bit_present = 1;
                p.reg_bit = static_cast<int>(merged["bit"].get<long long>());
            }

            fij_params_apply_defaults(p);

            FijJob job;
            job.path    = path;
            job.args    = arg_val;
            job.runs    = runs;
            job.params  = p;
            job.workers = workers;
            jobs.push_back(job);
        }
    }

    return jobs;
}

std::vector<FijJob> load_fij_jobs_from_file(const std::string &config_path) {
    std::ifstream ifs(config_path);
    if (!ifs) {
        throw std::runtime_error("Cannot open config file: " + config_path);
    }

    std::string text;
    std::string line;
    while (std::getline(ifs, line)) {
        auto pos = line.find("//");
        if (pos != std::string::npos) {
            line = line.substr(0, pos);
        }
        text += line;
        text += "\n";
    }

    json cfg = json::parse(text);
    return build_fij_jobs_from_config(cfg);
}
