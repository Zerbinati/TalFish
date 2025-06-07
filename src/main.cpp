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

#include <iostream>
#include<stdio.h>

#include "bitboard.h"
#include "endgame.h"

#include "misc.h"
#include "polybook.h"
#include "position.h"
#include "psqt.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "experience.h"

using namespace Stockfish;

int main(int argc, char* argv[]) {

  Utility::init(argv[0]);
  SysInfo::init();
  show_logo();

  std::cout << engine_info() << std::endl;

  CommandLine::init(argc, argv);

  std::cout
      << "Operating System (OS) : " << SysInfo::os_info() << std::endl
      << "CPU Brand             : " << SysInfo::processor_brand() << std::endl
      << "NUMA Nodes            : " << SysInfo::numa_nodes() << std::endl
      << "Cores                 : " << SysInfo::physical_cores() << std::endl
      << "Threads               : " << SysInfo::logical_cores() << std::endl
      << "Hyper-Threading       : " << SysInfo::is_hyper_threading() << std::endl
      << "L1/L2/L3 cache size   : " << SysInfo::cache_info(0) << "/" << SysInfo::cache_info(1) << "/" << SysInfo::cache_info(2) << std::endl
      << "Memory installed (RAM): " << SysInfo::total_memory() << std::endl << std::endl;

  UCI::init(Options);
  Tune::init();
  PSQT::init();
  Bitboards::init();
  Position::init();
  Bitbases::init();
  Endgames::init();
  Experience::init();
  Threads.set(size_t(Options["Threads"]));
  polybook[0].init(Options["Book File"]);
  Search::clear(); // After threads are up

  UCI::loop(argc, argv);

  Experience::unload();
  Threads.set(0);
  return 0;
}
