#include "telemetry.h"
#include <iostream>
#include <stdexcept>
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    using namespace telemetry;
    require(number("N/A") == -1 && number("nan") == -1 && number("8x") == -1, "Invalid values must be unavailable");
    require(split("\"game, test.exe\",42,10", ',')[0] == "game, test.exe", "Quoted CSV");
    require(busiestEngine({{"3D", 65}, {"Copy", 45}}) == 65, "Do not add independent GPU engines");
    require(busiestEngine({{"3D", 120}}) == 100, "Bound engine load");
    require(busiestEngine({}) == -1, "Missing GPU counter is not idle");
    Frames f;
    f.ingest("Application,ProcessID,SwapChainAddress,MsBetweenPresents", 100);
    f.ingest("\"game, test.exe\",42,0x1,10", 100);
    f.ingest("game.exe,42,0x1,20", 101);
    f.ingest("other.exe,99,0x1,2", 101);
    require(std::abs(f.take(42, 200) - 1000.0 / 15) < 0.001, "FPS is measured frames divided by elapsed time");
    require(f.take(42, 300) == -1, "No new frames must not reuse FPS");
    f.ingest("game.exe,42,0x1,10", 400);
    require(f.take(42, 3000) == -1, "Expired process data");
    f.ingest("game.exe,42,0x1,0", 3000);
    require(f.take(42, 3000) == -1, "Reject zero frame interval");
    std::cout << "Telemetry tests passed\n";
}
