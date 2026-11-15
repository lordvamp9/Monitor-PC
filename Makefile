CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -mwindows -static
LIBS = -luser32 -lkernel32 -lgdi32 -lshell32 -ladvapi32 -lpdh -ldxgi
all: hw_monitor.exe
hw_monitor.exe: main.cpp hardware_monitor.cpp hardware_monitor.h telemetry.h resource.o
	$(CXX) $(CXXFLAGS) main.cpp hardware_monitor.cpp resource.o -o $@ $(LIBS)
resource.o: resource.rc app.manifest icon.ico
	windres resource.rc -o resource.o
clean:
	powershell -NoProfile -Command "Remove-Item -ErrorAction SilentlyContinue *.o, hw_monitor.exe"
