#include "fij.hpp"

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " CONFIG.json\n";
        return 1;
    }

    std::string config_path = argv[1];

    try {
        run_campaigns_from_config(config_path);
    } catch (const std::exception &e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
