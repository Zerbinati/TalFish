#include "personality.h"
#include <iostream>

namespace Stockfish {

// Global instance of the active personality, used by the engine
Personality activePersonality;

// Initialize the personality system with default hardcoded values
bool Personality::load_from_file(const std::string&) {
    std::cout << "info string Personality system initialized with default internal values" << std::endl;

    // Static values set at startup
    BlunderRate       = 0;
    InaccuracyBias    = 0;
    RandomMoveDepth   = 0;
    MoveDelayMs       = 0;
    TrainingMode      = false;

    // Set dynamic parameter defaults
    set_param("HumanImperfection", 0);

    return true;
}

// Assigns a named evaluation parameter (dynamic)
void Personality::set_param(const std::string& name, int value) {
    evalParams[name] = value;
}

// Retrieves a dynamic evaluation parameter, or fallback if not set
int Personality::get_evaluation_param(const std::string& name, int fallback) const {
    auto it = evalParams.find(name);
    return it != evalParams.end() ? it->second : fallback;
}

} // namespace Stockfish


