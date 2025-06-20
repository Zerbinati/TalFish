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
#include <cstddef> // For offsetof()
#include <cstring> // For std::memset, std::memcmp
#include <iomanip>
#include <sstream>
#include <string_view>

#include "bitboard.h"
#include "misc.h"
#include "movegen.h"
#include "position.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

using std::string;

namespace Stockfish {

namespace Zobrist {

  Key psq[PIECE_NB][SQUARE_NB];
  Key enpassant[FILE_NB];
  Key castling[CASTLING_RIGHT_NB];
  Key side, noPawns;
}

namespace {

constexpr std::string_view PieceToChar(" PNBRQK  pnbrqk");

constexpr Piece Pieces[] = { W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
                             B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING };
} // namespace


/// operator<<(Position) returns an ASCII representation of the position

std::ostream& operator<<(std::ostream& os, const Position& pos) {

  os << "\n +---+---+---+---+---+---+---+---+\n";

  for (Rank r = RANK_8; r >= RANK_1; --r)
  {
      for (File f = FILE_A; f <= FILE_H; ++f)
          os << " | " << PieceToChar[pos.piece_on(make_square(f, r))];

      os << " | " << (1 + r) << "\n +---+---+---+---+---+---+---+---+\n";
  }

  os << "   a   b   c   d   e   f   g   h\n"
     << "\nFen: " << pos.fen() << "\nKey: " << std::hex << std::uppercase
     << std::setfill('0') << std::setw(16) << pos.key()
     << std::setfill(' ') << std::dec << "\nCheckers: ";

  for (Bitboard b = pos.checkers(); b; )
      os << UCI::square(pop_lsb(b)) << " ";

  return os;
}

// Marcel van Kervinck's cuckoo algorithm for fast detection of "upcoming repetition"
// situations. Description of the algorithm in the following paper:
// http://web.archive.org/web/20201107002606/https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf

// First and second hash functions for indexing the cuckoo tables
inline int H1(Key h) { return h & 0x1fff; }
inline int H2(Key h) { return (h >> 16) & 0x1fff; }

// Cuckoo tables with Zobrist hashes of valid reversible moves, and the moves themselves
Key cuckoo[8192];
Move cuckooMove[8192];


/// Position::init() initializes at startup the various arrays used to compute hash keys

void Position::init() {
    //Fixed Zobrist keys to maintain experience Key integrity
    Zobrist::psq[1][0]    = 591679071752537765ULL;
    Zobrist::psq[1][1]    = 11781298203991720739ULL;
    Zobrist::psq[1][2]    = 17774509420834274491ULL;
    Zobrist::psq[1][3]    = 93833316982319649ULL;
    Zobrist::psq[1][4]    = 5077288827755375591ULL;
    Zobrist::psq[1][5]    = 12650468822090308278ULL;
    Zobrist::psq[1][6]    = 7282142511083249914ULL;
    Zobrist::psq[1][7]    = 10536503665313592279ULL;
    Zobrist::psq[1][8]    = 4539792784031873725ULL;
    Zobrist::psq[1][9]    = 2841870292508388689ULL;
    Zobrist::psq[1][10]   = 15413206348252250872ULL;
    Zobrist::psq[1][11]   = 7678569077154129441ULL;
    Zobrist::psq[1][12]   = 13346546310876667408ULL;
    Zobrist::psq[1][13]   = 18288271767696598454ULL;
    Zobrist::psq[1][14]   = 10369369943721775254ULL;
    Zobrist::psq[1][15]   = 18081987910875800766ULL;
    Zobrist::psq[1][16]   = 5538285989180528017ULL;
    Zobrist::psq[1][17]   = 1561342000895978098ULL;
    Zobrist::psq[1][18]   = 344529452680813775ULL;
    Zobrist::psq[1][19]   = 12666574946949763448ULL;
    Zobrist::psq[1][20]   = 11485456468243178719ULL;
    Zobrist::psq[1][21]   = 7930595158480463155ULL;
    Zobrist::psq[1][22]   = 14302725423041560508ULL;
    Zobrist::psq[1][23]   = 14331261293281981139ULL;
    Zobrist::psq[1][24]   = 4456874005134181239ULL;
    Zobrist::psq[1][25]   = 2824504039224593559ULL;
    Zobrist::psq[1][26]   = 10380971965294849792ULL;
    Zobrist::psq[1][27]   = 15120440200421969569ULL;
    Zobrist::psq[1][28]   = 2459658218254782268ULL;
    Zobrist::psq[1][29]   = 3478717432759217624ULL;
    Zobrist::psq[1][30]   = 3378985187684316967ULL;
    Zobrist::psq[1][31]   = 9696037458963191704ULL;
    Zobrist::psq[1][32]   = 13098241107727776933ULL;
    Zobrist::psq[1][33]   = 16711523013166202616ULL;
    Zobrist::psq[1][34]   = 10079083771611825891ULL;
    Zobrist::psq[1][35]   = 14137347994420603547ULL;
    Zobrist::psq[1][36]   = 4791805899784156187ULL;
    Zobrist::psq[1][37]   = 6078389034317276724ULL;
    Zobrist::psq[1][38]   = 5994547221653596060ULL;
    Zobrist::psq[1][39]   = 16213379374441749196ULL;
    Zobrist::psq[1][40]   = 4600174966381648954ULL;
    Zobrist::psq[1][41]   = 2382793282151591793ULL;
    Zobrist::psq[1][42]   = 5441064086789571698ULL;
    Zobrist::psq[1][43]   = 13211067155709920737ULL;
    Zobrist::psq[1][44]   = 8095577678192451481ULL;
    Zobrist::psq[1][45]   = 12870220845239618167ULL;
    Zobrist::psq[1][46]   = 18366225606586112739ULL;
    Zobrist::psq[1][47]   = 1482740430229529117ULL;
    Zobrist::psq[1][48]   = 18398763828894394702ULL;
    Zobrist::psq[1][49]   = 12894175299039183743ULL;
    Zobrist::psq[1][50]   = 5973205243991449651ULL;
    Zobrist::psq[1][51]   = 16073805277627490771ULL;
    Zobrist::psq[1][52]   = 11840382123049768615ULL;
    Zobrist::psq[1][53]   = 16782637305176790952ULL;
    Zobrist::psq[1][54]   = 16565939816889406374ULL;
    Zobrist::psq[1][55]   = 7611013259146743987ULL;
    Zobrist::psq[1][56]   = 4325631834421711187ULL;
    Zobrist::psq[1][57]   = 7084652077183601842ULL;
    Zobrist::psq[1][58]   = 14113904950837697704ULL;
    Zobrist::psq[1][59]   = 6952439085241219742ULL;
    Zobrist::psq[1][60]   = 11697893679396085013ULL;
    Zobrist::psq[1][61]   = 15932411745698688381ULL;
    Zobrist::psq[1][62]   = 333938476871428781ULL;
    Zobrist::psq[1][63]   = 10094356940478519713ULL;
    Zobrist::psq[2][0]    = 8854028305631117351ULL;
    Zobrist::psq[2][1]    = 18264149368209609558ULL;
    Zobrist::psq[2][2]    = 18152850504025660547ULL;
    Zobrist::psq[2][3]    = 445125824226036916ULL;
    Zobrist::psq[2][4]    = 7445032221575161576ULL;
    Zobrist::psq[2][5]    = 5887372625995221418ULL;
    Zobrist::psq[2][6]    = 12579614965563241976ULL;
    Zobrist::psq[2][7]    = 15542129933905340102ULL;
    Zobrist::psq[2][8]    = 4278411582816540073ULL;
    Zobrist::psq[2][9]    = 7817987688731403418ULL;
    Zobrist::psq[2][10]   = 16765308846548980593ULL;
    Zobrist::psq[2][11]   = 15594655397588023405ULL;
    Zobrist::psq[2][12]   = 11116801254932199266ULL;
    Zobrist::psq[2][13]   = 11592572287770353464ULL;
    Zobrist::psq[2][14]   = 10698558469286656858ULL;
    Zobrist::psq[2][15]   = 263236209937302172ULL;
    Zobrist::psq[2][16]   = 15461982991340303336ULL;
    Zobrist::psq[2][17]   = 3043744698521235658ULL;
    Zobrist::psq[2][18]   = 1070442759222213040ULL;
    Zobrist::psq[2][19]   = 650534245804607543ULL;
    Zobrist::psq[2][20]   = 5943000432800778858ULL;
    Zobrist::psq[2][21]   = 26206987068637543ULL;
    Zobrist::psq[2][22]   = 16737080395141468053ULL;
    Zobrist::psq[2][23]   = 13977415469856941557ULL;
    Zobrist::psq[2][24]   = 1052117838564742180ULL;
    Zobrist::psq[2][25]   = 9424311196719389450ULL;
    Zobrist::psq[2][26]   = 12167498318705983564ULL;
    Zobrist::psq[2][27]   = 4301764225574437137ULL;
    Zobrist::psq[2][28]   = 17360266336634281276ULL;
    Zobrist::psq[2][29]   = 13868884065264943813ULL;
    Zobrist::psq[2][30]   = 15952283905104982306ULL;
    Zobrist::psq[2][31]   = 4998386290424363477ULL;
    Zobrist::psq[2][32]   = 4893239286087369377ULL;
    Zobrist::psq[2][33]   = 17573528852960048629ULL;
    Zobrist::psq[2][34]   = 2412201799238683587ULL;
    Zobrist::psq[2][35]   = 16517545668683925387ULL;
    Zobrist::psq[2][36]   = 16978748896271686395ULL;
    Zobrist::psq[2][37]   = 8830712609912112615ULL;
    Zobrist::psq[2][38]   = 244676446090624528ULL;
    Zobrist::psq[2][39]   = 10801320743593590304ULL;
    Zobrist::psq[2][40]   = 13531918303924845431ULL;
    Zobrist::psq[2][41]   = 10527125009130628070ULL;
    Zobrist::psq[2][42]   = 17495106538955645767ULL;
    Zobrist::psq[2][43]   = 14203433425689676251ULL;
    Zobrist::psq[2][44]   = 13760149572603586785ULL;
    Zobrist::psq[2][45]   = 1273129856199637694ULL;
    Zobrist::psq[2][46]   = 3154213753511759364ULL;
    Zobrist::psq[2][47]   = 12760143787594064657ULL;
    Zobrist::psq[2][48]   = 1600035040276021173ULL;
    Zobrist::psq[2][49]   = 5414819345072334853ULL;
    Zobrist::psq[2][50]   = 7201040945210650872ULL;
    Zobrist::psq[2][51]   = 11015789609492649674ULL;
    Zobrist::psq[2][52]   = 7712150959425383900ULL;
    Zobrist::psq[2][53]   = 8543311100722720016ULL;
    Zobrist::psq[2][54]   = 13076185511676908731ULL;
    Zobrist::psq[2][55]   = 3922562784470822468ULL;
    Zobrist::psq[2][56]   = 2780562387024492132ULL;
    Zobrist::psq[2][57]   = 6697120216501611455ULL;
    Zobrist::psq[2][58]   = 13480343126040452106ULL;
    Zobrist::psq[2][59]   = 12173667680050468927ULL;
    Zobrist::psq[2][60]   = 3302171945877565923ULL;
    Zobrist::psq[2][61]   = 16568602182162993491ULL;
    Zobrist::psq[2][62]   = 14953223006496535120ULL;
    Zobrist::psq[2][63]   = 16457941142416543492ULL;
    Zobrist::psq[3][0]    = 2945262940327718556ULL;
    Zobrist::psq[3][1]    = 3775538624233802005ULL;
    Zobrist::psq[3][2]    = 4292201895252289600ULL;
    Zobrist::psq[3][3]    = 16433809973923446677ULL;
    Zobrist::psq[3][4]    = 1284774014851141252ULL;
    Zobrist::psq[3][5]    = 18314932087213148495ULL;
    Zobrist::psq[3][6]    = 8946796353798605717ULL;
    Zobrist::psq[3][7]    = 16445820069092145103ULL;
    Zobrist::psq[3][8]    = 7588664147775519679ULL;
    Zobrist::psq[3][9]    = 12896594212779880816ULL;
    Zobrist::psq[3][10]   = 14935880823937687725ULL;
    Zobrist::psq[3][11]   = 13400879436137989525ULL;
    Zobrist::psq[3][12]   = 13846969535995712591ULL;
    Zobrist::psq[3][13]   = 12484917729738156524ULL;
    Zobrist::psq[3][14]   = 17882592831712409952ULL;
    Zobrist::psq[3][15]   = 16637473249645425632ULL;
    Zobrist::psq[3][16]   = 15098223454147433904ULL;
    Zobrist::psq[3][17]   = 17631249017957605294ULL;
    Zobrist::psq[3][18]   = 12582001597670293135ULL;
    Zobrist::psq[3][19]   = 17902661106057732664ULL;
    Zobrist::psq[3][20]   = 10274060743048400565ULL;
    Zobrist::psq[3][21]   = 12005958760542442625ULL;
    Zobrist::psq[3][22]   = 6324932172735347303ULL;
    Zobrist::psq[3][23]   = 17192330553585486663ULL;
    Zobrist::psq[3][24]   = 9422872207407330841ULL;
    Zobrist::psq[3][25]   = 3177237980255163711ULL;
    Zobrist::psq[3][26]   = 14998024116488875998ULL;
    Zobrist::psq[3][27]   = 705793604453777656ULL;
    Zobrist::psq[3][28]   = 11327568552142987041ULL;
    Zobrist::psq[3][29]   = 7029368612848231507ULL;
    Zobrist::psq[3][30]   = 11062860980165499825ULL;
    Zobrist::psq[3][31]   = 2900628512702115887ULL;
    Zobrist::psq[3][32]   = 308431256844078091ULL;
    Zobrist::psq[3][33]   = 752802454931337639ULL;
    Zobrist::psq[3][34]   = 5576583881995601144ULL;
    Zobrist::psq[3][35]   = 8733594096989903760ULL;
    Zobrist::psq[3][36]   = 290737499942622970ULL;
    Zobrist::psq[3][37]   = 8992780576699235245ULL;
    Zobrist::psq[3][38]   = 10425616809589311900ULL;
    Zobrist::psq[3][39]   = 5493674620779310265ULL;
    Zobrist::psq[3][40]   = 12589103349525344891ULL;
    Zobrist::psq[3][41]   = 14857852059215963628ULL;
    Zobrist::psq[3][42]   = 13495551423272463104ULL;
    Zobrist::psq[3][43]   = 6944056268429507318ULL;
    Zobrist::psq[3][44]   = 3988842613368812515ULL;
    Zobrist::psq[3][45]   = 14815775969275954512ULL;
    Zobrist::psq[3][46]   = 17868612272134391879ULL;
    Zobrist::psq[3][47]   = 8436706119115607049ULL;
    Zobrist::psq[3][48]   = 7555807622404432493ULL;
    Zobrist::psq[3][49]   = 9144495607954586305ULL;
    Zobrist::psq[3][50]   = 6794801016890317083ULL;
    Zobrist::psq[3][51]   = 6072558259768997948ULL;
    Zobrist::psq[3][52]   = 10941535447546794938ULL;
    Zobrist::psq[3][53]   = 14043502401785556544ULL;
    Zobrist::psq[3][54]   = 8362621443508695308ULL;
    Zobrist::psq[3][55]   = 17736840905212253027ULL;
    Zobrist::psq[3][56]   = 2733031211210449030ULL;
    Zobrist::psq[3][57]   = 4350365705834634871ULL;
    Zobrist::psq[3][58]   = 1100550212031776323ULL;
    Zobrist::psq[3][59]   = 17430963890314521917ULL;
    Zobrist::psq[3][60]   = 7470064030368587841ULL;
    Zobrist::psq[3][61]   = 13387014036020469860ULL;
    Zobrist::psq[3][62]   = 7078824284187344392ULL;
    Zobrist::psq[3][63]   = 12312007608706932222ULL;
    Zobrist::psq[4][0]    = 3826719064958106391ULL;
    Zobrist::psq[4][1]    = 17580452432494632735ULL;
    Zobrist::psq[4][2]    = 4372818848456885156ULL;
    Zobrist::psq[4][3]    = 20778095608392735ULL;
    Zobrist::psq[4][4]    = 9517712183106565981ULL;
    Zobrist::psq[4][5]    = 16772576131911258204ULL;
    Zobrist::psq[4][6]    = 12158847832281029501ULL;
    Zobrist::psq[4][7]    = 18318866654963083744ULL;
    Zobrist::psq[4][8]    = 14355784966049388499ULL;
    Zobrist::psq[4][9]    = 1442237715923966096ULL;
    Zobrist::psq[4][10]   = 16767620159370203923ULL;
    Zobrist::psq[4][11]   = 13501017873225644439ULL;
    Zobrist::psq[4][12]   = 12414460951753850741ULL;
    Zobrist::psq[4][13]   = 1630390626826320339ULL;
    Zobrist::psq[4][14]   = 11056926288496765292ULL;
    Zobrist::psq[4][15]   = 17514919132679636196ULL;
    Zobrist::psq[4][16]   = 6737125905271376420ULL;
    Zobrist::psq[4][17]   = 3156370395448333753ULL;
    Zobrist::psq[4][18]   = 7372374977020439436ULL;
    Zobrist::psq[4][19]   = 5277883516136612451ULL;
    Zobrist::psq[4][20]   = 16544956564115640970ULL;
    Zobrist::psq[4][21]   = 14431129579433994133ULL;
    Zobrist::psq[4][22]   = 10776067565185448ULL;
    Zobrist::psq[4][23]   = 15235680854177679657ULL;
    Zobrist::psq[4][24]   = 12767627681826077225ULL;
    Zobrist::psq[4][25]   = 1324675096273909386ULL;
    Zobrist::psq[4][26]   = 3456463189867507715ULL;
    Zobrist::psq[4][27]   = 9195964142578403484ULL;
    Zobrist::psq[4][28]   = 10627443539470127577ULL;
    Zobrist::psq[4][29]   = 7083655917886846512ULL;
    Zobrist::psq[4][30]   = 14734414825071094346ULL;
    Zobrist::psq[4][31]   = 8833975264052769557ULL;
    Zobrist::psq[4][32]   = 2965232458494052289ULL;
    Zobrist::psq[4][33]   = 12786367183060552144ULL;
    Zobrist::psq[4][34]   = 6364751811635930008ULL;
    Zobrist::psq[4][35]   = 12304694438192434386ULL;
    Zobrist::psq[4][36]   = 4420057912710567321ULL;
    Zobrist::psq[4][37]   = 13121826629733594974ULL;
    Zobrist::psq[4][38]   = 3295424378969736960ULL;
    Zobrist::psq[4][39]   = 16543444358261923928ULL;
    Zobrist::psq[4][40]   = 13665696745413941685ULL;
    Zobrist::psq[4][41]   = 3585618043384929225ULL;
    Zobrist::psq[4][42]   = 14758422515963078108ULL;
    Zobrist::psq[4][43]   = 5444185746065710993ULL;
    Zobrist::psq[4][44]   = 6217807121864929894ULL;
    Zobrist::psq[4][45]   = 7617121805124236390ULL;
    Zobrist::psq[4][46]   = 2176332518208481987ULL;
    Zobrist::psq[4][47]   = 1435617355844826626ULL;
    Zobrist::psq[4][48]   = 17897291909516933347ULL;
    Zobrist::psq[4][49]   = 17430612766366810879ULL;
    Zobrist::psq[4][50]   = 13845907184570465897ULL;
    Zobrist::psq[4][51]   = 3432307431600566936ULL;
    Zobrist::psq[4][52]   = 2532253559171451888ULL;
    Zobrist::psq[4][53]   = 11643128737472459646ULL;
    Zobrist::psq[4][54]   = 13606171979107604790ULL;
    Zobrist::psq[4][55]   = 10012509558550373270ULL;
    Zobrist::psq[4][56]   = 5587706015190365982ULL;
    Zobrist::psq[4][57]   = 18189230678861289336ULL;
    Zobrist::psq[4][58]   = 5637318834313874969ULL;
    Zobrist::psq[4][59]   = 4728172345191419793ULL;
    Zobrist::psq[4][60]   = 13287099661014164329ULL;
    Zobrist::psq[4][61]   = 8475766932330124954ULL;
    Zobrist::psq[4][62]   = 2781312650135424674ULL;
    Zobrist::psq[4][63]   = 10552294945874175633ULL;
    Zobrist::psq[5][0]    = 14116194119706301666ULL;
    Zobrist::psq[5][1]    = 908994258594572803ULL;
    Zobrist::psq[5][2]    = 3835251526534030662ULL;
    Zobrist::psq[5][3]    = 3902806174142003247ULL;
    Zobrist::psq[5][4]    = 8404113168045990162ULL;
    Zobrist::psq[5][5]    = 10605456791970677788ULL;
    Zobrist::psq[5][6]    = 8371724936653327204ULL;
    Zobrist::psq[5][7]    = 10149265301602815302ULL;
    Zobrist::psq[5][8]    = 10280163375965480302ULL;
    Zobrist::psq[5][9]    = 12878458563073396434ULL;
    Zobrist::psq[5][10]   = 1480273033205949154ULL;
    Zobrist::psq[5][11]   = 15420639285122262859ULL;
    Zobrist::psq[5][12]   = 16040433549230388361ULL;
    Zobrist::psq[5][13]   = 10889445127567090568ULL;
    Zobrist::psq[5][14]   = 7154846977618541400ULL;
    Zobrist::psq[5][15]   = 15324267473561911299ULL;
    Zobrist::psq[5][16]   = 9123044315927273855ULL;
    Zobrist::psq[5][17]   = 18178395620988860923ULL;
    Zobrist::psq[5][18]   = 13937825686985326355ULL;
    Zobrist::psq[5][19]   = 6208640256728026680ULL;
    Zobrist::psq[5][20]   = 17803354189602776349ULL;
    Zobrist::psq[5][21]   = 8168466387959732965ULL;
    Zobrist::psq[5][22]   = 4747388335999020093ULL;
    Zobrist::psq[5][23]   = 8076893647775627477ULL;
    Zobrist::psq[5][24]   = 135355862477779318ULL;
    Zobrist::psq[5][25]   = 13727020784074293322ULL;
    Zobrist::psq[5][26]   = 16471001867829363208ULL;
    Zobrist::psq[5][27]   = 3944848361583366045ULL;
    Zobrist::psq[5][28]   = 6153835027004876065ULL;
    Zobrist::psq[5][29]   = 17541053953916494135ULL;
    Zobrist::psq[5][30]   = 830442639195732299ULL;
    Zobrist::psq[5][31]   = 5707759661195251524ULL;
    Zobrist::psq[5][32]   = 16745928189385382169ULL;
    Zobrist::psq[5][33]   = 13853872449862111272ULL;
    Zobrist::psq[5][34]   = 10763276423780512808ULL;
    Zobrist::psq[5][35]   = 528748578239178413ULL;
    Zobrist::psq[5][36]   = 1195366693239264477ULL;
    Zobrist::psq[5][37]   = 16072813688416096526ULL;
    Zobrist::psq[5][38]   = 9411878730995839744ULL;
    Zobrist::psq[5][39]   = 14250860229846220116ULL;
    Zobrist::psq[5][40]   = 3391112600086567492ULL;
    Zobrist::psq[5][41]   = 11283764167692931512ULL;
    Zobrist::psq[5][42]   = 1672248607577385754ULL;
    Zobrist::psq[5][43]   = 2130286739811077583ULL;
    Zobrist::psq[5][44]   = 18311727561747759139ULL;
    Zobrist::psq[5][45]   = 974583822133342724ULL;
    Zobrist::psq[5][46]   = 5061116103402273638ULL;
    Zobrist::psq[5][47]   = 3126855720952116346ULL;
    Zobrist::psq[5][48]   = 578870949780164607ULL;
    Zobrist::psq[5][49]   = 3776778176701636327ULL;
    Zobrist::psq[5][50]   = 14213795876687685078ULL;
    Zobrist::psq[5][51]   = 5613780124034108946ULL;
    Zobrist::psq[5][52]   = 6069741268072432820ULL;
    Zobrist::psq[5][53]   = 8893641350514130178ULL;
    Zobrist::psq[5][54]   = 15249957078178864452ULL;
    Zobrist::psq[5][55]   = 18092583129505773527ULL;
    Zobrist::psq[5][56]   = 11393903435307203091ULL;
    Zobrist::psq[5][57]   = 8119660695860781220ULL;
    Zobrist::psq[5][58]   = 13766130452052543028ULL;
    Zobrist::psq[5][59]   = 7096579372531132405ULL;
    Zobrist::psq[5][60]   = 7459026647266724422ULL;
    Zobrist::psq[5][61]   = 5897616920394564481ULL;
    Zobrist::psq[5][62]   = 4162427946331299898ULL;
    Zobrist::psq[5][63]   = 2527789185948800525ULL;
    Zobrist::psq[6][0]    = 17290988795360054066ULL;
    Zobrist::psq[6][1]    = 5240905960030703813ULL;
    Zobrist::psq[6][2]    = 12532957579127022568ULL;
    Zobrist::psq[6][3]    = 7321214839249116978ULL;
    Zobrist::psq[6][4]    = 17188130528816882357ULL;
    Zobrist::psq[6][5]    = 13649660060729335176ULL;
    Zobrist::psq[6][6]    = 7877670809777050873ULL;
    Zobrist::psq[6][7]    = 8603165736220767331ULL;
    Zobrist::psq[6][8]    = 3731409983944574110ULL;
    Zobrist::psq[6][9]    = 14311591814980160037ULL;
    Zobrist::psq[6][10]   = 16719365103710912831ULL;
    Zobrist::psq[6][11]   = 15645061390881301878ULL;
    Zobrist::psq[6][12]   = 15313601992567477463ULL;
    Zobrist::psq[6][13]   = 558437165307320475ULL;
    Zobrist::psq[6][14]   = 10107592147679710958ULL;
    Zobrist::psq[6][15]   = 217058993405149273ULL;
    Zobrist::psq[6][16]   = 11583857652496458642ULL;
    Zobrist::psq[6][17]   = 12813267508475749642ULL;
    Zobrist::psq[6][18]   = 12801463184548517903ULL;
    Zobrist::psq[6][19]   = 10205205656182355892ULL;
    Zobrist::psq[6][20]   = 12009517757124415757ULL;
    Zobrist::psq[6][21]   = 11711220569788417590ULL;
    Zobrist::psq[6][22]   = 601506575385147719ULL;
    Zobrist::psq[6][23]   = 2403800598476663693ULL;
    Zobrist::psq[6][24]   = 3185273191806365666ULL;
    Zobrist::psq[6][25]   = 16311384682203900813ULL;
    Zobrist::psq[6][26]   = 2147738008043402447ULL;
    Zobrist::psq[6][27]   = 11784653004849107439ULL;
    Zobrist::psq[6][28]   = 11363702615030984814ULL;
    Zobrist::psq[6][29]   = 4459820841160151625ULL;
    Zobrist::psq[6][30]   = 17238855191434604666ULL;
    Zobrist::psq[6][31]   = 16533107622905015899ULL;
    Zobrist::psq[6][32]   = 12580437090734268666ULL;
    Zobrist::psq[6][33]   = 9002238121826321187ULL;
    Zobrist::psq[6][34]   = 7209727037264965188ULL;
    Zobrist::psq[6][35]   = 15210303941751662984ULL;
    Zobrist::psq[6][36]   = 5957580827072516578ULL;
    Zobrist::psq[6][37]   = 16077971979351817631ULL;
    Zobrist::psq[6][38]   = 7451935491114626499ULL;
    Zobrist::psq[6][39]   = 14243752318712699139ULL;
    Zobrist::psq[6][40]   = 12737894796843349185ULL;
    Zobrist::psq[6][41]   = 1351996896321498360ULL;
    Zobrist::psq[6][42]   = 4395539424431256646ULL;
    Zobrist::psq[6][43]   = 14636926406778905296ULL;
    Zobrist::psq[6][44]   = 10637364485216545239ULL;
    Zobrist::psq[6][45]   = 4709900282812548306ULL;
    Zobrist::psq[6][46]   = 14703591130731831913ULL;
    Zobrist::psq[6][47]   = 1476367765688281237ULL;
    Zobrist::psq[6][48]   = 4113914727206496161ULL;
    Zobrist::psq[6][49]   = 8066049843497142643ULL;
    Zobrist::psq[6][50]   = 7809561412546830570ULL;
    Zobrist::psq[6][51]   = 4879538739185105394ULL;
    Zobrist::psq[6][52]   = 9498083046807871856ULL;
    Zobrist::psq[6][53]   = 17559505952950827343ULL;
    Zobrist::psq[6][54]   = 11763387757765891631ULL;
    Zobrist::psq[6][55]   = 10055035698587107604ULL;
    Zobrist::psq[6][56]   = 12844734664424373030ULL;
    Zobrist::psq[6][57]   = 330991544207939447ULL;
    Zobrist::psq[6][58]   = 8508732305896661743ULL;
    Zobrist::psq[6][59]   = 11153570973223855023ULL;
    Zobrist::psq[6][60]   = 10238055872248257461ULL;
    Zobrist::psq[6][61]   = 1773280948989896239ULL;
    Zobrist::psq[6][62]   = 8300833427522849187ULL;
    Zobrist::psq[6][63]   = 10832779467616436194ULL;
    Zobrist::psq[9][0]    = 11781789245711860189ULL;
    Zobrist::psq[9][1]    = 2747882707407274161ULL;
    Zobrist::psq[9][2]    = 3724767368808293169ULL;
    Zobrist::psq[9][3]    = 10298180063630105197ULL;
    Zobrist::psq[9][4]    = 10746438658164496957ULL;
    Zobrist::psq[9][5]    = 16037040440297371558ULL;
    Zobrist::psq[9][6]    = 17588125462232966688ULL;
    Zobrist::psq[9][7]    = 6880843334474042246ULL;
    Zobrist::psq[9][8]    = 560415017990002212ULL;
    Zobrist::psq[9][9]    = 6626394159937994533ULL;
    Zobrist::psq[9][10]   = 2670333323665803600ULL;
    Zobrist::psq[9][11]   = 4280458366389177326ULL;
    Zobrist::psq[9][12]   = 1467978672011198404ULL;
    Zobrist::psq[9][13]   = 7620133404071416883ULL;
    Zobrist::psq[9][14]   = 13350367343504972530ULL;
    Zobrist::psq[9][15]   = 10138430730509076413ULL;
    Zobrist::psq[9][16]   = 6785953884329063615ULL;
    Zobrist::psq[9][17]   = 4006903721835701728ULL;
    Zobrist::psq[9][18]   = 17529175408771439641ULL;
    Zobrist::psq[9][19]   = 2257868868401674686ULL;
    Zobrist::psq[9][20]   = 16350586259217027048ULL;
    Zobrist::psq[9][21]   = 12792669610269240489ULL;
    Zobrist::psq[9][22]   = 15445432911128260212ULL;
    Zobrist::psq[9][23]   = 3830919760132254685ULL;
    Zobrist::psq[9][24]   = 17463139367032047470ULL;
    Zobrist::psq[9][25]   = 15002266175994648649ULL;
    Zobrist::psq[9][26]   = 17680514289072042202ULL;
    Zobrist::psq[9][27]   = 362761448860517629ULL;
    Zobrist::psq[9][28]   = 2620716836644167551ULL;
    Zobrist::psq[9][29]   = 10876826577342073644ULL;
    Zobrist::psq[9][30]   = 14704635783604247913ULL;
    Zobrist::psq[9][31]   = 8370308497378149181ULL;
    Zobrist::psq[9][32]   = 16902199073103511157ULL;
    Zobrist::psq[9][33]   = 4712050710770633961ULL;
    Zobrist::psq[9][34]   = 2335277171236964126ULL;
    Zobrist::psq[9][35]   = 15454330651988402294ULL;
    Zobrist::psq[9][36]   = 6039398895644425870ULL;
    Zobrist::psq[9][37]   = 5330935207425949713ULL;
    Zobrist::psq[9][38]   = 6844204079868621004ULL;
    Zobrist::psq[9][39]   = 15018633515897982115ULL;
    Zobrist::psq[9][40]   = 5869887878873962697ULL;
    Zobrist::psq[9][41]   = 9619421978703093664ULL;
    Zobrist::psq[9][42]   = 7065039212033014872ULL;
    Zobrist::psq[9][43]   = 14085021312833583897ULL;
    Zobrist::psq[9][44]   = 17738639966636660046ULL;
    Zobrist::psq[9][45]   = 18274309123980813514ULL;
    Zobrist::psq[9][46]   = 16007640215959475868ULL;
    Zobrist::psq[9][47]   = 4326793000252505639ULL;
    Zobrist::psq[9][48]   = 11694193434453531305ULL;
    Zobrist::psq[9][49]   = 15789397716808962025ULL;
    Zobrist::psq[9][50]   = 8672273831614123897ULL;
    Zobrist::psq[9][51]   = 6109915657282875177ULL;
    Zobrist::psq[9][52]   = 6240221177136276484ULL;
    Zobrist::psq[9][53]   = 17650760467278016265ULL;
    Zobrist::psq[9][54]   = 13635783915766085055ULL;
    Zobrist::psq[9][55]   = 17178975703249397658ULL;
    Zobrist::psq[9][56]   = 690100752037560272ULL;
    Zobrist::psq[9][57]   = 846594232046156050ULL;
    Zobrist::psq[9][58]   = 11437611220054444781ULL;
    Zobrist::psq[9][59]   = 1050411833588837386ULL;
    Zobrist::psq[9][60]   = 10485589741397417446ULL;
    Zobrist::psq[9][61]   = 12844414679888429939ULL;
    Zobrist::psq[9][62]   = 6491358656106542835ULL;
    Zobrist::psq[9][63]   = 12575464921310399912ULL;
    Zobrist::psq[10][0]   = 14923825269739949453ULL;
    Zobrist::psq[10][1]   = 18375002115249413557ULL;
    Zobrist::psq[10][2]   = 3423036550911737589ULL;
    Zobrist::psq[10][3]   = 15250861506191355802ULL;
    Zobrist::psq[10][4]   = 15031961129285356212ULL;
    Zobrist::psq[10][5]   = 15435012606837965840ULL;
    Zobrist::psq[10][6]   = 6304673951675292305ULL;
    Zobrist::psq[10][7]   = 12785716655315370815ULL;
    Zobrist::psq[10][8]   = 9808873325341612945ULL;
    Zobrist::psq[10][9]   = 9783992785966697331ULL;
    Zobrist::psq[10][10]  = 18138650430907468530ULL;
    Zobrist::psq[10][11]  = 18431297401347671031ULL;
    Zobrist::psq[10][12]  = 18148129570815566817ULL;
    Zobrist::psq[10][13]  = 12696743950740820713ULL;
    Zobrist::psq[10][14]  = 1854845205476015706ULL;
    Zobrist::psq[10][15]  = 12865777516920439176ULL;
    Zobrist::psq[10][16]  = 15636159047245426328ULL;
    Zobrist::psq[10][17]  = 17373407353156678628ULL;
    Zobrist::psq[10][18]  = 2495834645782650553ULL;
    Zobrist::psq[10][19]  = 11247757644603045972ULL;
    Zobrist::psq[10][20]  = 17130748698210142189ULL;
    Zobrist::psq[10][21]  = 11422966446976074719ULL;
    Zobrist::psq[10][22]  = 1595016003613213710ULL;
    Zobrist::psq[10][23]  = 3899856913033553150ULL;
    Zobrist::psq[10][24]  = 15470414105568996654ULL;
    Zobrist::psq[10][25]  = 2572459120480840982ULL;
    Zobrist::psq[10][26]  = 14288318049370965601ULL;
    Zobrist::psq[10][27]  = 4034656711994978492ULL;
    Zobrist::psq[10][28]  = 3619462250265206907ULL;
    Zobrist::psq[10][29]  = 12564616267900212223ULL;
    Zobrist::psq[10][30]  = 6563888989859451823ULL;
    Zobrist::psq[10][31]  = 2454157599688795602ULL;
    Zobrist::psq[10][32]  = 122761158351497116ULL;
    Zobrist::psq[10][33]  = 4118064480546384385ULL;
    Zobrist::psq[10][34]  = 13825342760651713002ULL;
    Zobrist::psq[10][35]  = 3757958894065091138ULL;
    Zobrist::psq[10][36]  = 3348351562535718824ULL;
    Zobrist::psq[10][37]  = 11085064257829065607ULL;
    Zobrist::psq[10][38]  = 4791949565677098244ULL;
    Zobrist::psq[10][39]  = 16741859899153424134ULL;
    Zobrist::psq[10][40]  = 13552228277894027114ULL;
    Zobrist::psq[10][41]  = 18043793947072687525ULL;
    Zobrist::psq[10][42]  = 18232133385309552782ULL;
    Zobrist::psq[10][43]  = 17162542170033385071ULL;
    Zobrist::psq[10][44]  = 17966719644677930276ULL;
    Zobrist::psq[10][45]  = 4126374944389900134ULL;
    Zobrist::psq[10][46]  = 7694029693525104626ULL;
    Zobrist::psq[10][47]  = 7844796758498075948ULL;
    Zobrist::psq[10][48]  = 15171322352384637386ULL;
    Zobrist::psq[10][49]  = 4901284706517591019ULL;
    Zobrist::psq[10][50]  = 11550611493505829690ULL;
    Zobrist::psq[10][51]  = 8591758722916550176ULL;
    Zobrist::psq[10][52]  = 6614280899913466481ULL;
    Zobrist::psq[10][53]  = 15659292666557594854ULL;
    Zobrist::psq[10][54]  = 8334845918197067198ULL;
    Zobrist::psq[10][55]  = 14303347218899317731ULL;
    Zobrist::psq[10][56]  = 18185681713739197231ULL;
    Zobrist::psq[10][57]  = 10010957749676186008ULL;
    Zobrist::psq[10][58]  = 6151588837035247399ULL;
    Zobrist::psq[10][59]  = 15955998980864570780ULL;
    Zobrist::psq[10][60]  = 14725804664707294906ULL;
    Zobrist::psq[10][61]  = 9071111217904025772ULL;
    Zobrist::psq[10][62]  = 4268551186589045976ULL;
    Zobrist::psq[10][63]  = 3787505694838293655ULL;
    Zobrist::psq[11][0]   = 3463765996898474975ULL;
    Zobrist::psq[11][1]   = 1419043948633899671ULL;
    Zobrist::psq[11][2]   = 4738255775972431200ULL;
    Zobrist::psq[11][3]   = 10880687006345860054ULL;
    Zobrist::psq[11][4]   = 6083956890523873398ULL;
    Zobrist::psq[11][5]   = 15399367780949709721ULL;
    Zobrist::psq[11][6]   = 10077652868536637496ULL;
    Zobrist::psq[11][7]   = 4763774200646997281ULL;
    Zobrist::psq[11][8]   = 2058719554631509711ULL;
    Zobrist::psq[11][9]   = 16245257579300202929ULL;
    Zobrist::psq[11][10]  = 12549234361408101229ULL;
    Zobrist::psq[11][11]  = 5132111825598353706ULL;
    Zobrist::psq[11][12]  = 13210867931726967807ULL;
    Zobrist::psq[11][13]  = 8049587883156206974ULL;
    Zobrist::psq[11][14]  = 14208790774466773366ULL;
    Zobrist::psq[11][15]  = 15004789243215417478ULL;
    Zobrist::psq[11][16]  = 2705161721287640173ULL;
    Zobrist::psq[11][17]  = 6606951690346399114ULL;
    Zobrist::psq[11][18]  = 9038858141657157738ULL;
    Zobrist::psq[11][19]  = 9864507686211087503ULL;
    Zobrist::psq[11][20]  = 8174211780307618304ULL;
    Zobrist::psq[11][21]  = 16060351410629081351ULL;
    Zobrist::psq[11][22]  = 5484951598904056885ULL;
    Zobrist::psq[11][23]  = 12456759525904287919ULL;
    Zobrist::psq[11][24]  = 8919252620379965524ULL;
    Zobrist::psq[11][25]  = 15501107657356591656ULL;
    Zobrist::psq[11][26]  = 3242949188225361282ULL;
    Zobrist::psq[11][27]  = 5926058172544675863ULL;
    Zobrist::psq[11][28]  = 6405123151097452666ULL;
    Zobrist::psq[11][29]  = 172567736958909523ULL;
    Zobrist::psq[11][30]  = 17292315564005737229ULL;
    Zobrist::psq[11][31]  = 13464278685013338817ULL;
    Zobrist::psq[11][32]  = 3686053955562449182ULL;
    Zobrist::psq[11][33]  = 8857017014241158725ULL;
    Zobrist::psq[11][34]  = 15421895718306499875ULL;
    Zobrist::psq[11][35]  = 3815913251318905694ULL;
    Zobrist::psq[11][36]  = 3432648465599995302ULL;
    Zobrist::psq[11][37]  = 818320788389300537ULL;
    Zobrist::psq[11][38]  = 4071520112108071604ULL;
    Zobrist::psq[11][39]  = 13295466432639272442ULL;
    Zobrist::psq[11][40]  = 2426572569594491679ULL;
    Zobrist::psq[11][41]  = 10076303268977391406ULL;
    Zobrist::psq[11][42]  = 8784192232334006419ULL;
    Zobrist::psq[11][43]  = 2997181738853009670ULL;
    Zobrist::psq[11][44]  = 15770398685934330580ULL;
    Zobrist::psq[11][45]  = 13017264784195056557ULL;
    Zobrist::psq[11][46]  = 4330776497582490757ULL;
    Zobrist::psq[11][47]  = 10934498588458332802ULL;
    Zobrist::psq[11][48]  = 10356579632341837397ULL;
    Zobrist::psq[11][49]  = 2098241031318749487ULL;
    Zobrist::psq[11][50]  = 14789448409803449028ULL;
    Zobrist::psq[11][51]  = 11251433970760721438ULL;
    Zobrist::psq[11][52]  = 7224004101031043677ULL;
    Zobrist::psq[11][53]  = 15038935143876354117ULL;
    Zobrist::psq[11][54]  = 13215483265469582733ULL;
    Zobrist::psq[11][55]  = 1462298635979286935ULL;
    Zobrist::psq[11][56]  = 5759284467508932139ULL;
    Zobrist::psq[11][57]  = 5761810302276021825ULL;
    Zobrist::psq[11][58]  = 1946852319481058342ULL;
    Zobrist::psq[11][59]  = 8779292626819401953ULL;
    Zobrist::psq[11][60]  = 9980275774854520963ULL;
    Zobrist::psq[11][61]  = 9018156077605645253ULL;
    Zobrist::psq[11][62]  = 10175632970326281074ULL;
    Zobrist::psq[11][63]  = 17670251009423356428ULL;
    Zobrist::psq[12][0]   = 2047473063754745880ULL;
    Zobrist::psq[12][1]   = 4129462703004022451ULL;
    Zobrist::psq[12][2]   = 10030514736718131075ULL;
    Zobrist::psq[12][3]   = 8457187454173219884ULL;
    Zobrist::psq[12][4]   = 675824455430313366ULL;
    Zobrist::psq[12][5]   = 15722708499135010396ULL;
    Zobrist::psq[12][6]   = 1416150021210949828ULL;
    Zobrist::psq[12][7]   = 18340753630988628266ULL;
    Zobrist::psq[12][8]   = 4279562020148953383ULL;
    Zobrist::psq[12][9]   = 7599717795808621650ULL;
    Zobrist::psq[12][10]  = 8493385059263161629ULL;
    Zobrist::psq[12][11]  = 5448373608430482181ULL;
    Zobrist::psq[12][12]  = 7975000343659144004ULL;
    Zobrist::psq[12][13]  = 3661443877569162353ULL;
    Zobrist::psq[12][14]  = 17436434418308603210ULL;
    Zobrist::psq[12][15]  = 7723061412912586436ULL;
    Zobrist::psq[12][16]  = 12478269109366344372ULL;
    Zobrist::psq[12][17]  = 5260527761162561230ULL;
    Zobrist::psq[12][18]  = 3664808336308943032ULL;
    Zobrist::psq[12][19]  = 12246522629121956498ULL;
    Zobrist::psq[12][20]  = 11421384233946319246ULL;
    Zobrist::psq[12][21]  = 10711232448204740396ULL;
    Zobrist::psq[12][22]  = 394033332107778027ULL;
    Zobrist::psq[12][23]  = 1653867462011650260ULL;
    Zobrist::psq[12][24]  = 10614247855083729040ULL;
    Zobrist::psq[12][25]  = 3511207051989217747ULL;
    Zobrist::psq[12][26]  = 14828688729293007936ULL;
    Zobrist::psq[12][27]  = 12730238737606105501ULL;
    Zobrist::psq[12][28]  = 9131161340116597330ULL;
    Zobrist::psq[12][29]  = 10475424158865388660ULL;
    Zobrist::psq[12][30]  = 12216784836515690585ULL;
    Zobrist::psq[12][31]  = 12605719261947498045ULL;
    Zobrist::psq[12][32]  = 55059904350528673ULL;
    Zobrist::psq[12][33]  = 5668017292185949458ULL;
    Zobrist::psq[12][34]  = 5318848626170854652ULL;
    Zobrist::psq[12][35]  = 5812165408168894719ULL;
    Zobrist::psq[12][36]  = 12436591089168384586ULL;
    Zobrist::psq[12][37]  = 11456184110470635333ULL;
    Zobrist::psq[12][38]  = 17354703890556504985ULL;
    Zobrist::psq[12][39]  = 12819708191444916183ULL;
    Zobrist::psq[12][40]  = 2051969874001439467ULL;
    Zobrist::psq[12][41]  = 9752086654524583546ULL;
    Zobrist::psq[12][42]  = 8598830537031500033ULL;
    Zobrist::psq[12][43]  = 10803717843971298140ULL;
    Zobrist::psq[12][44]  = 17386254373003795027ULL;
    Zobrist::psq[12][45]  = 3490013643061567317ULL;
    Zobrist::psq[12][46]  = 14966160920336416174ULL;
    Zobrist::psq[12][47]  = 2716159408585464742ULL;
    Zobrist::psq[12][48]  = 13704057180721116715ULL;
    Zobrist::psq[12][49]  = 6139827121406310950ULL;
    Zobrist::psq[12][50]  = 12045645008689575811ULL;
    Zobrist::psq[12][51]  = 5879666907986225363ULL;
    Zobrist::psq[12][52]  = 18332108852121545326ULL;
    Zobrist::psq[12][53]  = 8302596541641486393ULL;
    Zobrist::psq[12][54]  = 3337300269606353125ULL;
    Zobrist::psq[12][55]  = 4641043901128821440ULL;
    Zobrist::psq[12][56]  = 17552658021160699704ULL;
    Zobrist::psq[12][57]  = 15245517114959849830ULL;
    Zobrist::psq[12][58]  = 898774234328201642ULL;
    Zobrist::psq[12][59]  = 13458365488972458856ULL;
    Zobrist::psq[12][60]  = 17617352963801145870ULL;
    Zobrist::psq[12][61]  = 12653043169047643133ULL;
    Zobrist::psq[12][62]  = 3946055118622982785ULL;
    Zobrist::psq[12][63]  = 78667567517654999ULL;
    Zobrist::psq[13][0]   = 7496345100749090134ULL;
    Zobrist::psq[13][1]   = 11141138397664383499ULL;
    Zobrist::psq[13][2]   = 9990861652354760086ULL;
    Zobrist::psq[13][3]   = 6136051413974204120ULL;
    Zobrist::psq[13][4]   = 14382251659553821084ULL;
    Zobrist::psq[13][5]   = 12222838175704680581ULL;
    Zobrist::psq[13][6]   = 9437743647758681312ULL;
    Zobrist::psq[13][7]   = 5321952072316248116ULL;
    Zobrist::psq[13][8]   = 9510472571572253025ULL;
    Zobrist::psq[13][9]   = 13968738580144591953ULL;
    Zobrist::psq[13][10]  = 9048732621241245672ULL;
    Zobrist::psq[13][11]  = 7070992119077796289ULL;
    Zobrist::psq[13][12]  = 7585987196905721881ULL;
    Zobrist::psq[13][13]  = 12797609451470009512ULL;
    Zobrist::psq[13][14]  = 13831169997283951441ULL;
    Zobrist::psq[13][15]  = 14062956797276305407ULL;
    Zobrist::psq[13][16]  = 7195172102806297836ULL;
    Zobrist::psq[13][17]  = 13763135782447679404ULL;
    Zobrist::psq[13][18]  = 8729177333120200902ULL;
    Zobrist::psq[13][19]  = 8228513033455726756ULL;
    Zobrist::psq[13][20]  = 5827889096510108059ULL;
    Zobrist::psq[13][21]  = 1541817158620711182ULL;
    Zobrist::psq[13][22]  = 18002525473269359251ULL;
    Zobrist::psq[13][23]  = 7210349805272776282ULL;
    Zobrist::psq[13][24]  = 6760744891923215431ULL;
    Zobrist::psq[13][25]  = 1684012349959865632ULL;
    Zobrist::psq[13][26]  = 5422658641223860702ULL;
    Zobrist::psq[13][27]  = 5964630753289401637ULL;
    Zobrist::psq[13][28]  = 16048931659747747714ULL;
    Zobrist::psq[13][29]  = 12995369105282084360ULL;
    Zobrist::psq[13][30]  = 2210225853011473806ULL;
    Zobrist::psq[13][31]  = 13310794355402477849ULL;
    Zobrist::psq[13][32]  = 4356361331354780175ULL;
    Zobrist::psq[13][33]  = 10920940233470324174ULL;
    Zobrist::psq[13][34]  = 4480682637160025854ULL;
    Zobrist::psq[13][35]  = 11920920861864075275ULL;
    Zobrist::psq[13][36]  = 17830720560385394644ULL;
    Zobrist::psq[13][37]  = 17667812763781863653ULL;
    Zobrist::psq[13][38]  = 8584251371203620679ULL;
    Zobrist::psq[13][39]  = 10083927648945854194ULL;
    Zobrist::psq[13][40]  = 15175717840117055506ULL;
    Zobrist::psq[13][41]  = 3402388332801799152ULL;
    Zobrist::psq[13][42]  = 17983756367024412696ULL;
    Zobrist::psq[13][43]  = 13633521765968038314ULL;
    Zobrist::psq[13][44]  = 18197623828188242686ULL;
    Zobrist::psq[13][45]  = 7159151014196207335ULL;
    Zobrist::psq[13][46]  = 6329323109608928752ULL;
    Zobrist::psq[13][47]  = 4596348075478973761ULL;
    Zobrist::psq[13][48]  = 1929043772203993371ULL;
    Zobrist::psq[13][49]  = 2942782730029388844ULL;
    Zobrist::psq[13][50]  = 17616535832761962408ULL;
    Zobrist::psq[13][51]  = 14638746212880920282ULL;
    Zobrist::psq[13][52]  = 235408037287298392ULL;
    Zobrist::psq[13][53]  = 15488773953079788133ULL;
    Zobrist::psq[13][54]  = 14511691540381881087ULL;
    Zobrist::psq[13][55]  = 4908241668947178463ULL;
    Zobrist::psq[13][56]  = 8002325218109467205ULL;
    Zobrist::psq[13][57]  = 384694259305835297ULL;
    Zobrist::psq[13][58]  = 4413022859932656147ULL;
    Zobrist::psq[13][59]  = 16084510603130945976ULL;
    Zobrist::psq[13][60]  = 7817184652260023923ULL;
    Zobrist::psq[13][61]  = 11521163704900182019ULL;
    Zobrist::psq[13][62]  = 10633473972031941012ULL;
    Zobrist::psq[13][63]  = 7028123206539359005ULL;
    Zobrist::psq[14][0]   = 12370129909167185711ULL;
    Zobrist::psq[14][1]   = 18282545875249343957ULL;
    Zobrist::psq[14][2]   = 11571910781648655955ULL;
    Zobrist::psq[14][3]   = 12044362528788437371ULL;
    Zobrist::psq[14][4]   = 15748959137105604538ULL;
    Zobrist::psq[14][5]   = 12433669315838447795ULL;
    Zobrist::psq[14][6]   = 3539341563356477798ULL;
    Zobrist::psq[14][7]   = 8229636981602574987ULL;
    Zobrist::psq[14][8]   = 18267920850505015981ULL;
    Zobrist::psq[14][9]   = 18135187956959905864ULL;
    Zobrist::psq[14][10]  = 10122403804874825725ULL;
    Zobrist::psq[14][11]  = 8577640427585662579ULL;
    Zobrist::psq[14][12]  = 16947872026033056961ULL;
    Zobrist::psq[14][13]  = 4498886674923994328ULL;
    Zobrist::psq[14][14]  = 5110446196942225801ULL;
    Zobrist::psq[14][15]  = 2443501881669395127ULL;
    Zobrist::psq[14][16]  = 6915148508579620831ULL;
    Zobrist::psq[14][17]  = 9154422921438056207ULL;
    Zobrist::psq[14][18]  = 3578030806440286511ULL;
    Zobrist::psq[14][19]  = 15315801991440539300ULL;
    Zobrist::psq[14][20]  = 7070866824836391168ULL;
    Zobrist::psq[14][21]  = 14817924832942381111ULL;
    Zobrist::psq[14][22]  = 3001446271118775643ULL;
    Zobrist::psq[14][23]  = 13000642695841600636ULL;
    Zobrist::psq[14][24]  = 14370567463871457833ULL;
    Zobrist::psq[14][25]  = 11030064684553339453ULL;
    Zobrist::psq[14][26]  = 14239970918075645415ULL;
    Zobrist::psq[14][27]  = 9415971121016597759ULL;
    Zobrist::psq[14][28]  = 6665243610733579451ULL;
    Zobrist::psq[14][29]  = 12729882327349519727ULL;
    Zobrist::psq[14][30]  = 127495542892799647ULL;
    Zobrist::psq[14][31]  = 6044073010763988256ULL;
    Zobrist::psq[14][32]  = 13007064564721953048ULL;
    Zobrist::psq[14][33]  = 13888665226332397302ULL;
    Zobrist::psq[14][34]  = 13536486134713258398ULL;
    Zobrist::psq[14][35]  = 16493663995181111698ULL;
    Zobrist::psq[14][36]  = 2130152061385863810ULL;
    Zobrist::psq[14][37]  = 5369940202574713097ULL;
    Zobrist::psq[14][38]  = 4976109024626592507ULL;
    Zobrist::psq[14][39]  = 17662718886951473514ULL;
    Zobrist::psq[14][40]  = 10194604604769366768ULL;
    Zobrist::psq[14][41]  = 9434649875492567077ULL;
    Zobrist::psq[14][42]  = 9275344374679790988ULL;
    Zobrist::psq[14][43]  = 13950395516943844512ULL;
    Zobrist::psq[14][44]  = 4634019286100624619ULL;
    Zobrist::psq[14][45]  = 17524913661501655732ULL;
    Zobrist::psq[14][46]  = 12758868016771465513ULL;
    Zobrist::psq[14][47]  = 3127147764315865797ULL;
    Zobrist::psq[14][48]  = 3960938717909563730ULL;
    Zobrist::psq[14][49]  = 14869830638616427590ULL;
    Zobrist::psq[14][50]  = 305185646789997459ULL;
    Zobrist::psq[14][51]  = 4139658351799906696ULL;
    Zobrist::psq[14][52]  = 272667046354598132ULL;
    Zobrist::psq[14][53]  = 15621274402096728762ULL;
    Zobrist::psq[14][54]  = 16483498129229512495ULL;
    Zobrist::psq[14][55]  = 12953368655171389128ULL;
    Zobrist::psq[14][56]  = 10678035399177741929ULL;
    Zobrist::psq[14][57]  = 18049652274331575310ULL;
    Zobrist::psq[14][58]  = 7975081034372805163ULL;
    Zobrist::psq[14][59]  = 10522098076497821829ULL;
    Zobrist::psq[14][60]  = 12606359703294662790ULL;
    Zobrist::psq[14][61]  = 13924857104548874958ULL;
    Zobrist::psq[14][62]  = 6566773282407180921ULL;
    Zobrist::psq[14][63]  = 3452471826952569846ULL;
    Zobrist::enpassant[0] = 9031641776876329352ULL;
    Zobrist::enpassant[1] = 12228382040141709029ULL;
    Zobrist::enpassant[2] = 2494223668561036951ULL;
    Zobrist::enpassant[3] = 7849557628814744642ULL;
    Zobrist::enpassant[4] = 16000570245257669890ULL;
    Zobrist::enpassant[5] = 16614404541835922253ULL;
    Zobrist::enpassant[6] = 17787301719840479309ULL;
    Zobrist::enpassant[7] = 6371708097697762807ULL;
    Zobrist::castling[1]  = 7487338029351702425ULL;
    Zobrist::castling[2]  = 10138645747811604478ULL;
    Zobrist::castling[3]  = 16959407016388712551ULL;
    Zobrist::castling[4]  = 16332212992845378228ULL;
    Zobrist::castling[5]  = 9606164174486469933ULL;
    Zobrist::castling[6]  = 7931993123235079498ULL;
    Zobrist::castling[7]  = 719529192282958547ULL;
    Zobrist::castling[8]  = 6795873897769436611ULL;
    Zobrist::castling[9]  = 4154453049008294490ULL;
    Zobrist::castling[10] = 15203167020455580221ULL;
    Zobrist::castling[11] = 13048090984296504740ULL;
    Zobrist::castling[12] = 13612242447579281271ULL;
    Zobrist::castling[13] = 15780674830245624046ULL;
    Zobrist::castling[14] = 3484610688987504777ULL;
    Zobrist::castling[15] = 6319549394931232528ULL;
    Zobrist::side         = 4906379431808431525ULL;
    Zobrist::noPawns      = 895963052000028445ULL;

  // Prepare the cuckoo tables
  std::memset(cuckoo, 0, sizeof(cuckoo));
  std::memset(cuckooMove, 0, sizeof(cuckooMove));
  [[maybe_unused]] int count = 0;
  for (Piece pc : Pieces)
      for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
          for (Square s2 = Square(s1 + 1); s2 <= SQ_H8; ++s2)
              if ((type_of(pc) != PAWN) && (attacks_bb(type_of(pc), s1, 0) & s2))
              {
                  Move move = make_move(s1, s2);
                  Key key = Zobrist::psq[pc][s1] ^ Zobrist::psq[pc][s2] ^ Zobrist::side;
                  int i = H1(key);
                  while (true)
                  {
                      std::swap(cuckoo[i], key);
                      std::swap(cuckooMove[i], move);
                      if (move == MOVE_NONE) // Arrived at empty slot?
                          break;
                      i = (i == H1(key)) ? H2(key) : H1(key); // Push victim to alternative slot
                  }
                  count++;
             }
  assert(count == 3668);
}


/// Position::set() initializes the position object with the given FEN string.
/// This function is not very robust - make sure that input FENs are correct,
/// this is assumed to be the responsibility of the GUI.

Position& Position::set(const string& fenStr, bool isChess960, StateInfo* si, Thread* th) {
/*
   A FEN string defines a particular position using only the ASCII character set.

   A FEN string contains six fields separated by a space. The fields are:

   1) Piece placement (from white's perspective). Each rank is described, starting
      with rank 8 and ending with rank 1. Within each rank, the contents of each
      square are described from file A through file H. Following the Standard
      Algebraic Notation (SAN), each piece is identified by a single letter taken
      from the standard English names. White pieces are designated using upper-case
      letters ("PNBRQK") whilst Black uses lowercase ("pnbrqk"). Blank squares are
      noted using digits 1 through 8 (the number of blank squares), and "/"
      separates ranks.

   2) Active color. "w" means white moves next, "b" means black.

   3) Castling availability. If neither side can castle, this is "-". Otherwise,
      this has one or more letters: "K" (White can castle kingside), "Q" (White
      can castle queenside), "k" (Black can castle kingside), and/or "q" (Black
      can castle queenside).

   4) En passant target square (in algebraic notation). If there's no en passant
      target square, this is "-". If a pawn has just made a 2-square move, this
      is the position "behind" the pawn. Following X-FEN standard, this is recorded only
      if there is a pawn in position to make an en passant capture, and if there really
      is a pawn that might have advanced two squares.

   5) Halfmove clock. This is the number of halfmoves since the last pawn advance
      or capture. This is used to determine if a draw can be claimed under the
      fifty-move rule.

   6) Fullmove number. The number of the full move. It starts at 1, and is
      incremented after Black's move.
*/

  unsigned char col, row, token;
  size_t idx;
  Square sq = SQ_A8;
  std::istringstream ss(fenStr);

  std::memset(this, 0, sizeof(Position));
  std::memset(si, 0, sizeof(StateInfo));
  st = si;

  ss >> std::noskipws;

  // 1. Piece placement
  while ((ss >> token) && !isspace(token))
  {
      if (isdigit(token))
          sq += (token - '0') * EAST; // Advance the given number of files

      else if (token == '/')
          sq += 2 * SOUTH;

      else if ((idx = PieceToChar.find(token)) != string::npos) {
          put_piece(Piece(idx), sq);
          ++sq;
      }
  }

  // 2. Active color
  ss >> token;
  sideToMove = (token == 'w' ? WHITE : BLACK);
  ss >> token;

  // 3. Castling availability. Compatible with 3 standards: Normal FEN standard,
  // Shredder-FEN that uses the letters of the columns on which the rooks began
  // the game instead of KQkq and also X-FEN standard that, in case of Chess960,
  // if an inner rook is associated with the castling right, the castling tag is
  // replaced by the file letter of the involved rook, as for the Shredder-FEN.
  while ((ss >> token) && !isspace(token))
  {
      Square rsq;
      Color c = islower(token) ? BLACK : WHITE;
      Piece rook = make_piece(c, ROOK);

      token = char(toupper(token));

      if (token == 'K')
          for (rsq = relative_square(c, SQ_H1); piece_on(rsq) != rook; --rsq) {}

      else if (token == 'Q')
          for (rsq = relative_square(c, SQ_A1); piece_on(rsq) != rook; ++rsq) {}

      else if (token >= 'A' && token <= 'H')
          rsq = make_square(File(token - 'A'), relative_rank(c, RANK_1));

      else
          continue;

      set_castling_right(c, rsq);
  }

  // 4. En passant square.
  // Ignore if square is invalid or not on side to move relative rank 6.
  bool enpassant = false;

  if (   ((ss >> col) && (col >= 'a' && col <= 'h'))
      && ((ss >> row) && (row == (sideToMove == WHITE ? '6' : '3'))))
  {
      st->epSquare = make_square(File(col - 'a'), Rank(row - '1'));

      // En passant square will be considered only if
      // a) side to move have a pawn threatening epSquare
      // b) there is an enemy pawn in front of epSquare
      // c) there is no piece on epSquare or behind epSquare
      enpassant = pawn_attacks_bb(~sideToMove, st->epSquare) & pieces(sideToMove, PAWN)
               && (pieces(~sideToMove, PAWN) & (st->epSquare + pawn_push(~sideToMove)))
               && !(pieces() & (st->epSquare | (st->epSquare + pawn_push(sideToMove))));
  }

  if (!enpassant)
      st->epSquare = SQ_NONE;

  // 5-6. Halfmove clock and fullmove number
  ss >> std::skipws >> st->rule50 >> gamePly;

  // Convert from fullmove starting from 1 to gamePly starting from 0,
  // handle also common incorrect FEN with fullmove = 0.
  gamePly = std::max(2 * (gamePly - 1), 0) + (sideToMove == BLACK);

  chess960 = isChess960;
  thisThread = th;
  set_state();

  assert(pos_is_ok());

  return *this;
}


/// Position::set_castling_right() is a helper function used to set castling
/// rights given the corresponding color and the rook starting square.

void Position::set_castling_right(Color c, Square rfrom) {

  Square kfrom = square<KING>(c);
  CastlingRights cr = c & (kfrom < rfrom ? KING_SIDE: QUEEN_SIDE);

  st->castlingRights |= cr;
  castlingRightsMask[kfrom] |= cr;
  castlingRightsMask[rfrom] |= cr;
  castlingRookSquare[cr] = rfrom;

  Square kto = relative_square(c, cr & KING_SIDE ? SQ_G1 : SQ_C1);
  Square rto = relative_square(c, cr & KING_SIDE ? SQ_F1 : SQ_D1);

  castlingPath[cr] =   (between_bb(rfrom, rto) | between_bb(kfrom, kto))
                    & ~(kfrom | rfrom);
}


/// Position::set_check_info() sets king attacks to detect if a move gives check

void Position::set_check_info() const {

  st->blockersForKing[WHITE] = slider_blockers(pieces(BLACK), square<KING>(WHITE), st->pinners[BLACK]);
  st->blockersForKing[BLACK] = slider_blockers(pieces(WHITE), square<KING>(BLACK), st->pinners[WHITE]);

  Square ksq = square<KING>(~sideToMove);

  st->checkSquares[PAWN]   = pawn_attacks_bb(~sideToMove, ksq);
  st->checkSquares[KNIGHT] = attacks_bb<KNIGHT>(ksq);
  st->checkSquares[BISHOP] = attacks_bb<BISHOP>(ksq, pieces());
  st->checkSquares[ROOK]   = attacks_bb<ROOK>(ksq, pieces());
  st->checkSquares[QUEEN]  = st->checkSquares[BISHOP] | st->checkSquares[ROOK];
  st->checkSquares[KING]   = 0;
}


/// Position::set_state() computes the hash keys of the position, and other
/// data that once computed is updated incrementally as moves are made.
/// The function is only used when a new position is set up

void Position::set_state() const {

  st->key = st->materialKey = 0;
  st->pawnKey = Zobrist::noPawns;
  st->nonPawnMaterial[WHITE] = st->nonPawnMaterial[BLACK] = VALUE_ZERO;
  st->checkersBB = attackers_to(square<KING>(sideToMove)) & pieces(~sideToMove);

  set_check_info();

  for (Bitboard b = pieces(); b; )
  {
      Square s = pop_lsb(b);
      Piece pc = piece_on(s);
      st->key ^= Zobrist::psq[pc][s];

      if (type_of(pc) == PAWN)
          st->pawnKey ^= Zobrist::psq[pc][s];

      else if (type_of(pc) != KING)
          st->nonPawnMaterial[color_of(pc)] += PieceValue[MG][pc];
  }

  if (st->epSquare != SQ_NONE)
      st->key ^= Zobrist::enpassant[file_of(st->epSquare)];

  if (sideToMove == BLACK)
      st->key ^= Zobrist::side;

  st->key ^= Zobrist::castling[st->castlingRights];

  for (Piece pc : Pieces)
      for (int cnt = 0; cnt < pieceCount[pc]; ++cnt)
          st->materialKey ^= Zobrist::psq[pc][cnt];
}


/// Position::set() is an overload to initialize the position object with
/// the given endgame code string like "KBPKN". It is mainly a helper to
/// get the material key out of an endgame code.

Position& Position::set(const string& code, Color c, StateInfo* si) {

  assert(code[0] == 'K');

  string sides[] = { code.substr(code.find('K', 1)),      // Weak
                     code.substr(0, std::min(code.find('v'), code.find('K', 1))) }; // Strong

  assert(sides[0].length() > 0 && sides[0].length() < 8);
  assert(sides[1].length() > 0 && sides[1].length() < 8);

  std::transform(sides[c].begin(), sides[c].end(), sides[c].begin(), tolower);

  string fenStr = "8/" + sides[0] + char(8 - sides[0].length() + '0') + "/8/8/8/8/"
                       + sides[1] + char(8 - sides[1].length() + '0') + "/8 w - - 0 10";

  return set(fenStr, false, si, nullptr);
}


/// Position::fen() returns a FEN representation of the position. In case of
/// Chess960 the Shredder-FEN notation is used. This is mainly a debugging function.

string Position::fen() const {

  int emptyCnt;
  std::ostringstream ss;

  for (Rank r = RANK_8; r >= RANK_1; --r)
  {
      for (File f = FILE_A; f <= FILE_H; ++f)
      {
          for (emptyCnt = 0; f <= FILE_H && empty(make_square(f, r)); ++f)
              ++emptyCnt;

          if (emptyCnt)
              ss << emptyCnt;

          if (f <= FILE_H)
              ss << PieceToChar[piece_on(make_square(f, r))];
      }

      if (r > RANK_1)
          ss << '/';
  }

  ss << (sideToMove == WHITE ? " w " : " b ");

  if (can_castle(WHITE_OO))
      ss << (chess960 ? char('A' + file_of(castling_rook_square(WHITE_OO ))) : 'K');

  if (can_castle(WHITE_OOO))
      ss << (chess960 ? char('A' + file_of(castling_rook_square(WHITE_OOO))) : 'Q');

  if (can_castle(BLACK_OO))
      ss << (chess960 ? char('a' + file_of(castling_rook_square(BLACK_OO ))) : 'k');

  if (can_castle(BLACK_OOO))
      ss << (chess960 ? char('a' + file_of(castling_rook_square(BLACK_OOO))) : 'q');

  if (!can_castle(ANY_CASTLING))
      ss << '-';

  ss << (ep_square() == SQ_NONE ? " - " : " " + UCI::square(ep_square()) + " ")
     << st->rule50 << " " << 1 + (gamePly - (sideToMove == BLACK)) / 2;

  return ss.str();
}


/// Position::slider_blockers() returns a bitboard of all the pieces (both colors)
/// that are blocking attacks on the square 's' from 'sliders'. A piece blocks a
/// slider if removing that piece from the board would result in a position where
/// square 's' is attacked. For example, a king-attack blocking piece can be either
/// a pinned or a discovered check piece, according if its color is the opposite
/// or the same of the color of the slider.

Bitboard Position::slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const {

  Bitboard blockers = 0;
  pinners = 0;

  // Snipers are sliders that attack 's' when a piece and other snipers are removed
  Bitboard snipers = (  (attacks_bb<  ROOK>(s) & pieces(QUEEN, ROOK))
                      | (attacks_bb<BISHOP>(s) & pieces(QUEEN, BISHOP))) & sliders;
  Bitboard occupancy = pieces() ^ snipers;

  while (snipers)
  {
    Square sniperSq = pop_lsb(snipers);
    Bitboard b = between_bb(s, sniperSq) & occupancy;

    if (b && !more_than_one(b))
    {
        blockers |= b;
        if (b & pieces(color_of(piece_on(s))))
            pinners |= sniperSq;
    }
  }
  return blockers;
}


/// Position::attackers_to() computes a bitboard of all pieces which attack a
/// given square. Slider attacks use the occupied bitboard to indicate occupancy.

Bitboard Position::attackers_to(Square s, Bitboard occupied) const {

  return  (pawn_attacks_bb(BLACK, s)       & pieces(WHITE, PAWN))
        | (pawn_attacks_bb(WHITE, s)       & pieces(BLACK, PAWN))
        | (attacks_bb<KNIGHT>(s)           & pieces(KNIGHT))
        | (attacks_bb<  ROOK>(s, occupied) & pieces(  ROOK, QUEEN))
        | (attacks_bb<BISHOP>(s, occupied) & pieces(BISHOP, QUEEN))
        | (attacks_bb<KING>(s)             & pieces(KING));
}


/// Position::legal() tests whether a pseudo-legal move is legal

bool Position::legal(Move m) const {

  assert(is_ok(m));

  Color us = sideToMove;
  Square from = from_sq(m);
  Square to = to_sq(m);

  assert(color_of(moved_piece(m)) == us);
  assert(piece_on(square<KING>(us)) == make_piece(us, KING));

  // En passant captures are a tricky special case. Because they are rather
  // uncommon, we do it simply by testing whether the king is attacked after
  // the move is made.
  if (type_of(m) == EN_PASSANT)
  {
      Square ksq = square<KING>(us);
      Square capsq = to - pawn_push(us);
      Bitboard occupied = (pieces() ^ from ^ capsq) | to;

      assert(to == ep_square());
      assert(moved_piece(m) == make_piece(us, PAWN));
      assert(piece_on(capsq) == make_piece(~us, PAWN));
      assert(piece_on(to) == NO_PIECE);

      return   !(attacks_bb<  ROOK>(ksq, occupied) & pieces(~us, QUEEN, ROOK))
            && !(attacks_bb<BISHOP>(ksq, occupied) & pieces(~us, QUEEN, BISHOP));
  }

  // Castling moves generation does not check if the castling path is clear of
  // enemy attacks, it is delayed at a later time: now!
  if (type_of(m) == CASTLING)
  {
      // After castling, the rook and king final positions are the same in
      // Chess960 as they would be in standard chess.
      to = relative_square(us, to > from ? SQ_G1 : SQ_C1);
      Direction step = to > from ? WEST : EAST;

      for (Square s = to; s != from; s += step)
          if (attackers_to(s) & pieces(~us))
              return false;

      // In case of Chess960, verify if the Rook blocks some checks
      // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
      return !chess960 || !(blockers_for_king(us) & to_sq(m));
  }

  // If the moving piece is a king, check whether the destination square is
  // attacked by the opponent.
  if (type_of(piece_on(from)) == KING)
      return !(attackers_to(to, pieces() ^ from) & pieces(~us));

  // A non-king move is legal if and only if it is not pinned or it
  // is moving along the ray towards or away from the king.
  return !(blockers_for_king(us) & from)
      || aligned(from, to, square<KING>(us));
}


/// Position::pseudo_legal() takes a random move and tests whether the move is
/// pseudo legal. It is used to validate moves from TT that can be corrupted
/// due to SMP concurrent access or hash position key aliasing.

bool Position::pseudo_legal(const Move m) const {

  Color us = sideToMove;
  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = moved_piece(m);

  // Use a slower but simpler function for uncommon cases
  // yet we skip the legality check of MoveList<LEGAL>().
  if (type_of(m) != NORMAL)
      return checkers() ? MoveList<    EVASIONS>(*this).contains(m)
                        : MoveList<NON_EVASIONS>(*this).contains(m);

  // Is not a promotion, so promotion piece must be empty
  assert(promotion_type(m) - KNIGHT == NO_PIECE_TYPE);

  // If the 'from' square is not occupied by a piece belonging to the side to
  // move, the move is obviously not legal.
  if (pc == NO_PIECE || color_of(pc) != us)
      return false;

  // The destination square cannot be occupied by a friendly piece
  if (pieces(us) & to)
      return false;

  // Handle the special case of a pawn move
  if (type_of(pc) == PAWN)
  {
      // We have already handled promotion moves, so destination
      // cannot be on the 8th/1st rank.
      if ((Rank8BB | Rank1BB) & to)
          return false;

      if (   !(pawn_attacks_bb(us, from) & pieces(~us) & to) // Not a capture
          && !((from + pawn_push(us) == to) && empty(to))       // Not a single push
          && !(   (from + 2 * pawn_push(us) == to)              // Not a double push
               && (relative_rank(us, from) == RANK_2)
               && empty(to)
               && empty(to - pawn_push(us))))
          return false;
  }
  else if (!(attacks_bb(type_of(pc), from, pieces()) & to))
      return false;

  // Evasions generator already takes care to avoid some kind of illegal moves
  // and legal() relies on this. We therefore have to take care that the same
  // kind of moves are filtered out here.
  if (checkers())
  {
      if (type_of(pc) != KING)
      {
          // Double check? In this case a king move is required
          if (more_than_one(checkers()))
              return false;

          // Our move must be a blocking interposition or a capture of the checking piece
          if (!(between_bb(square<KING>(us), lsb(checkers())) & to))
              return false;
      }
      // In case of king moves under check we have to remove king so as to catch
      // invalid moves like b1a1 when opposite queen is on c1.
      else if (attackers_to(to, pieces() ^ from) & pieces(~us))
          return false;
  }

  return true;
}


/// Position::gives_check() tests whether a pseudo-legal move gives a check

bool Position::gives_check(Move m) const {

  assert(is_ok(m));
  assert(color_of(moved_piece(m)) == sideToMove);

  Square from = from_sq(m);
  Square to = to_sq(m);

  // Is there a direct check?
  if (check_squares(type_of(piece_on(from))) & to)
      return true;

  // Is there a discovered check?
  if (blockers_for_king(~sideToMove) & from)
      return   !aligned(from, to, square<KING>(~sideToMove))
            || type_of(m) == CASTLING;

  switch (type_of(m))
  {
  case NORMAL:
      return false;

  case PROMOTION:
      return attacks_bb(promotion_type(m), to, pieces() ^ from) & square<KING>(~sideToMove);

  // En passant capture with check? We have already handled the case
  // of direct checks and ordinary discovered check, so the only case we
  // need to handle is the unusual case of a discovered check through
  // the captured pawn.
  case EN_PASSANT:
  {
      Square capsq = make_square(file_of(to), rank_of(from));
      Bitboard b = (pieces() ^ from ^ capsq) | to;

      return  (attacks_bb<  ROOK>(square<KING>(~sideToMove), b) & pieces(sideToMove, QUEEN, ROOK))
            | (attacks_bb<BISHOP>(square<KING>(~sideToMove), b) & pieces(sideToMove, QUEEN, BISHOP));
  }
  default: //CASTLING
  {
      // Castling is encoded as 'king captures the rook'
      Square rto = relative_square(sideToMove, to > from ? SQ_F1 : SQ_D1);

      return check_squares(ROOK) & rto;
  }
  }
}


/// Position::do_move() makes a move, and saves all information necessary
/// to a StateInfo object. The move is assumed to be legal. Pseudo-legal
/// moves should be filtered out before this function is called.

void Position::do_move(Move m, StateInfo& newSt, bool givesCheck) {

  assert(is_ok(m));
  assert(&newSt != st);

  thisThread->nodes.fetch_add(1, std::memory_order_relaxed);
  Key k = st->key ^ Zobrist::side;

  // Copy some fields of the old state to our new StateInfo object except the
  // ones which are going to be recalculated from scratch anyway and then switch
  // our state pointer to point to the new (ready to be updated) state.
  std::memcpy(&newSt, st, offsetof(StateInfo, key));
  newSt.previous = st;
  st = &newSt;

  // Increment ply counters. In particular, rule50 will be reset to zero later on
  // in case of a capture or a pawn move.
  ++gamePly;
  ++st->rule50;
  ++st->pliesFromNull;

  Color us = sideToMove;
  Color them = ~us;
  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = piece_on(from);
  Piece captured = type_of(m) == EN_PASSANT ? make_piece(them, PAWN) : piece_on(to);

  assert(color_of(pc) == us);
  assert(captured == NO_PIECE || color_of(captured) == (type_of(m) != CASTLING ? them : us));
  assert(type_of(captured) != KING);

  if (type_of(m) == CASTLING)
  {
      assert(pc == make_piece(us, KING));
      assert(captured == make_piece(us, ROOK));

      Square rfrom, rto;
      do_castling<true>(us, from, to, rfrom, rto);

      k ^= Zobrist::psq[captured][rfrom] ^ Zobrist::psq[captured][rto];
      captured = NO_PIECE;
  }

  if (captured)
  {
      Square capsq = to;

      // If the captured piece is a pawn, update pawn hash key, otherwise
      // update non-pawn material.
      if (type_of(captured) == PAWN)
      {
          if (type_of(m) == EN_PASSANT)
          {
              capsq -= pawn_push(us);

              assert(pc == make_piece(us, PAWN));
              assert(to == st->epSquare);
              assert(relative_rank(us, to) == RANK_6);
              assert(piece_on(to) == NO_PIECE);
              assert(piece_on(capsq) == make_piece(them, PAWN));
          }

          st->pawnKey ^= Zobrist::psq[captured][capsq];
      }
      else
          st->nonPawnMaterial[them] -= PieceValue[MG][captured];

      // Update board and piece lists
      remove_piece(capsq);

      // Update material hash key and prefetch access to materialTable
      k ^= Zobrist::psq[captured][capsq];
      st->materialKey ^= Zobrist::psq[captured][pieceCount[captured]];
      prefetch(thisThread->materialTable[st->materialKey]);

      // Reset rule 50 counter
      st->rule50 = 0;
  }

  // Update hash key
  k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];

  // Reset en passant square
  if (st->epSquare != SQ_NONE)
  {
      k ^= Zobrist::enpassant[file_of(st->epSquare)];
      st->epSquare = SQ_NONE;
  }

// Update castling rights if needed
if (st->castlingRights && (castlingRightsMask[from] | castlingRightsMask[to]))
{
    k ^= Zobrist::castling[st->castlingRights];
    st->castlingRights &= ~(castlingRightsMask[from] | castlingRightsMask[to]);
    k ^= Zobrist::castling[st->castlingRights];
}

// Move the piece. The tricky Chess960 castling is handled earlier
if (type_of(m) != CASTLING)
{
    move_piece(from, to);
}

// If the moving piece is a pawn do some special extra work
  if (type_of(pc) == PAWN)
  {
      // Set en passant square if the moved pawn can be captured
      if (   (int(to) ^ int(from)) == 16
          && (pawn_attacks_bb(us, to - pawn_push(us)) & pieces(them, PAWN)))
      {
          st->epSquare = to - pawn_push(us);
          k ^= Zobrist::enpassant[file_of(st->epSquare)];
      }

      else if (type_of(m) == PROMOTION)
      {
          Piece promotion = make_piece(us, promotion_type(m));

          assert(relative_rank(us, to) == RANK_8);
          assert(type_of(promotion) >= KNIGHT && type_of(promotion) <= QUEEN);

          remove_piece(to);
          put_piece(promotion, to);

          // Update hash keys
          k ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promotion][to];
          st->pawnKey ^= Zobrist::psq[pc][to];
          st->materialKey ^=  Zobrist::psq[promotion][pieceCount[promotion]-1]
                            ^ Zobrist::psq[pc][pieceCount[pc]];

          // Update material
          st->nonPawnMaterial[us] += PieceValue[MG][promotion];
      }

      // Update pawn hash key
      st->pawnKey ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];

      // Reset rule 50 draw counter
      st->rule50 = 0;
  }

  // Set capture piece
  st->capturedPiece = captured;

  // Update the key with the final value
  st->key = k;

  // Calculate checkers bitboard (if move gives check)
  st->checkersBB = givesCheck ? attackers_to(square<KING>(them)) & pieces(us) : 0;

  sideToMove = ~sideToMove;

  // Update king attacks used for fast check detection
  set_check_info();

  // Calculate the repetition info. It is the ply distance from the previous
  // occurrence of the same position, negative in the 3-fold case, or zero
  // if the position was not repeated.
  st->repetition = 0;
  int end = std::min(st->rule50, st->pliesFromNull);
  if (end >= 4)
  {
      StateInfo* stp = st->previous->previous;
      for (int i = 4; i <= end; i += 2)
      {
          stp = stp->previous->previous;
          if (stp->key == st->key)
          {
              st->repetition = stp->repetition ? -i : i;
              break;
          }
      }
  }

  assert(pos_is_ok());
}


/// Position::undo_move() unmakes a move. When it returns, the position should
/// be restored to exactly the same state as before the move was made.

void Position::undo_move(Move m) {

  assert(is_ok(m));

  sideToMove = ~sideToMove;

  Color us = sideToMove;
  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = piece_on(to);

  assert(empty(from) || type_of(m) == CASTLING);
  assert(type_of(st->capturedPiece) != KING);

  if (type_of(m) == PROMOTION)
  {
      assert(relative_rank(us, to) == RANK_8);
      assert(type_of(pc) == promotion_type(m));
      assert(type_of(pc) >= KNIGHT && type_of(pc) <= QUEEN);

      remove_piece(to);
      pc = make_piece(us, PAWN);
      put_piece(pc, to);
  }

  if (type_of(m) == CASTLING)
  {
      Square rfrom, rto;
      do_castling<false>(us, from, to, rfrom, rto);
  }
  else
  {
      move_piece(to, from); // Put the piece back at the source square

      if (st->capturedPiece)
      {
          Square capsq = to;

          if (type_of(m) == EN_PASSANT)
          {
              capsq -= pawn_push(us);

              assert(type_of(pc) == PAWN);
              assert(to == st->previous->epSquare);
              assert(relative_rank(us, to) == RANK_6);
              assert(piece_on(capsq) == NO_PIECE);
              assert(st->capturedPiece == make_piece(~us, PAWN));
          }

          put_piece(st->capturedPiece, capsq); // Restore the captured piece
      }
  }

  // Finally point our state pointer back to the previous state
  st = st->previous;
  --gamePly;

  assert(pos_is_ok());
}


/// Position::do_castling() is a helper used to do/undo a castling move. This
/// is a bit tricky in Chess960 where from/to squares can overlap.
template<bool Do>
void Position::do_castling(Color us, Square from, Square& to, Square& rfrom, Square& rto) {

  bool kingSide = to > from;
  rfrom = to; // Castling is encoded as "king captures friendly rook"
  rto = relative_square(us, kingSide ? SQ_F1 : SQ_D1);
  to = relative_square(us, kingSide ? SQ_G1 : SQ_C1);

  // Remove both pieces first since squares could overlap in Chess960
  remove_piece(Do ? from : to);
  remove_piece(Do ? rfrom : rto);
  board[Do ? from : to] = board[Do ? rfrom : rto] = NO_PIECE; // Since remove_piece doesn't do this for us
  put_piece(make_piece(us, KING), Do ? to : from);
  put_piece(make_piece(us, ROOK), Do ? rto : rfrom);
}


/// Position::do_null_move() is used to do a "null move": it flips
/// the side to move without executing any move on the board.

void Position::do_null_move(StateInfo& newSt) {

  assert(!checkers());
  assert(&newSt != st);

  // Copia lo stato fino a key, escludendo le parti rimosse
  std::memcpy(&newSt, st, sizeof(StateInfo));

  newSt.previous = st;
  st = &newSt;

  // Gestisci l'en passant
  if (st->epSquare != SQ_NONE) {
      st->key ^= Zobrist::enpassant[file_of(st->epSquare)];
      st->epSquare = SQ_NONE;
  }

  // Aggiorna altre variabili necessarie per mantenere la consistenza
  st->pliesFromNull = 0;
  sideToMove = ~sideToMove;
  st->key ^= Zobrist::side;

  // Reimposta il contesto degli scacchi
  set_check_info();

  assert(pos_is_ok());
}


/// Position::undo_null_move() must be used to undo a "null move"

void Position::undo_null_move() {

  assert(!checkers());

  st = st->previous;
  sideToMove = ~sideToMove;
}


/// Position::key_after() computes the new hash key after the given move. Needed
/// for speculative prefetch. It doesn't recognize special moves like castling,
/// en passant and promotions.

Key Position::key_after(Move m) const {

  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = piece_on(from);
  Piece captured = piece_on(to);
  Key k = st->key ^ Zobrist::side;

  if (captured)
      k ^= Zobrist::psq[captured][to];

  k ^= Zobrist::psq[pc][to] ^ Zobrist::psq[pc][from];

  return (captured || type_of(pc) == PAWN)
      ? k : adjust_key50<true>(k);
}


/// Position::see_ge (Static Exchange Evaluation Greater or Equal) tests if the
/// SEE value of move is greater or equal to the given threshold. We'll use an
/// algorithm similar to alpha-beta pruning with a null window.

bool Position::see_ge(Move m, Bitboard& occupied, Value threshold) const {

  assert(is_ok(m));

  // Only deal with normal moves, assume others pass a simple SEE
  if (type_of(m) != NORMAL)
      return VALUE_ZERO >= threshold;

  Square from = from_sq(m), to = to_sq(m);

  int swap = PieceValue[MG][piece_on(to)] - threshold;
  if (swap < 0)
      return false;

  swap = PieceValue[MG][piece_on(from)] - swap;
  if (swap <= 0)
      return true;

  assert(color_of(piece_on(from)) == sideToMove);
  occupied = pieces() ^ from ^ to; // xoring to is important for pinned piece logic
  Color stm = sideToMove;
  Bitboard attackers = attackers_to(to, occupied);
  Bitboard stmAttackers, bb;
  int res = 1;

  while (true)
  {
      stm = ~stm;
      attackers &= occupied;

      // If stm has no more attackers then give up: stm loses
      if (!(stmAttackers = attackers & pieces(stm)))
          break;

      // Don't allow pinned pieces to attack as long as there are
      // pinners on their original square.
      if (pinners(~stm) & occupied)
      {
          stmAttackers &= ~blockers_for_king(stm);

          if (!stmAttackers)
              break;
      }

      res ^= 1;

      // Locate and remove the next least valuable attacker, and add to
      // the bitboard 'attackers' any X-ray attackers behind it.
      if ((bb = stmAttackers & pieces(PAWN)))
      {
          occupied ^= least_significant_square_bb(bb);
          if ((swap = PawnValueMg - swap) < res)
              break;

          attackers |= attacks_bb<BISHOP>(to, occupied) & pieces(BISHOP, QUEEN);
      }

      else if ((bb = stmAttackers & pieces(KNIGHT)))
      {
          occupied ^= least_significant_square_bb(bb);
          if ((swap = KnightValueMg - swap) < res)
              break;
      }

      else if ((bb = stmAttackers & pieces(BISHOP)))
      {
          occupied ^= least_significant_square_bb(bb);
          if ((swap = BishopValueMg - swap) < res)
              break;

          attackers |= attacks_bb<BISHOP>(to, occupied) & pieces(BISHOP, QUEEN);
      }

      else if ((bb = stmAttackers & pieces(ROOK)))
      {
          occupied ^= least_significant_square_bb(bb);
          if ((swap = RookValueMg - swap) < res)
              break;

          attackers |= attacks_bb<ROOK>(to, occupied) & pieces(ROOK, QUEEN);
      }

      else if ((bb = stmAttackers & pieces(QUEEN)))
      {
          occupied ^= least_significant_square_bb(bb);
          if ((swap = QueenValueMg - swap) < res)
              break;

          attackers |=  (attacks_bb<BISHOP>(to, occupied) & pieces(BISHOP, QUEEN))
                      | (attacks_bb<ROOK  >(to, occupied) & pieces(ROOK  , QUEEN));
      }

      else // KING
           // If we "capture" with the king but opponent still has attackers,
           // reverse the result.
          return (attackers & ~pieces(stm)) ? res ^ 1 : res;
  }

  return bool(res);
}

bool Position::see_ge(Move m, Value threshold) const {
    Bitboard occupied;
    return see_ge(m, occupied, threshold);
}


/// Position::is_draw() tests whether the position is drawn by 50-move rule
/// or by repetition. It does not detect stalemates.

bool Position::is_draw(int ply) const {

  if (st->rule50 > 99 && (!checkers() || MoveList<LEGAL>(*this).size()))
      return true;

  // Return a draw score if a position repeats once earlier but strictly
  // after the root, or repeats twice before or at the root.
  return st->repetition && st->repetition < ply;
}


// Position::has_repeated() tests whether there has been at least one repetition
// of positions since the last capture or pawn move.

bool Position::has_repeated() const {

    StateInfo* stc = st;
    int end = std::min(st->rule50, st->pliesFromNull);
    while (end-- >= 4)
    {
        if (stc->repetition)
            return true;

        stc = stc->previous;
    }
    return false;
}


/// Position::has_game_cycle() tests if the position has a move which draws by repetition,
/// or an earlier position has a move that directly reaches the current position.

bool Position::has_game_cycle(int ply) const {

  int j;

  int end = std::min(st->rule50, st->pliesFromNull);

  if (end < 3)
    return false;

  Key originalKey = st->key;
  StateInfo* stp = st->previous;

  for (int i = 3; i <= end; i += 2)
  {
      stp = stp->previous->previous;

      Key moveKey = originalKey ^ stp->key;
      if (   (j = H1(moveKey), cuckoo[j] == moveKey)
          || (j = H2(moveKey), cuckoo[j] == moveKey))
      {
          Move move = cuckooMove[j];
          Square s1 = from_sq(move);
          Square s2 = to_sq(move);

          if (!((between_bb(s1, s2) ^ s2) & pieces()))
          {
              if (ply > i)
                  return true;

              // For nodes before or at the root, check that the move is a
              // repetition rather than a move to the current position.
              // In the cuckoo table, both moves Rc1c5 and Rc5c1 are stored in
              // the same location, so we have to select which square to check.
              if (color_of(piece_on(empty(s1) ? s2 : s1)) != side_to_move())
                  continue;

              // For repetitions before or at the root, require one more
              if (stp->repetition)
                  return true;
          }
      }
  }
  return false;
}


/// Position::flip() flips position with the white and black sides reversed. This
/// is only useful for debugging e.g. for finding evaluation symmetry bugs.

void Position::flip() {

  string f, token;
  std::stringstream ss(fen());

  for (Rank r = RANK_8; r >= RANK_1; --r) // Piece placement
  {
      std::getline(ss, token, r > RANK_1 ? '/' : ' ');
      f.insert(0, token + (f.empty() ? " " : "/"));
  }

  ss >> token; // Active color
  f += (token == "w" ? "B " : "W "); // Will be lowercased later

  ss >> token; // Castling availability
  f += token + " ";

  std::transform(f.begin(), f.end(), f.begin(),
                 [](char c) { return char(islower(c) ? toupper(c) : tolower(c)); });

  ss >> token; // En passant square
  f += (token == "-" ? token : token.replace(1, 1, token[1] == '3' ? "6" : "3"));

  std::getline(ss, token); // Half and full moves
  f += token;

  set(f, is_chess960(), st, this_thread());

  assert(pos_is_ok());
}


/// Position::pos_is_ok() performs some consistency checks for the
/// position object and raises an asserts if something wrong is detected.
/// This is meant to be helpful when debugging.

bool Position::pos_is_ok() const {

  constexpr bool Fast = true; // Quick (default) or full check?

  if (   (sideToMove != WHITE && sideToMove != BLACK)
      || piece_on(square<KING>(WHITE)) != W_KING
      || piece_on(square<KING>(BLACK)) != B_KING
      || (   ep_square() != SQ_NONE
          && relative_rank(sideToMove, ep_square()) != RANK_6))
      assert(0 && "pos_is_ok: Default");

  if (Fast)
      return true;

  if (   pieceCount[W_KING] != 1
      || pieceCount[B_KING] != 1
      || attackers_to(square<KING>(~sideToMove)) & pieces(sideToMove))
      assert(0 && "pos_is_ok: Kings");

  if (   (pieces(PAWN) & (Rank1BB | Rank8BB))
      || pieceCount[W_PAWN] > 8
      || pieceCount[B_PAWN] > 8)
      assert(0 && "pos_is_ok: Pawns");

  if (   (pieces(WHITE) & pieces(BLACK))
      || (pieces(WHITE) | pieces(BLACK)) != pieces()
      || popcount(pieces(WHITE)) > 16
      || popcount(pieces(BLACK)) > 16)
      assert(0 && "pos_is_ok: Bitboards");

  for (PieceType p1 = PAWN; p1 <= KING; ++p1)
      for (PieceType p2 = PAWN; p2 <= KING; ++p2)
          if (p1 != p2 && (pieces(p1) & pieces(p2)))
              assert(0 && "pos_is_ok: Bitboards");


  for (Piece pc : Pieces)
      if (   pieceCount[pc] != popcount(pieces(color_of(pc), type_of(pc)))
          || pieceCount[pc] != std::count(board, board + SQUARE_NB, pc))
          assert(0 && "pos_is_ok: Pieces");

  for (Color c : { WHITE, BLACK })
      for (CastlingRights cr : {c & KING_SIDE, c & QUEEN_SIDE})
      {
          if (!can_castle(cr))
              continue;

          if (   piece_on(castlingRookSquare[cr]) != make_piece(c, ROOK)
              || castlingRightsMask[castlingRookSquare[cr]] != cr
              || (castlingRightsMask[square<KING>(c)] & cr) != cr)
              assert(0 && "pos_is_ok: Castling");
      }

  return true;
}

} // namespace Stockfish
