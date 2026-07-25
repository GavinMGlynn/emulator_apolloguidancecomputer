// Minimal host for AGCPlusPlus's Block II core: load a rope, tick N times,
// dump erasable cells. No sockets, no timer thread, no DSKY.
#include "block2/agc.hpp"
#include <fstream>
#include <iostream>
#include <vector>
using namespace agcplusplus;
using namespace agcplusplus::block2;

int main(int argc, char** argv) {
    if (argc < 3) { std::cerr << "usage: oracle <rope> <ticks> [octal-addr...]\n"; return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<word> rope;
    while (in) {
        unsigned char b[2];
        in.read(reinterpret_cast<char*>(b), 2);
        if (in.gcount() != 2) break;
        rope.push_back((word)((b[0] << 8) | b[1]));
    }
    std::vector<word> coredump;
    InitArguments cfg{};
    cfg.ignore_alarms = true;
    if (getenv("ORACLE_TRACE")) cfg.log_timepulse = true;
    Agc computer(rope, coredump, cfg);
    Agc::cpu.start();
    long ticks = std::stol(argv[2]);
    std::fprintf(stderr, "started\n");
    for (long i = 0; i < ticks; ++i) {
        Agc::cpu.tick();
        // Scaler ticking drives the CDU, which spawns a thread it never joins;
        // harmless in a long-running program, fatal in a short-lived host. The
        // divide comparison needs no counters, so leave the scaler stopped.
    }
    for (int i = 3; i < argc; ++i) {
        unsigned addr = std::stoul(argv[i], nullptr, 8);
        std::printf("E %04o %06o\n", addr, Agc::memory.read_erasable_word(addr));
    }
    return 0;
}

// Agc::run() starts the real timer loop (sockets, threads, wall clock). The
// oracle host drives cpu.tick() itself and never calls run(), but the symbol
// must resolve.
namespace agcplusplus::block2 { void Timer::start() {} }
