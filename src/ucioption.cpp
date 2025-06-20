/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2023 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cassert>
#include <ostream>
#include <sstream>

#include "evaluate.h"
#include "experience.h"
#include "misc.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "polybook.h"
#include "personalities/personality.h"

using std::string;

namespace Stockfish {

UCI::OptionsMap Options; // Global object for UCI options
std::string previousBookFile = "<empty>"; // Tracks the last loaded book

namespace UCI {

void sync_uci_options(); // Declaration to avoid the warning

bool personalityChanged = true; // Global variable

/// 'On change' actions, triggered by an option's value change
static void on_clear_hash(const Option&) { Search::clear(); }
static void on_hash_size(const Option& o) { TT.resize(size_t(o)); }
static void on_logger(const Option& o) { start_logger(o); }
static void on_threads(const Option& o) { Threads.set(size_t(o)); }
static void on_exp_enabled(const Option& /*o*/) { Experience::init(); }
static void on_exp_file(const Option& /*o*/) { Experience::init(); }

static void on_book_file(const Option& o) {
    std::string newBookFile = static_cast<std::string>(o);
    std::cout << "info string Book file set to: " << newBookFile << std::endl;

    activePersonality.BookFile = newBookFile;
    polybook[0].init(newBookFile);
    previousBookFile = newBookFile;

    std::cout << "info string Book loaded: " << newBookFile << std::endl;
}

void sync_uci_options() {
 std::cout << "info string Syncing UCI options with active personality..." << std::endl;

															   
													 
    Options["PersonalityBook"] = std::string(activePersonality.PersonalityBook ? "true" : "false");
    Options["Book File"] = activePersonality.BookFile;
    Options["Book Width"] = std::to_string(activePersonality.BookWidth);
    Options["Book Depth"] = std::to_string(activePersonality.BookDepth);
    Options["HumanImperfection"] = std::to_string(activePersonality.get_evaluation_param("HumanImperfection", 0));

    std::cout << "setoption name HumanImperfection value " << Options["HumanImperfection"] << std::endl;
    std::cout << "info string UCI options successfully synced." << std::endl;

    // Force the GUI to reload the updated options
    std::cout << "isready" << std::endl;
    std::cout << "uci" << std::endl;
}

bool CaseInsensitiveLess::operator() (const string& s1, const string& s2) const {
    return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
         [](char c1, char c2) { return tolower(c1) < tolower(c2); });
}

void init(OptionsMap& o) {
    constexpr int MaxHashMB = Is64Bit ? 33554432 : 2048;

    // Impostazione esplicita di default per evitare problemi
    activePersonality.PersonalityBook = true;

    o["Debug Log File"]        << Option("", on_logger);
    o["Threads"]               << Option(1, 1, 1, on_threads);
    o["Hash"]                  << Option(16, 1, MaxHashMB, on_hash_size);
    o["Clear Hash"]            << Option(on_clear_hash);
    o["Ponder"]                << Option(false);
    o["MultiPV"]               << Option(1, 1, 500);
    o["Skill Level"]           << Option(20, 0, 20);
    o["Move Overhead"]         << Option(10, 0, 5000);
    o["Slow Mover"]            << Option(100, 10, 1000);
    o["nodestime"]             << Option(0, 0, 10000);
    o["UCI_Chess960"]          << Option(false);
    o["UCI_ShowWDL"]           << Option(false);
    o["Personality"]           << Option(false);

    o["Elo"] << Option(1320, 1320, 3190, [](const Option& v) {
        int uci_elo = int(v);
        std::cout << "info string UCI Elo changed to " << uci_elo << std::endl;

        // === HumanImperfection Calculation ===
        int humanImperfection = ((3190 - uci_elo) * 50) / (3190 - 1320);
        humanImperfection = std::clamp(humanImperfection, 0, 50);
        activePersonality.set_param("HumanImperfection", humanImperfection);
        std::cout << "info string Calculated HumanImperfection: " << humanImperfection << std::endl;

        // === Dynamic RandomMoveDepth calculation ===
        int dynamicRandomDepth = 4 + (uci_elo - 1320) * 16 / (3190 - 1320);
        dynamicRandomDepth = std::clamp(dynamicRandomDepth, 0, 20);
        activePersonality.RandomMoveDepth = dynamicRandomDepth;
        std::cout << "info string Calculated RandomMoveDepth: " << dynamicRandomDepth << std::endl;

        // === Dynamic MoveDelayMs calculation ===
        int delayMs = 1000 - (uci_elo - 1320) * 900 / (3190 - 1320);
        delayMs = std::clamp(delayMs, 100, 1000);
        activePersonality.MoveDelayMs = delayMs;
        std::cout << "info string Calculated MoveDelayMs: " << delayMs << " ms" << std::endl;

        // === Dynamic BlunderRate calculation ===
        int blunderRate = ((3190 - uci_elo) * 50) / (3190 - 1320);
        blunderRate = std::clamp(blunderRate, 0, 50);
        activePersonality.BlunderRate = blunderRate;
        std::cout << "info string Calculated BlunderRate: " << blunderRate << "%" << std::endl;

        // === Optional: Enable TrainingMode automatically at low Elo ===
        if (uci_elo <= 1600) {
            activePersonality.TrainingMode = true;
            std::cout << "info string TrainingMode activated automatically for Elo <= 1600" << std::endl;
        }
    });

    // Book Options
    o["PersonalityBook"]   << Option(true, [](const Option& v) { activePersonality.PersonalityBook = bool(v); });
    o["Book File"]         << Option("Human.bin", on_book_file);
    on_book_file(o["Book File"]);  // <-- Aggiunta questa chiamata per caricare subito il libro all'avvio
    o["Book Width"]        << Option(1, 1, 20, [](const Option& v) { activePersonality.BookWidth = int(v); });
    o["Book Depth"]        << Option(1, 1, 30, [](const Option& v) { activePersonality.BookDepth = int(v); });
    o["Experience Enabled"]                  << Option(true, on_exp_enabled);
    o["Experience File"]                     << Option("HumanMind.exp", on_exp_file);
    o["Experience Book"]                     << Option(false);
    o["Experience Book Best Move"]           << Option(true);
    o["Experience Book Eval Importance"]     << Option(5, 0, 10);
    o["Experience Book Min Depth"]           << Option(27, EXP_MIN_DEPTH, 64);
    o["Experience Book Max Moves"]           << Option(100, 1, 100);

    // Human training options
    o["TrainingMode"]      << Option(false, [](const Option& v) { activePersonality.TrainingMode = bool(v); });

    // Synchronization and output
    // Carica libro solo se PersonalityBook attivo
    if (activePersonality.PersonalityBook) {
        on_book_file(o["Book File"]);
    }

    sync_uci_options();
    Stockfish::UCI::personalityChanged = true;
    Stockfish::activePersonality = activePersonality;

    std::cout << "info string Personality initialized (default values used)" << std::endl;
    std::cout << "setoption name PersonalityBook value " << (activePersonality.PersonalityBook ? "true" : "false") << std::endl;
    std::cout << "setoption name Book File value "       << activePersonality.BookFile << std::endl;
    std::cout << "setoption name Book Width value "      << activePersonality.BookWidth << std::endl;
    std::cout << "setoption name Book Depth value "      << activePersonality.BookDepth << std::endl;

    std::cout << "uci" << std::endl;
    std::cout << "isready" << std::endl;

    if (activePersonality.PersonalityBook) {
        std::cout << "info string Loading personality book: " << activePersonality.BookFile << std::endl;
        std::cout << "info string Book Width: " << activePersonality.BookWidth << std::endl;
        std::cout << "info string Book Depth: " << activePersonality.BookDepth << std::endl;
    } else {
        std::cout << "info string No personality book assigned." << std::endl;
    }

    std::cout << "info string - HumanImperfection: " << activePersonality.get_evaluation_param("HumanImperfection", 0) << std::endl;

    if (activePersonality.TrainingMode) {
        std::cout << "info string Training mode ON" << std::endl;
        std::cout << "info string Blunder Rate: " << activePersonality.BlunderRate << "%" << std::endl;
        std::cout << "info string Random Move Depth: " << activePersonality.RandomMoveDepth << std::endl;
        std::cout << "info string Move Delay (ms): " << activePersonality.MoveDelayMs << std::endl;
    }

    std::cout << "info string UCI options successfully synced with personality!" << std::endl;
    std::cout << "isready" << std::endl;
    std::cout << "uci" << std::endl;
}


std::ostream& operator<<(std::ostream& os, const OptionsMap& om) {
    for (size_t idx = 0; idx < om.size(); ++idx)
        for (const auto& it : om)
            if (it.second.idx == idx) {
                const Option& o = it.second;
                os << "\noption name " << it.first << " type " << o.type;

                if (o.type == "string" || o.type == "check" || o.type == "combo")
                    os << " default " << o.defaultValue;

                if (o.type == "spin")
                    os << " default " << int(stof(o.defaultValue))
                       << " min " << o.min
                       << " max " << o.max;

                break;
            }

    return os;
}

// Option class

Option::Option(const char* v, OnChange f) : type("string"), min(0), max(0), on_change(f)
{ defaultValue = currentValue = v; }

Option::Option(bool v, OnChange f) : type("check"), min(0), max(0), on_change(f)
{ defaultValue = currentValue = (v ? "true" : "false"); }

Option::Option(OnChange f) : type("button"), min(0), max(0), on_change(f) {}

Option::Option(double v, int minv, int maxv, OnChange f)
    : type("spin"), min(minv), max(maxv), on_change(f)
{ defaultValue = currentValue = std::to_string(v); }

Option::Option(const char* v, const char* cur, OnChange f)
    : type("combo"), min(0), max(0), on_change(f)
{ defaultValue = v; currentValue = cur; }

Option::operator int() const {
    assert(type == "check" || type == "spin");
    return (type == "spin" ? std::stoi(currentValue) : currentValue == "true");
}

Option::operator std::string() const {
    assert(type == "string");
    return currentValue;
}

bool Option::operator==(const char* s) const {
    assert(type == "combo");
    return !CaseInsensitiveLess()(currentValue, s) && !CaseInsensitiveLess()(s, currentValue);
}

void Option::operator<<(const Option& o) {
    static size_t insert_order = 0;
    *this = o;
    idx = insert_order++;
}

Option& Option::operator=(const string& v) {
    assert(!type.empty());

    if ((type != "button" && type != "string" && v.empty()) ||
        (type == "check" && v != "true" && v != "false") ||
        (type == "spin" && (stof(v) < min || stof(v) > max)))
        return *this;

    if (type == "combo") {
        OptionsMap comboMap;
        string token;
        std::istringstream ss(defaultValue);
        while (ss >> token)
            comboMap[token] << Option();
        if (!comboMap.count(v) || v == "var")
            return *this;
    }

    if (type != "button")
        currentValue = v;

    if (on_change)
        on_change(*this);

    return *this;
}

} // namespace UCI

} // namespace Stockfish
