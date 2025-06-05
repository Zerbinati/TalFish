#pragma once

#include <string>
#include <unordered_map>

namespace Stockfish {

class Personality {
public:
    // Book-related options
    bool PersonalityBook = false;
    std::string BookFile;
    int BookWidth = 1;
    int BookDepth = 1;

    // Training and error simulation parameters
    int BlunderRate = 0;         // Percentage chance of a blunder
    int InaccuracyBias = 0;      // How likely the engine favors inaccurate moves
    int RandomMoveDepth = 0;     // How deep randomization applies
    int MoveDelayMs = 0;         // Delay in milliseconds before playing
    bool TrainingMode = false;   // Toggles special training behaviors

    // Core methods
    bool load_from_file(const std::string&); // Initializes default values (no file used)
    void set_param(const std::string& name, int value); // Set named dynamic parameter
    int get_evaluation_param(const std::string& name, int fallback = 0) const; // Get value or fallback

private:
    std::unordered_map<std::string, int> evalParams; // For dynamic evaluation parameters (e.g., HumanImperfection)
};

// Global active personality instance
extern Personality activePersonality;

} // namespace Stockfish

