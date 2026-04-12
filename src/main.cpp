#include "Core/aero_engine.h"
#include <iostream>

int main() {
    AeroEngine& engine = AeroEngine::Get();

    try {
        engine.init();
        engine.run();
        engine.cleanup();
    }
    catch (const std::exception& e) {
        std::cerr << "[Fatal Error] " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}