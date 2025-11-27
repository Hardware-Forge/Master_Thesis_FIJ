#include "fij.hpp"

// -----------------------------------------------------------------------------
// Helpers for working with fij_params char arrays
// -----------------------------------------------------------------------------

std::string cstr_from_fixed(const char *buf, std::size_t n) {
    return std::string(buf, strnlen(buf, n));
}

void set_process_name_from_path(struct fij_params &p) {
    std::string path = cstr_from_array(p.process_path);
    if (path.empty()) return;
    std::string base = fs::path(path).filename().string();
    set_cstring(p.process_name, base);
}

// -----------------------------------------------------------------------------
// Reg name mapping
// -----------------------------------------------------------------------------

int reg_name_to_id(const std::string &name) {
    static const std::unordered_map<std::string, int> map = {
#ifdef  CONFIG_X86
        {"rax", FIJ_REG_RAX},
        {"rbx", FIJ_REG_RBX},
        {"rcx", FIJ_REG_RCX},
        {"rdx", FIJ_REG_RDX},
        {"rsi", FIJ_REG_RSI},
        {"rdi", FIJ_REG_RDI},
        {"rbp", FIJ_REG_RBP},
        {"rsp", FIJ_REG_RSP},
        {"pc",  FIJ_REG_RIP},
        {"rip", FIJ_REG_RIP},
        {"r8",  FIJ_REG_R8},
        {"r9",  FIJ_REG_R9},
        {"r10", FIJ_REG_R10},
        {"r11", FIJ_REG_R11},
        {"r12", FIJ_REG_R12},
        {"r13", FIJ_REG_R13},
        {"r14", FIJ_REG_R14},
        {"r15", FIJ_REG_R15},
#endif

#ifdef CONFIG_ARM64
        /* AArch64 GPRs x0–x30, SP, PC */
        {"x0",  FIJ_REG_X0},
        {"x1",  FIJ_REG_X1},
        {"x2",  FIJ_REG_X2},
        {"x3",  FIJ_REG_X3},
        {"x4",  FIJ_REG_X4},
        {"x5",  FIJ_REG_X5},
        {"x6",  FIJ_REG_X6},
        {"x7",  FIJ_REG_X7},
        {"x8",  FIJ_REG_X8},
        {"x9",  FIJ_REG_X9},
        {"x10", FIJ_REG_X10},
        {"x11", FIJ_REG_X11},
        {"x12", FIJ_REG_X12},
        {"x13", FIJ_REG_X13},
        {"x14", FIJ_REG_X14},
        {"x15", FIJ_REG_X15},
        {"x16", FIJ_REG_X16},
        {"x17", FIJ_REG_X17},
        {"x18", FIJ_REG_X18},
        {"x19", FIJ_REG_X19},
        {"x20", FIJ_REG_X20},
        {"x21", FIJ_REG_X21},
        {"x22", FIJ_REG_X22},
        {"x23", FIJ_REG_X23},
        {"x24", FIJ_REG_X24},
        {"x25", FIJ_REG_X25},
        {"x26", FIJ_REG_X26},
        {"x27", FIJ_REG_X27},
        {"x28", FIJ_REG_X28},
        {"x29", FIJ_REG_X29},   /* fp */
        {"x30", FIJ_REG_X30},   /* lr */
        {"fp",  FIJ_REG_X29},
        {"lr",  FIJ_REG_X30},
        {"sp",  FIJ_REG_SP},
        {"pc",  FIJ_REG_PC},
#endif

#ifdef CONFIG_RISCV
        /* RISC-V integer regs: ABI + numeric aliases + pc */
        {"zero", FIJ_REG_ZERO}, /* x0 */
        {"x0",   FIJ_REG_ZERO},

        {"ra",   FIJ_REG_RA},   /* x1 */
        {"x1",   FIJ_REG_RA},

        {"sp",   FIJ_REG_SP},   /* x2 */
        {"x2",   FIJ_REG_SP},

        {"gp",   FIJ_REG_GP},   /* x3 */
        {"x3",   FIJ_REG_GP},

        {"tp",   FIJ_REG_TP},   /* x4 */
        {"x4",   FIJ_REG_TP},

        {"t0",   FIJ_REG_T0},   /* x5 */
        {"x5",   FIJ_REG_T0},
        {"t1",   FIJ_REG_T1},   /* x6 */
        {"x6",   FIJ_REG_T1},
        {"t2",   FIJ_REG_T2},   /* x7 */
        {"x7",   FIJ_REG_T2},

        {"s0",   FIJ_REG_S0},   /* x8 / fp */
        {"fp",   FIJ_REG_S0},
        {"x8",   FIJ_REG_S0},
        {"s1",   FIJ_REG_S1},   /* x9 */
        {"x9",   FIJ_REG_S1},

        {"a0",   FIJ_REG_A0},   /* x10 */
        {"x10",  FIJ_REG_A0},
        {"a1",   FIJ_REG_A1},   /* x11 */
        {"x11",  FIJ_REG_A1},
        {"a2",   FIJ_REG_A2},   /* x12 */
        {"x12",  FIJ_REG_A2},
        {"a3",   FIJ_REG_A3},   /* x13 */
        {"x13",  FIJ_REG_A3},
        {"a4",   FIJ_REG_A4},   /* x14 */
        {"x14",  FIJ_REG_A4},
        {"a5",   FIJ_REG_A5},   /* x15 */
        {"x15",  FIJ_REG_A5},
        {"a6",   FIJ_REG_A6},   /* x16 */
        {"x16",  FIJ_REG_A6},
        {"a7",   FIJ_REG_A7},   /* x17 */
        {"x17",  FIJ_REG_A7},

        {"s2",   FIJ_REG_S2},   /* x18 */
        {"x18",  FIJ_REG_S2},
        {"s3",   FIJ_REG_S3},   /* x19 */
        {"x19",  FIJ_REG_S3},
        {"s4",   FIJ_REG_S4},   /* x20 */
        {"x20",  FIJ_REG_S4},
        {"s5",   FIJ_REG_S5},   /* x21 */
        {"x21",  FIJ_REG_S5},
        {"s6",   FIJ_REG_S6},   /* x22 */
        {"x22",  FIJ_REG_S6},
        {"s7",   FIJ_REG_S7},   /* x23 */
        {"x23",  FIJ_REG_S7},
        {"s8",   FIJ_REG_S8},   /* x24 */
        {"x24",  FIJ_REG_S8},
        {"s9",   FIJ_REG_S9},   /* x25 */
        {"x25",  FIJ_REG_S9},
        {"s10",  FIJ_REG_S10},  /* x26 */
        {"x26",  FIJ_REG_S10},
        {"s11",  FIJ_REG_S11},  /* x27 */
        {"x27",  FIJ_REG_S11},

        {"t3",   FIJ_REG_T3},   /* x28 */
        {"x28",  FIJ_REG_T3},
        {"t4",   FIJ_REG_T4},   /* x29 */
        {"x29",  FIJ_REG_T4},
        {"t5",   FIJ_REG_T5},   /* x30 */
        {"x30",  FIJ_REG_T5},
        {"t6",   FIJ_REG_T6},   /* x31 */
        {"x31",  FIJ_REG_T6},

        {"pc",   FIJ_REG_PC},
#endif
    };

    auto it = map.find(name);
    if (it == map.end()) return FIJ_REG_NONE;
    return it->second;
}

// -----------------------------------------------------------------------------
// fij_params_apply_defaults
// -----------------------------------------------------------------------------

void fij_params_apply_defaults(struct fij_params &p) {
    auto get_cstring = [](auto &arr) {
        return cstr_from_array(arr);
    };

    if (get_cstring(p.process_name).empty() && !get_cstring(p.process_path).empty()) {
        set_process_name_from_path(p);
    }

    auto norm_bool = [](int v) -> int { return v ? 1 : 0; };
    p.only_mem        = norm_bool(p.only_mem);
    p.thread_present  = norm_bool(p.thread_present);
    p.all_threads     = norm_bool(p.all_threads);
    p.process_present = norm_bool(p.process_present);
    p.no_injection    = norm_bool(p.no_injection);

    if (p.weight_mem < 0) p.weight_mem = 0;

    if (p.target_reg == 0) p.target_reg = FIJ_REG_NONE;

    if (!p.thread_present) {
        p.thread = 0;
    }

    if (!p.reg_bit_present) {
        p.reg_bit = 0;
    } else {
        if (p.reg_bit < 0) p.reg_bit = 0;
        if (p.reg_bit > 63) p.reg_bit = 63;
    }

    if (!p.target_pc_present) {
        p.target_pc = 0;
    }

    if (p.min_delay_ms && p.max_delay_ms && p.max_delay_ms < p.min_delay_ms) {
        int tmp = p.min_delay_ms;
        p.min_delay_ms = p.max_delay_ms;
        p.max_delay_ms = tmp;
    }
}

// -----------------------------------------------------------------------------
// Basic utilities
// -----------------------------------------------------------------------------

int bool_int(const json &v) {
    if (v.is_boolean()) {
        return v.get<bool>() ? 1 : 0;
    }
    if (v.is_number()) {
        return v.get<double>() != 0.0 ? 1 : 0;
    }
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        std::string lower;
        lower.reserve(s.size());
        for (char c : s) lower.push_back(std::tolower(c));
        return (lower == "1" || lower == "true" || lower == "yes" || lower == "on") ? 1 : 0;
    }
    return 0;
}

void apply_field_if_present(
    struct fij_params &p,
    const json &cfg,
    const char *key,
    int fij_params::*field,
    bool boolean
) {
    auto it = cfg.find(key);
    if (it != cfg.end()) {
        if (boolean) {
            p.*field = bool_int(*it);
        } else {
            p.*field = static_cast<int>(it->get<long long>());
        }
    }
}

// -----------------------------------------------------------------------------
// create_dir_in_path
// -----------------------------------------------------------------------------

fs::path create_dir_in_path(const fs::path &base_path, const std::string &final_folder) {
    fs::create_directories(base_path);

    std::string candidate = final_folder;
    int i = 1;

    while (true) {
        fs::path target = base_path / candidate;
        std::error_code ec;
        if (fs::create_directory(target, ec)) {
            return fs::absolute(target);
        }
        if (ec && ec.value() != EEXIST) {
            throw std::system_error(ec);
        }
        candidate = final_folder + "(" + std::to_string(i) + ")";
        ++i;
    }
}

// -----------------------------------------------------------------------------
// log_injection_iteration
// -----------------------------------------------------------------------------

void log_injection_iteration(
    const fs::path &base_path,
    int i,
    double dt_seconds,
    const struct fij_result &res
) {
    fs::path folder = base_path / ("injection_" + std::to_string(i));
    fs::create_directories(folder);

    json raw_result;

    raw_result["iteration_number"]  = res.iteration_number;
    raw_result["fault_injected"]    = res.fault_injected;
    raw_result["signal"]            = res.sigal;
    raw_result["process_hanged"]    = res.process_hanged;
    raw_result["exit_code"]         = res.exit_code;
    raw_result["target_tgid"]       = res.target_tgid;
    raw_result["pid_idx"]           = res.pid_idx;
    raw_result["thread_idx"]        = res.thread_idx;
    raw_result["injection_time_ns"] = static_cast<std::uint64_t>(res.injection_time_ns);
    raw_result["memory_flip"]       = res.memory_flip;

    auto to_hex64 = [](std::uint64_t v) {
        std::ostringstream oss;
        oss << "0x" << std::setw(16) << std::setfill('0') << std::hex << v;
        return oss.str();
    };

    std::uint64_t addr   = res.target_address;
    std::uint64_t before = res.target_before & 0xFFull;
    std::uint64_t after  = res.target_after & 0xFFull;

    raw_result["target_address"] = to_hex64(addr);
    raw_result["target_before"]  = to_hex64(before);
    raw_result["target_after"]   = to_hex64(after);

    raw_result["register_name"] =
        cstr_from_fixed(res.register_name, sizeof(res.register_name));

    json payload;
    payload["iteration"]   = i;

    // Timestamp in UTC, ISO-ish: YYYY-MM-DDTHH:MM:SSZ
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    char buf[64];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc)) {
        payload["timestamp"] = std::string(buf);
    } else {
        payload["timestamp"] = "";
    }

    payload["duration_ms"] = dt_seconds * 1000.0;
    payload["result"]      = raw_result;

    fs::path out_file = folder / ("injection_" + std::to_string(i) + ".json");
    std::ofstream ofs(out_file);
    ofs << std::setw(2) << payload << std::endl;
}

// -----------------------------------------------------------------------------
// label_from_params
// -----------------------------------------------------------------------------

std::string label_from_params(const struct fij_params &p) {
    std::string path = cstr_from_array(p.process_path);
    std::string args = cstr_from_array(p.process_args);

    if (!path.empty() && !args.empty()) {
        return path + " '" + args + "'";
    }
    if (!path.empty()) return path;
    return "<unknown>";
}
