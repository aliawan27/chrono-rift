CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -pthread

LIBS = -lncurses -lrt

TARGETS = arbiter_exe hip_exe asp_exe

all: clean $(TARGETS)
	@echo Build complete.

arbiter_exe: arbiter/arbiter.cpp
	$(CXX) $(CXXFLAGS) arbiter/*.cpp -o $@ $(LIBS)

hip_exe: hip/hip.cpp
	$(CXX) $(CXXFLAGS) hip/*.cpp -o $@ $(LIBS)

asp_exe: asp/asp.cpp
	$(CXX) $(CXXFLAGS) asp/*.cpp -o $@ $(LIBS)

clean:
	rm -f arbiter_exe hip_exe asp_exe

.PHONY: all clean