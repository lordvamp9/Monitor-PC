CC = gcc
CFLAGS = -Wall -Wextra -O2 -mwindows
LIBS = -luser32 -lkernel32 -lgdi32 -lcomctl32 -lmsimg32 -ldwmapi -lpdh

all: hw_monitor.exe

hw_monitor.exe: main.o hardware_monitor.o resource.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

main.o: main.c hardware_monitor.h
	$(CC) $(CFLAGS) -c $<

hardware_monitor.o: hardware_monitor.c hardware_monitor.h
	$(CC) $(CFLAGS) -c $<

resource.o: resource.rc app.manifest icon.ico
	windres resource.rc -o resource.o

clean:
	del /Q *.o hw_monitor.exe
