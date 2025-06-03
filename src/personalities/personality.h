#pragma once

#include <string>
#include <unordered_map>

namespace Stockfish {

class Personality {
public:
    bool PersonalityBook = false;
    std::string BookFile;
    int BookWidth = 1;
    int BookDepth = 1;

    bool load_from_file(const std::string& path); // Stub
    void set_param(const std::string& name, int value);
    int get_evaluation_param(const std::string& name, int fallback = 0) const;

private:
    std::unordered_map<std::string, int> evalParams;
};

// Global instance
extern Personality activePersonality;

} // namespace Stockfish
