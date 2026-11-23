CXX      ?= g++
MPICXX   ?= mpicxx
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

all: bin/serial bin/bolsa_mpi

bin:
	mkdir -p bin

bin/serial: src/serial.cpp src/comun.hpp | bin
	$(CXX) $(CXXFLAGS) -o $@ src/serial.cpp

bin/bolsa_mpi: src/paralelo_mpi.cpp src/comun.hpp | bin
	$(MPICXX) $(CXXFLAGS) -DOMPI_SKIP_MPICXX -o $@ src/paralelo_mpi.cpp

clean:
	rm -rf bin

.PHONY: all clean
