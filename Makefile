CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -pthread -I.

LIBS = -lsfml-graphics -lsfml-window -lsfml-audio -lsfml-network -lsfml-system -lrt
TARGETS = arbiter_exe hip_exe asp_exe

all: clean $(TARGETS)
	@echo Build complete.

arbiter_exe: arbiter/arbiter.cpp
	$(CXX) $(CXXFLAGS) arbiter/arbiter.cpp -o $@ $(LIBS)

hip_exe: hip/hip.cpp
	$(CXX) $(CXXFLAGS) hip/hip.cpp -o $@ $(LIBS)

asp_exe: asp/asp.cpp
	$(CXX) $(CXXFLAGS) asp/asp.cpp -o $@ $(LIBS)

clean:
	rm -f arbiter_exe hip_exe asp_exe

.PHONY: all clean