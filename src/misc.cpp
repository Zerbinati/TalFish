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

#ifdef _WIN32
#if _WIN32_WINNT < 0x0601
#undef  _WIN32_WINNT
#define _WIN32_WINNT 0x0601 // Force to include needed API prototypes
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "VersionHelpers.h"

// The needed Windows API for processor groups could be missed from old Windows
// versions, so instead of calling them directly (forcing the linker to resolve
// the calls at compile time), try to load them at runtime. To do this we need
// first to define the corresponding function pointers.
extern "C" {
using fun1_t = bool(*)(LOGICAL_PROCESSOR_RELATIONSHIP,
                       PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD);
using fun2_t = bool(*)(USHORT, PGROUP_AFFINITY);
using fun3_t = bool(*)(HANDLE, CONST GROUP_AFFINITY*, PGROUP_AFFINITY);
using fun4_t = bool(*)(USHORT, PGROUP_AFFINITY, USHORT, PUSHORT);
using fun5_t = WORD(*)();
using fun6_t = bool(*)(HANDLE, DWORD, PHANDLE);
using fun7_t = bool(*)(LPCSTR, LPCSTR, PLUID);
using fun8_t = bool(*)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
}
#endif

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>
#include <stdarg.h>
#include <bitset>
#include <cstdlib>
#include <regex>

#ifdef __GNUC__
#include <sys/stat.h> //for stat()
#endif

#ifdef _MSC_VER
#else
#include <sys/types.h>
#include <dirent.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#include <stdlib.h>
#include <sys/mman.h>
#endif

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__OpenBSD__) || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32)) || defined(__e2k__)
#define POSIXALIGNEDALLOC
#include <stdlib.h>
#endif

#include "misc.h"
#include "thread.h"

using namespace std;

namespace Stockfish {

namespace {

/// Version number. If Version is left empty, then compile date in the format
/// DD-MM-YY and show in engine_info.
const string Version = "v1.01";

bool LPMessage = false;

/// Our fancy logging facility. The trick here is to replace cin.rdbuf() and
/// cout.rdbuf() with two Tie objects that tie cin and cout to a file stream. We
/// can toggle the logging of std::cout and std:cin at runtime whilst preserving
/// usual I/O functionality, all without changing a single line of code!
/// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81

struct Tie: public streambuf { // MSVC requires split streambuf for cin and cout

  Tie(streambuf* b, streambuf* l) : buf(b), logBuf(l) {}

  int sync() override { return logBuf->pubsync(), buf->pubsync(); }
  int overflow(int c) override { return log(buf->sputc((char)c), "<< "); }
  int underflow() override { return buf->sgetc(); }
  int uflow() override { return log(buf->sbumpc(), ">> "); }

  streambuf *buf, *logBuf;

  int log(int c, const char* prefix) {

    static int last = '\n'; // Single log file

    if (last == '\n')
        logBuf->sputn(prefix, 3);

    return last = logBuf->sputc((char)c);
  }
};

class Logger {

  Logger() : in(cin.rdbuf(), file.rdbuf()), out(cout.rdbuf(), file.rdbuf()) {}
 ~Logger() { start(""); }

  ofstream file;
  Tie in, out;

public:
  static void start(const std::string& fname) {

    static Logger l;

    if (l.file.is_open())
    {
        cout.rdbuf(l.out.buf);
        cin.rdbuf(l.in.buf);
        l.file.close();
    }

    if (!fname.empty())
    {
        l.file.open(fname, ifstream::out);

        if (!l.file.is_open())
        {
            cerr << "Unable to open debug log file " << fname << endl;
            exit(EXIT_FAILURE);
        }

        cin.rdbuf(&l.in);
        cout.rdbuf(&l.out);
    }
  }
};

} // namespace

/// engine_info() returns the full name of the current Stockfish version. This
/// will be either "Stockfish <Tag> DD-MM-YY" (where DD-MM-YY is the date when
/// the program was compiled) or "Stockfish <Version>", depending on whether
/// Version is empty.

string engine_info(bool to_uci) {

  const string months("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");
  string month, day, year;
  stringstream ss, date(__DATE__); // From compiler, format is "Sep 21 2008"

  ss << "HumanMind " << Version << setfill('0');

  ss << (to_uci  ? "\nid author ": " by ")
     << "M.Z and the Stockfish developers (see AUTHORS file)";


  if (!to_uci)
  {
      date >> month >> day >> year;

      ss << "\n"
         << compiler_info()
         << "\nBuild date/time       : " << year << '-' << setw(2) << setfill('0') << month << '-' << setw(2) << setfill('0') << day << ' ' << __TIME__
         << "\n";
  }

  return ss.str();
}


/// compiler_info() returns a string trying to describe the compiler we use

std::string compiler_info() {

  #define make_version_string(major, minor, patch) stringify(major) "." stringify(minor) "." stringify(patch)

/// Predefined macros hell:
///
/// __GNUC__           Compiler is gcc, Clang or Intel on Linux
/// __INTEL_COMPILER   Compiler is Intel
/// _MSC_VER           Compiler is MSVC or Intel on Windows
/// _WIN32             Building on Windows (any)
/// _WIN64             Building on Windows 64 bit

  std::string compiler = "\nCompiled by           : ";

  #ifdef __clang__
     compiler += "clang++ ";
     compiler += make_version_string(__clang_major__, __clang_minor__, __clang_patchlevel__);
  #elif __INTEL_COMPILER
     compiler += "Intel compiler ";
     compiler += "(version ";
     compiler += stringify(__INTEL_COMPILER) " update " stringify(__INTEL_COMPILER_UPDATE);
     compiler += ")";
  #elif _MSC_VER
     compiler += "MSVC ";
     compiler += "(version ";
     compiler += stringify(_MSC_FULL_VER) "." stringify(_MSC_BUILD);
     compiler += ")";
  #elif defined(__e2k__) && defined(__LCC__)
    #define dot_ver2(n) \
      compiler += (char)'.'; \
      compiler += (char)('0' + (n) / 10); \
      compiler += (char)('0' + (n) % 10);

     compiler += "MCST LCC ";
     compiler += "(version ";
     compiler += std::to_string(__LCC__ / 100);
     dot_ver2(__LCC__ % 100)
     dot_ver2(__LCC_MINOR__)
     compiler += ")";
  #elif __GNUC__
     compiler += "g++ (GNUC) ";
     compiler += make_version_string(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
  #else
     compiler += "Unknown compiler ";
     compiler += "(unknown version)";
  #endif

  #if defined(__APPLE__)
     compiler += " on Apple";
  #elif defined(__CYGWIN__)
     compiler += " on Cygwin";
  #elif defined(__MINGW64__)
     compiler += " on MinGW64";
  #elif defined(__MINGW32__)
     compiler += " on MinGW32";
  #elif defined(__ANDROID__)
     compiler += " on Android";
  #elif defined(__linux__)
     compiler += " on Linux";
  #elif defined(_WIN64)
     compiler += " on Microsoft Windows 64-bit";
  #elif defined(_WIN32)
     compiler += " on Microsoft Windows 32-bit";
  #else
     compiler += " on unknown system";
  #endif

  compiler += "\nCompile settings      :";
  compiler += (Is64Bit ? " 64bit" : " 32bit");
  #if defined(USE_VNNI)
    compiler += " VNNI";
  #endif
  #if defined(USE_AVX512)
    compiler += " AVX512";
  #endif
  compiler += (HasPext ? " BMI2" : "");
  #if defined(USE_AVX2)
    compiler += " AVX2";
  #endif
  #if defined(USE_SSE41)
    compiler += " SSE41";
  #endif
  #if defined(USE_SSSE3)
    compiler += " SSSE3";
  #endif
  #if defined(USE_SSE2)
    compiler += " SSE2";
  #endif
  compiler += (HasPopCnt ? " POPCNT" : "");
  #if defined(USE_MMX)
    compiler += " MMX";
  #endif
  #if defined(USE_NEON)
    compiler += " NEON";
  #endif

  #if !defined(NDEBUG)
    compiler += " DEBUG";
  #endif

  compiler += "\n__VERSION__ macro expands to: ";
  #ifdef __VERSION__
     compiler += __VERSION__;
  #else
     compiler += "(undefined macro)";
  #endif
  compiler += "\n";

  return compiler;
}

string format_bytes(uint64_t bytes, int decimals)
{
    static const uint64_t _1KB = 1024;
    static const uint64_t _1MB = _1KB * 1024;
    static const uint64_t _1GB = _1MB * 1024;

    std::stringstream ss;

    if (bytes < _1KB)
        ss << bytes << " B";
    else if (bytes < _1MB)
        ss << std::fixed << std::setprecision(decimals) << ((double)bytes / _1KB) << "KB";
    else if (bytes < _1GB)
        ss << std::fixed << std::setprecision(decimals) << ((double)bytes / _1MB) << "MB";
    else
        ss << std::fixed << std::setprecision(decimals) << ((double)bytes / _1GB) << "GB";

    return ss.str();
}

void show_logo()
{
#if defined(_WIN32)
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    if (hConsole && !GetConsoleScreenBufferInfo(hConsole, &csbiInfo))
        hConsole = nullptr;

    if (hConsole)
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
#elif defined(__linux)
    cout << "\033[1;31m";
#endif

    cout <<
        R"(
HumanMind Personality Series 

)" << endl;

#if defined(_WIN32)
    if (hConsole)
        SetConsoleTextAttribute(hConsole, csbiInfo.wAttributes);
#elif defined(__linux)
    cout << "\033[0m";
#endif
}

namespace SysInfo
{
    namespace
    {
        uint32_t numaNodeCount = 0;
        uint32_t processorCoreCount = 0;
        uint32_t logicalProcessorCount = 0;
        uint32_t processorCacheSize[3] = { 0, 0, 0 };

        uint64_t totalMemory = 0;

        std::string osInfo;
        std::string cpuBrand;

        void init_hw_info()
        {
#if defined(_WIN32)
            typedef BOOL(WINAPI* GLPIEX)(LOGICAL_PROCESSOR_RELATIONSHIP, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD);
            static GLPIEX impGetLogicalProcessorInformationEx = (GLPIEX)(void (*)(void))GetProcAddress(GetModuleHandle("kernel32.dll"), "GetLogicalProcessorInformationEx");

            void* oldBuffer = nullptr;
            DWORD len = 0;
            DWORD offset = 0;

            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* bufferEx = nullptr;
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer = nullptr;
            GROUP_AFFINITY* nodeGroupMask = nullptr;
            ULONGLONG* nodeMask = nullptr;

            auto release_memory = [&]()
            {
                if (bufferEx) { free(bufferEx);      bufferEx = nullptr; }
                if (buffer) { free(buffer);        buffer = nullptr; }
                if (oldBuffer) { free(oldBuffer);     oldBuffer = nullptr; }
                if (nodeGroupMask) { free(nodeGroupMask); nodeGroupMask = nullptr; }
                if (nodeMask) { free(nodeMask);      nodeMask = nullptr; }
            };

            // Use windows processor groups?
            if (impGetLogicalProcessorInformationEx)
            {
                // Get array of node and core data
                while (true)
                {
                    if (impGetLogicalProcessorInformationEx(RelationAll, bufferEx, &len))
                        break;

                    //Save old buffer in case realloc fails
                    oldBuffer = bufferEx;

                    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
                        bufferEx = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)realloc(bufferEx, len);
                    else
                        bufferEx = nullptr; //Old value already stored in 'oldBuffer'

                    if (!bufferEx)
                    {
                        release_memory();
                        return;
                    }
                }

                //Prepare
                size_t maxNodes = 16;

                //Allocate memory
                nodeGroupMask = (GROUP_AFFINITY*)malloc(maxNodes * sizeof(GROUP_AFFINITY));
                if (!nodeGroupMask)
                {
                    release_memory();
                    return;
                }

                //Numa nodes loop
                SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* ptr = bufferEx;
                while (offset < len && offset + ptr->Size <= len)
                {
                    if (ptr->Relationship == RelationNumaNode)
                    {
                        if (numaNodeCount == maxNodes)
                        {
                            maxNodes += 16;

                            oldBuffer = nodeGroupMask;
                            nodeGroupMask = (GROUP_AFFINITY*)realloc(nodeGroupMask, maxNodes * sizeof(GROUP_AFFINITY));
                            if (!nodeGroupMask)
                            {
                                release_memory();
                                return;
                            }
                        }

                        nodeGroupMask[numaNodeCount] = ptr->NumaNode.GroupMask;
                        numaNodeCount++;
                    }

                    offset += ptr->Size;
                    ptr = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(((char*)ptr) + ptr->Size);
                }

                //Physical/Logical cores loop and cache
                ptr = bufferEx;
                offset = 0;
                while (offset < len && offset + ptr->Size <= len)
                {
                    if (ptr->Relationship == RelationProcessorCore)
                    {
                        // Loop through nodes to find one with matching group number and intersecting mask
                        for (size_t i = 0; i < numaNodeCount; i++)
                        {
                            if (nodeGroupMask[i].Group == ptr->Processor.GroupMask[0].Group && (nodeGroupMask[i].Mask & ptr->Processor.GroupMask[0].Mask))
                            {
                                ++processorCoreCount;
                                logicalProcessorCount += ptr->Processor.Flags == LTP_PC_SMT ? 2 : 1;
                            }
                        }
                    }
                    else if (ptr->Relationship == RelationCache)
                    {
                        switch (ptr->Cache.Level)
                        {
                        case 1:
                        case 2:
                        case 3:
                            processorCacheSize[ptr->Cache.Level - 1] += ptr->Cache.CacheSize;
                            break;

                        default:
                            assert(false);
                            break;
                        }
                    }

                    offset += ptr->Size;
                    ptr = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(((char*)ptr) + ptr->Size);
                }
            }
            else // Use windows but not its processor groups
            {
                while (true)
                {
                    if (GetLogicalProcessorInformation(buffer, &len))
                        break;

                    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
                    {
                        //Save old buffer in case realloc fails
                        oldBuffer = buffer;

                        buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)realloc(buffer, len);
                    }
                    else
                    {
                        buffer = nullptr; //Old value already stored in 'oldBuffer'
                    }

                    if (!buffer)
                    {
                        release_memory();
                        return;
                    }
                }

                //Prepare
                size_t maxNodes = 16;

                //Allocate memory
                nodeMask = (ULONGLONG*)malloc(maxNodes * sizeof(ULONGLONG));
                if (!nodeMask)
                {
                    release_memory();
                    return;
                }

                //Numa nodes loop
                SYSTEM_LOGICAL_PROCESSOR_INFORMATION* ptr = buffer;
                while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= len)
                {
                    if (ptr->Relationship == RelationNumaNode)
                    {
                        if (numaNodeCount == maxNodes)
                        {
                            maxNodes += 16;

                            oldBuffer = nodeMask;
                            nodeMask = (ULONGLONG*)realloc(nodeMask, maxNodes * sizeof(ULONGLONG));
                            if (!nodeMask)
                            {
                                release_memory();
                                return;
                            }
                        }

                        nodeMask[numaNodeCount] = ptr->ProcessorMask;
                        numaNodeCount++;
                    }

                    offset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
                    ptr++;
                }

                //Physical/Logical cores and cache loop
                ptr = buffer;
                offset = 0;
                while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= len)
                {
                    if (ptr->Relationship == RelationProcessorCore)
                    {
                        // Loop through nodes to find one with intersecting mask
                        for (size_t i = 0; i < numaNodeCount; i++)
                        {
                            if (nodeMask[i] & ptr->ProcessorMask)
                            {
                                ++processorCoreCount;
                                logicalProcessorCount += ptr->ProcessorCore.Flags == 1 ? 2 : 1;
                            }
                        }
                    }
                    else if (ptr->Relationship == RelationCache)
                    {
                        switch (ptr->Cache.Level)
                        {
                        case 1:
                        case 2:
                        case 3:
                            processorCacheSize[ptr->Cache.Level - 1] += ptr->Cache.Size;
                            break;

                        default:
                            assert(false);
                            break;
                        }
                    }

                    offset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
                    ptr++;
                }
            }

            release_memory();
#elif defined(__linux__)
            const int max_buffer = 1024;
            char buffer[max_buffer];

            //Run 'lscpu' to get CPU information
            FILE* stream = popen("lscpu 2>&1", "r");
            if (!stream)
                return;

            string cpuData;
            while (!feof(stream))
            {
                if (fgets(buffer, max_buffer, stream) != NULL)
                    cpuData.append(buffer);
            }

            pclose(stream);

            if (cpuData.empty())
                return;

            auto parse_unit = [](const char *s)
            {
                if (strcasecmp(s, "KB") == 0 || strcasecmp(s, "KiB") == 0)
                    return 1024;

                if (strcasecmp(s, "MB") == 0 || strcasecmp(s, "MiB") == 0)
                    return 1024 * 1024;

                if (strcasecmp(s, "GB") == 0 || strcasecmp(s, "GiB") == 0)
                    return 1024 * 1024 * 1024;

                return 0;
            };

            //Find required data
            static regex rgxNumberOfCpus("^CPU\\(s\\):\\s*(\\d*)$");
            static regex rgxThreadsPerCode("^Thread\\(s\\) per core:\\s*(\\d*)$");
            static regex rgxNumaNodes("NUMA node\\(s\\):\\s*(\\d*)$");
            static regex rgxL1dCache("^L1d cache:\\s*(\\d*) (.*)$");
            static regex rgxL1iCache("^L1i cache:\\s*(\\d*) (.*)$");
            static regex rgxL2Cache("^L2 cache:\\s*(\\d*) (.*)$");
            static regex rgxL3Cache("^L3 cache:\\s*(\\d*) (.*)$");
            static regex rgxCpuBrand("^Model name:\\s*(.*)$");

            int tempThreadsPerCode = 0;

            std::stringstream ss(cpuData);
            std::string line;
            while (std::getline(ss, line))
            {
                smatch match;
                if (regex_search(line, match, rgxNumberOfCpus))
                {
                    processorCoreCount = (uint32_t)atoi(match[1].str().c_str());
                }
                else if(regex_search(line, match, rgxThreadsPerCode))
                {
                    tempThreadsPerCode = atoi(match[1].str().c_str());
                }
                else if (regex_search(line, match, rgxL1dCache) || regex_search(line, match, rgxL1iCache))
                {
                    processorCacheSize[0] += (uint32_t)atoi(match[1].str().c_str()) * parse_unit(match[2].str().c_str());
                }
                else if (regex_search(line, match, rgxL2Cache))
                {
                    processorCacheSize[1] += (uint32_t)atoi(match[1].str().c_str()) * parse_unit(match[2].str().c_str());
                }
                else if (regex_search(line, match, rgxL3Cache))
                {
                    processorCacheSize[2] += (uint32_t)atoi(match[1].str().c_str()) * parse_unit(match[2].str().c_str());
                }
                else if (regex_search(line, match, rgxNumaNodes))
                {
                    numaNodeCount = (uint32_t)atoi(match[1].str().c_str());
                }
                else if (regex_search(line, match, rgxCpuBrand))
                {
                    cpuBrand = match[1].str();
                }
            }

            if (processorCoreCount)
            {
                if (tempThreadsPerCode)
                    logicalProcessorCount = processorCoreCount * tempThreadsPerCode;
                else
                    logicalProcessorCount = processorCoreCount;
            }
#endif
        }

        void init_processor_brand()
        {
#if defined(_WIN32)
            HKEY hKey = HKEY_LOCAL_MACHINE;
            TCHAR Data[1024];

            //Clear buffer
            ZeroMemory(Data, sizeof(Data));

            //Open target registry key
            DWORD buffersize = sizeof(Data) / sizeof(Data[0]);
            LONG result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("Hardware\\Description\\System\\CentralProcessor\\0\\"), 0, KEY_READ, &hKey);
            if (result == ERROR_SUCCESS)
            {
                //Get value of 'Key = ProcessorNameString' which is the processor name
                result = RegQueryValueEx(hKey, TEXT("ProcessorNameString"), NULL, NULL, (LPBYTE)&Data, &buffersize);

                //If we failed to retrieve the processor name, then we retrun "N/A"
                if (result == ERROR_SUCCESS)
                {
                    cpuBrand = Data;
                }

                // Close the Registry Key
                RegCloseKey(hKey);
            }
#elif defined(__linux__)
            //Nothing to do here since CPU brand is read when init_hw_info() is called
#endif
        }

        void init_os_info()
        {
#if defined(_WIN32)
            {
                InitVersion();

                if (IsWindowsXPOrGreater())
                {
                    if (IsWindowsXPSP1OrGreater())
                    {
                        if (IsWindowsXPSP2OrGreater())
                        {
                            if (IsWindowsXPSP3OrGreater())
                            {
                                if (IsWindowsVistaOrGreater())
                                {
                                    if (IsWindowsVistaSP1OrGreater())
                                    {
                                        if (IsWindowsVistaSP2OrGreater())
                                        {
                                            if (IsWindows7OrGreater())
                                            {
                                                if (IsWindows7SP1OrGreater())
                                                {
                                                    if (IsWindows8OrGreater())
                                                    {
                                                        if (IsWindows8Point1OrGreater())
                                                        {
                                                            if (IsWindows10OrGreater())
                                                            {
                                                                osInfo = "Windows 10";
                                                            }
                                                            else
                                                            {
                                                                osInfo = "Windows 8.1";
                                                            }
                                                        }
                                                        else
                                                        {
                                                            osInfo = "Windows 8";
                                                        }
                                                    }
                                                    else
                                                    {
                                                        osInfo = "Windows 7 SP1";
                                                    }
                                                }
                                                else
                                                {
                                                    osInfo = "Windows 7";
                                                }
                                            }
                                            else
                                            {
                                                osInfo = "Vista SP2";
                                            }
                                        }
                                        else
                                        {
                                            osInfo = "Vista SP1";
                                        }
                                    }
                                    else
                                    {
                                        osInfo = "Vista";
                                    }
                                }
                                else
                                {
                                    osInfo = "XP SP3";
                                }
                            }
                            else
                            {
                                osInfo = "XP SP2";
                            }
                        }
                        else
                        {
                            osInfo = "XP SP1";
                        }
                    }
                    else
                    {
                        osInfo = "XP";
                    }
                }

                if (IsWindowsServer())
                {
                    osInfo += " Server";
                }
                else
                {
                    osInfo += " Client";
                }

                osInfo += " Or Greater";
            }
#elif defined(__linux__)
            ifstream distribInfp("/etc/lsb-release");
            if (!distribInfp.is_open())
                return;

            static regex rgxDistributionID("^DISTRIB_ID=(.*)$");
            static regex rgxDistribRelease("^DISTRIB_RELEASE=(.*)$");
            static regex rgxDistribDescription("^DISTRIB_DESCRIPTION=\"(.*)\"$");

            std::string distribID;
            std::string distribRelease;
            std::string distribDescription;

            std::string line;
            while (std::getline(distribInfp, line))
            {
                smatch match;
                if (regex_search(line, match, rgxDistributionID))
                {
                    distribID = match[1].str();
                }
                else if (regex_search(line, match, rgxDistribRelease))
                {
                    distribRelease = match[1].str();
                }
                else if (regex_search(line, match, rgxDistribDescription))
                {
                    distribDescription = match[1].str();

                    //If we have the distrib description then we are good to go
                    break;
                }
            }

            if (!distribDescription.empty())
                osInfo = distribDescription;
            else if (!distribID.empty() && !distribRelease.empty())
                osInfo = distribID + " " + distribRelease;
#endif
        }

        void init_mem_info()
        {
#if defined(_WIN32)
            ULONGLONG totMem;
            if (GetPhysicallyInstalledSystemMemory(&totMem))
            {
                //Returned value is in KB
                totalMemory = totMem * 1024;
            }
            else
            {
                MEMORYSTATUSEX statex;
                statex.dwLength = sizeof(statex);

                if (GlobalMemoryStatusEx(&statex))
                    totalMemory = statex.ullTotalPhys;
                else
                    totalMemory = 0;
            }
#elif defined(__linux__)
            ifstream memInfo("/proc/meminfo");
            if (!memInfo.is_open())
                return;

            static regex rgxMemTotal("^MemTotal:\\s*(\\d*) (.*)$$");

            std::string line;
            while (std::getline(memInfo, line))
            {
                smatch match;
                if (regex_search(line, match, rgxMemTotal))
                {
                    totalMemory = strtoull(match[1].str().c_str(), nullptr, 10);
                    if (strcasecmp(match[2].str().c_str(), "KB") == 0 || strcasecmp(match[2].str().c_str(), "KiB") == 0)
                        totalMemory *= 1024;
                    else if (strcasecmp(match[2].str().c_str(), "MB") == 0 || strcasecmp(match[2].str().c_str(), "MiB") == 0)
                        totalMemory *= 1024 * 1024;
                    else if (strcasecmp(match[2].str().c_str(), "GB") == 0 || strcasecmp(match[2].str().c_str(), "GiB") == 0)
                        totalMemory *= 1024 * 1024 * 1024;

                    //We found what we are looking for
                    break;
                }
            }
#endif
        }
    }

    void init()
    {
        init_hw_info();
        init_processor_brand();
        init_os_info();
        init_mem_info();
    }

    const string numa_nodes()
    {
        if (!numaNodeCount)
            return "N/A";

        return to_string(numaNodeCount);
    }

    const string physical_cores()
    {
        if (!processorCoreCount)
            return "N/A";

        return to_string(processorCoreCount);
    }

    const string logical_cores()
    {
        if (!logicalProcessorCount)
            return "N/A";

        return to_string(logicalProcessorCount);
    }

    const string is_hyper_threading()
    {
        if (!logicalProcessorCount || !processorCoreCount)
            return "N/A";

        return processorCoreCount == logicalProcessorCount ? "No" : "Yes";
    }

    const string cache_info(int idx)
    {
        if (!processorCacheSize[idx])
            return "N/A";

        return format_bytes(processorCacheSize[idx], 0);
    }

    const string os_info()
    {
        if (osInfo.empty())
            return "N/A";

        return osInfo;
    }

    const string processor_brand()
    {
        if (cpuBrand.empty())
            return "N/A";

        return cpuBrand;
    }

    const string total_memory()
    {
        if (totalMemory == 0)
            return "N/A";

        return format_bytes(totalMemory, 0);
    }
}

/// Debug functions used mainly to collect run-time statistics
constexpr int MaxDebugSlots = 32;

namespace {

template<size_t N>
struct DebugInfo {
    std::atomic<int64_t> data[N] = { 0 };

    constexpr inline std::atomic<int64_t>& operator[](int index) { return data[index]; }
};

DebugInfo<2> hit[MaxDebugSlots];
DebugInfo<2> mean[MaxDebugSlots];
DebugInfo<3> stdev[MaxDebugSlots];
DebugInfo<6> correl[MaxDebugSlots];

}  // namespace

void dbg_hit_on(bool cond, int slot) {

    ++hit[slot][0];
    if (cond)
        ++hit[slot][1];
}

void dbg_mean_of(int64_t value, int slot) {

    ++mean[slot][0];
    mean[slot][1] += value;
}

void dbg_stdev_of(int64_t value, int slot) {

    ++stdev[slot][0];
    stdev[slot][1] += value;
    stdev[slot][2] += value * value;
}

void dbg_correl_of(int64_t value1, int64_t value2, int slot) {

    ++correl[slot][0];
    correl[slot][1] += value1;
    correl[slot][2] += value1 * value1;
    correl[slot][3] += value2;
    correl[slot][4] += value2 * value2;
    correl[slot][5] += value1 * value2;
}

void dbg_print() {

    int64_t n;
    auto E   = [&n](int64_t x) { return double(x) / n; };
    auto sqr = [](double x) { return x * x; };

    for (int i = 0; i < MaxDebugSlots; ++i)
        if ((n = hit[i][0]))
            std::cerr << "Hit #" << i
                      << ": Total " << n << " Hits " << hit[i][1]
                      << " Hit Rate (%) " << 100.0 * E(hit[i][1])
                      << std::endl;

    for (int i = 0; i < MaxDebugSlots; ++i)
        if ((n = mean[i][0]))
        {
            std::cerr << "Mean #" << i
                      << ": Total " << n << " Mean " << E(mean[i][1])
                      << std::endl;
        }

    for (int i = 0; i < MaxDebugSlots; ++i)
        if ((n = stdev[i][0]))
        {
            double r = sqrtl(E(stdev[i][2]) - sqr(E(stdev[i][1])));
            std::cerr << "Stdev #" << i
                      << ": Total " << n << " Stdev " << r
                      << std::endl;
        }

    for (int i = 0; i < MaxDebugSlots; ++i)
        if ((n = correl[i][0]))
        {
            double r = (E(correl[i][5]) - E(correl[i][1]) * E(correl[i][3]))
                       / (  sqrtl(E(correl[i][2]) - sqr(E(correl[i][1])))
                          * sqrtl(E(correl[i][4]) - sqr(E(correl[i][3]))));
            std::cerr << "Correl. #" << i
                      << ": Total " << n << " Coefficient " << r
                      << std::endl;
        }
}


/// Used to serialize access to std::cout to avoid multiple threads writing at
/// the same time.

std::ostream& operator<<(std::ostream& os, SyncCout sc) {

  static std::mutex m;

  if (sc == IO_LOCK)
      m.lock();

  if (sc == IO_UNLOCK)
      m.unlock();

  return os;
}


/// Trampoline helper to avoid moving Logger to misc.h
void start_logger(const std::string& fname) { Logger::start(fname); }


/// prefetch() preloads the given address in L1/L2 cache. This is a non-blocking
/// function that doesn't stall the CPU waiting for data to be loaded from memory,
/// which can be quite slow.
#ifdef NO_PREFETCH

void prefetch(void*) {}

#else

void prefetch(void* addr) {

#  if defined(__INTEL_COMPILER)
   // This hack prevents prefetches from being optimized away by
   // Intel compiler. Both MSVC and gcc seem not be affected by this.
   __asm__ ("");
#  endif

#  if defined(__INTEL_COMPILER) || defined(_MSC_VER)
  _mm_prefetch((char*)addr, _MM_HINT_T0);
#  else
  __builtin_prefetch(addr);
#  endif
}

#endif


/// std_aligned_alloc() is our wrapper for systems where the c++17 implementation
/// does not guarantee the availability of aligned_alloc(). Memory allocated with
/// std_aligned_alloc() must be freed with std_aligned_free().

void* std_aligned_alloc(size_t alignment, size_t size) {

#if defined(POSIXALIGNEDALLOC)
  void *mem;
  return posix_memalign(&mem, alignment, size) ? nullptr : mem;
#elif defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64)
  return _mm_malloc(size, alignment);
#elif defined(_WIN32)
  return _aligned_malloc(size, alignment);
#else
  return std::aligned_alloc(alignment, size);
#endif
}

void std_aligned_free(void* ptr) {

#if defined(POSIXALIGNEDALLOC)
  free(ptr);
#elif defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64)
  _mm_free(ptr);
#elif defined(_WIN32)
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}

/// aligned_large_pages_alloc() will return suitably aligned memory, if possible using large pages.

#if defined(_WIN32)

static void* aligned_large_pages_alloc_windows([[maybe_unused]] size_t allocSize) {

  #if !defined(_WIN64)
    return nullptr;
  #else

  HANDLE hProcessToken { };
  LUID luid { };
  void* mem = nullptr;

  const size_t largePageSize = GetLargePageMinimum();
  if (!largePageSize)
      return nullptr;

  // Dynamically link OpenProcessToken, LookupPrivilegeValue and AdjustTokenPrivileges

  HMODULE hAdvapi32 = GetModuleHandle(TEXT("advapi32.dll"));

  if (!hAdvapi32)
      hAdvapi32 = LoadLibrary(TEXT("advapi32.dll"));

  auto fun6 = (fun6_t)(void(*)())GetProcAddress(hAdvapi32, "OpenProcessToken");
  if (!fun6)
      return nullptr;
  auto fun7 = (fun7_t)(void(*)())GetProcAddress(hAdvapi32, "LookupPrivilegeValueA");
  if (!fun7)
      return nullptr;
  auto fun8 = (fun8_t)(void(*)())GetProcAddress(hAdvapi32, "AdjustTokenPrivileges");
  if (!fun8)
      return nullptr;

  // We need SeLockMemoryPrivilege, so try to enable it for the process
  if (!fun6( // OpenProcessToken()
      GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hProcessToken))
          return nullptr;

  if (fun7( // LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &luid)
      nullptr, "SeLockMemoryPrivilege", &luid))
  {
      TOKEN_PRIVILEGES tp { };
      TOKEN_PRIVILEGES prevTp { };
      DWORD prevTpLen = 0;

      tp.PrivilegeCount = 1;
      tp.Privileges[0].Luid = luid;
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

      // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
      // we still need to query GetLastError() to ensure that the privileges were actually obtained.
      if (fun8( // AdjustTokenPrivileges()
              hProcessToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &prevTp, &prevTpLen) &&
          GetLastError() == ERROR_SUCCESS)
      {
          // Round up size to full pages and allocate
          allocSize = (allocSize + largePageSize - 1) & ~size_t(largePageSize - 1);
          mem = VirtualAlloc(
              nullptr, allocSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);

          // Privilege no longer needed, restore previous state
          fun8( // AdjustTokenPrivileges ()
              hProcessToken, FALSE, &prevTp, 0, nullptr, nullptr);
      }
  }

  CloseHandle(hProcessToken);

  return mem;

  #endif
}

void* aligned_large_pages_alloc(size_t allocSize) {

  // Try to allocate large pages
  void* mem = aligned_large_pages_alloc_windows(allocSize);

  // Fall back to regular, page aligned, allocation if necessary
  if (!mem)
     {
      mem = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	    if (LPMessage == false)
		    {
		    cout << "Large Memory Pages    : not available" << endl << endl;
		    LPMessage = true;
            }
        }
  else
        {
	    if (LPMessage == false)
		    {
		    cout << "Large Memory Pages    : available" << endl << endl;
		    LPMessage = true;
            }
	    }
  return mem;
}

#else

void* aligned_large_pages_alloc(size_t allocSize) {

#if defined(__linux__)
  constexpr size_t alignment = 2 * 1024 * 1024; // assumed 2MB page size
#else
  constexpr size_t alignment = 4096; // assumed small page size
#endif

  // round up to multiples of alignment
  size_t size = ((allocSize + alignment - 1) / alignment) * alignment;
  void *mem = std_aligned_alloc(alignment, size);
#if defined(MADV_HUGEPAGE)
  madvise(mem, size, MADV_HUGEPAGE);
#endif
  return mem;
}

#endif


/// aligned_large_pages_free() will free the previously allocated ttmem

#if defined(_WIN32)

void aligned_large_pages_free(void* mem) {

  if (mem && !VirtualFree(mem, 0, MEM_RELEASE))
  {
      DWORD err = GetLastError();
      std::cerr << "Failed to free large page memory. Error code: 0x"
                << std::hex << err
                << std::dec << std::endl;
      exit(EXIT_FAILURE);
  }
}

#else

void aligned_large_pages_free(void *mem) {
  std_aligned_free(mem);
}

#endif


namespace WinProcGroup {

#ifndef _WIN32

void bindThisThread(size_t) {}

#else

/// best_node() retrieves logical processor information using Windows specific
/// API and returns the best node id for the thread with index idx. Original
/// code from Texel by Peter Österlund.

static int best_node(size_t idx) {

  int threads = 0;
  int nodes = 0;
  int cores = 0;
  DWORD returnLength = 0;
  DWORD byteOffset = 0;

  // Early exit if the needed API is not available at runtime
  HMODULE k32 = GetModuleHandle(TEXT("Kernel32.dll"));
  auto fun1 = (fun1_t)(void(*)())GetProcAddress(k32, "GetLogicalProcessorInformationEx");
  if (!fun1)
      return -1;

  // First call to GetLogicalProcessorInformationEx() to get returnLength.
  // We expect the call to fail due to null buffer.
  if (fun1(RelationAll, nullptr, &returnLength))
      return -1;

  // Once we know returnLength, allocate the buffer
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *buffer, *ptr;
  ptr = buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)malloc(returnLength);

  // Second call to GetLogicalProcessorInformationEx(), now we expect to succeed
  if (!fun1(RelationAll, buffer, &returnLength))
  {
      free(buffer);
      return -1;
  }

  while (byteOffset < returnLength)
  {
      if (ptr->Relationship == RelationNumaNode)
          nodes++;

      else if (ptr->Relationship == RelationProcessorCore)
      {
          cores++;
          threads += (ptr->Processor.Flags == LTP_PC_SMT) ? 2 : 1;
      }

      assert(ptr->Size);
      byteOffset += ptr->Size;
      ptr = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(((char*)ptr) + ptr->Size);
  }

  free(buffer);

  std::vector<int> groups;

  // Run as many threads as possible on the same node until core limit is
  // reached, then move on filling the next node.
  for (int n = 0; n < nodes; n++)
      for (int i = 0; i < cores / nodes; i++)
          groups.push_back(n);

  // In case a core has more than one logical processor (we assume 2) and we
  // have still threads to allocate, then spread them evenly across available
  // nodes.
  for (int t = 0; t < threads - cores; t++)
      groups.push_back(t % nodes);

  // If we still have more threads than the total number of logical processors
  // then return -1 and let the OS to decide what to do.
  return idx < groups.size() ? groups[idx] : -1;
}


/// bindThisThread() set the group affinity of the current thread

void bindThisThread(size_t idx) {

  // Use only local variables to be thread-safe
  int node = best_node(idx);

  if (node == -1)
      return;

  // Early exit if the needed API are not available at runtime
  HMODULE k32 = GetModuleHandle(TEXT("Kernel32.dll"));
  auto fun2 = (fun2_t)(void(*)())GetProcAddress(k32, "GetNumaNodeProcessorMaskEx");
  auto fun3 = (fun3_t)(void(*)())GetProcAddress(k32, "SetThreadGroupAffinity");
  auto fun4 = (fun4_t)(void(*)())GetProcAddress(k32, "GetNumaNodeProcessorMask2");
  auto fun5 = (fun5_t)(void(*)())GetProcAddress(k32, "GetMaximumProcessorGroupCount");

  if (!fun2 || !fun3)
      return;

  if (!fun4 || !fun5)
  {
      GROUP_AFFINITY affinity;
      if (fun2(node, &affinity))                                                 // GetNumaNodeProcessorMaskEx
      {
          fun3(GetCurrentThread(), &affinity, nullptr);                          // SetThreadGroupAffinity
          sync_cout << "info string Binding thread " << idx << " to node " << node << sync_endl;
      }
  }
  else
  {
      // If a numa node has more than one processor group, we assume they are
      // sized equal and we spread threads evenly across the groups.
      USHORT elements, returnedElements;
      elements = fun5();                                                         // GetMaximumProcessorGroupCount
      GROUP_AFFINITY *affinity = (GROUP_AFFINITY*)malloc(elements * sizeof(GROUP_AFFINITY));
      if (fun4(node, affinity, elements, &returnedElements))                     // GetNumaNodeProcessorMask2
          fun3(GetCurrentThread(), &affinity[idx % returnedElements], nullptr);  // SetThreadGroupAffinity
      free(affinity);
  }
}

#endif

} // namespace WinProcGroup

#ifdef _WIN32
#include <direct.h>
#define GETCWD _getcwd
#else
#include <unistd.h>
#define GETCWD getcwd
#endif

namespace CommandLine {

string argv0;            // path+name of the executable binary, as given by argv[0]
string binaryDirectory;  // path of the executable directory
string workingDirectory; // path of the working directory

void init([[maybe_unused]] int argc, char* argv[]) {
    string pathSeparator;

    // extract the path+name of the executable binary
    argv0 = argv[0];

#ifdef _WIN32
    pathSeparator = "\\";
  #ifdef _MSC_VER
    // Under windows argv[0] may not have the extension. Also _get_pgmptr() had
    // issues in some windows 10 versions, so check returned values carefully.
    char* pgmptr = nullptr;
    if (!_get_pgmptr(&pgmptr) && pgmptr != nullptr && *pgmptr)
        argv0 = pgmptr;
  #endif
#else
    pathSeparator = "/";
#endif

    // extract the working directory
    workingDirectory = "";
    char buff[40000];
    char* cwd = GETCWD(buff, 40000);
    if (cwd)
        workingDirectory = cwd;

    // extract the binary directory path from argv0
    binaryDirectory = argv0;
    size_t pos = binaryDirectory.find_last_of("\\/");
    if (pos == std::string::npos)
        binaryDirectory = "." + pathSeparator;
    else
        binaryDirectory.resize(pos + 1);

    // pattern replacement: "./" at the start of path is replaced by the working directory
    if (binaryDirectory.find("." + pathSeparator) == 0)
        binaryDirectory.replace(0, 1, workingDirectory);

    binaryDirectory = Utility::fix_path(binaryDirectory);
    workingDirectory = Utility::fix_path(workingDirectory);

}


} // namespace CommandLine

namespace Utility
{
    string myFolder;

    void init(const char* arg0)
    {
        string s = arg0;
        size_t i = s.find_last_of(DirectorySeparator);
        if (i != string::npos)
            myFolder = s.substr(0, i);
    }

    bool file_exists(const string& filename)
    {
        struct stat info;
        if (stat(filename.c_str(), &info) == 0)
            return (info.st_mode & S_IFREG) == S_IFREG;

        return false;
    }

    bool is_game_decided(const Position& pos, Value lastScore)
    {
        //Assume game is decided if game ply is above 200
        if (pos.game_ply() > 200)
            return true;

        //Assume game is decided if |last score| is above 2.5 Pawn
        if (lastScore != VALUE_NONE && std::abs(lastScore) > PawnValueEg * 5 / 2)
            return true;

        //Assume game is decided if |last score| is below 0.25 Pawn and game ply is above 120
        if (pos.game_ply() > 120 && lastScore < PawnValueEg / 4)
            return true;

        //Assume game is decided if remaining pieces is less than 9
        if (pos.count<ALL_PIECES>() < 9)
            return true;

        //Assume game is not decided!
        return false;
    }

    string unquote(const string& s)
    {
        string s1 = s;

        if (s1.size() > 2)
        {
            if ((s1.front() == '\"' && s1.back() == '\"') || (s1.front() == '\'' && s1.back() == '\''))
            {
                s1 = s1.substr(1, s1.size() - 2);
            }
        }

        return s1;
    }

    bool is_empty_filename(const string &fn)
    {
        if (fn.empty())
            return true;

        static string Empty = EMPTY;
        return equal(
            fn.begin(), fn.end(),
            Empty.begin(), Empty.end(),
            [](char a, char b) { return tolower(a) == tolower(b); });
    }

    string fix_path(const string& p)
    {
        if (is_empty_filename(p))
            return p;

        string p1 = unquote(p);
        replace(p1.begin(), p1.end(), ReverseDirectorySeparator, DirectorySeparator);

        return p1;
    }

    string combine_path(const string& p1, const string& p2)
    {
        //We don't expect the first part of the path to be empty!
        assert(is_empty_filename(p1) == false);

        if (is_empty_filename(p2))
            return p2;

        string p;
        if (p1.back() == DirectorySeparator || p1.back() == ReverseDirectorySeparator)
            p = p1 + p2;
        else
            p = p1 + DirectorySeparator + p2;

        return fix_path(p);
    }

    string map_path(const string& p)
    {
        if (is_empty_filename(p))
            return p;

        string p2 = fix_path(p);

        //Make sure we can map this path
        if (p2.find(DirectorySeparator) == string::npos)
            p2 = combine_path(CommandLine::binaryDirectory, p);

        return p2;
    }

    size_t get_file_size(const string& f)
    {
        if(is_empty_filename(f))
            return (size_t)-1;

        ifstream in(map_path(f), ifstream::ate | ifstream::binary);
        if (!in.is_open())
            return (size_t)-1;

        return (size_t)in.tellg();
    }

    bool is_same_file(const string& f1, const string& f2)
    {
        return map_path(f1) == map_path(f2);
    }

    string format_bytes(uint64_t bytes, int decimals)
    {
        static const uint64_t KB = 1024;
        static const uint64_t MB = KB * 1024;
        static const uint64_t GB = MB * 1024;
        static const uint64_t TB = GB * 1024;

        stringstream ss;

        if (bytes < KB)
            ss << bytes << " B";
        else if (bytes < MB)
            ss << fixed << setprecision(decimals) << ((double)bytes / KB) << "KB";
        else if (bytes < GB)
            ss << fixed << setprecision(decimals) << ((double)bytes / MB) << "MB";
        else if (bytes < TB)
            ss << fixed << setprecision(decimals) << ((double)bytes / GB) << "GB";
        else
            ss << fixed << setprecision(decimals) << ((double)bytes / TB) << "TB";

        return ss.str();
    }

    //Code is an `edited` version of: https://stackoverflow.com/a/49812018
    string format_string(const char* const fmt, ...)
    {
        //Initialize use of the variable arguments
        va_list vaArgs;
        va_start(vaArgs, fmt);

        //Acquire the required string size
        va_start(vaArgs, fmt);
        int len = vsnprintf(nullptr, 0, fmt, vaArgs);
        va_end(vaArgs);

        
        //Allocate enough buffer and format
        vector<char> v(len + 1);
        
        va_start(vaArgs, fmt);
        vsnprintf(v.data(), v.size(), fmt, vaArgs);
        va_end(vaArgs);

        return string(v.data(), len);
    }
} // namespace Utility

} // namespace Stockfish
