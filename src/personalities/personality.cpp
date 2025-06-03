#include "personality.h"
#include <iostream>

namespace Stockfish {

// Definizione della variabile globale
Personality activePersonality;

// Stub: Simula il caricamento, ma non fa nulla realmente
bool Personality::load_from_file(const std::string& path) {
    std::cout << "info string Personality system initialized with default values (path: " << path << ")" << std::endl;
    return true; // sempre vero per ora
}

// Setta un parametro di valutazione personalizzato
void Personality::set_param(const std::string& name, int value) {
    evalParams[name] = value;
}

// Ottiene il valore di un parametro, o ritorna un valore di fallback
int Personality::get_evaluation_param(const std::string& name, int fallback) const {
    auto it = evalParams.find(name);
    return it != evalParams.end() ? it->second : fallback;
}

} // namespace Stockfish

