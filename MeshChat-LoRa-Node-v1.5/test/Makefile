# Builds and runs the PC tests of the relay core (needs g++ and OpenSSL headers: apt install g++ libssl-dev)
CXX ?= g++
SRC = ../lib/meshcore/mc_packet.cpp ../lib/meshcore/mc_crypto.cpp ../lib/meshcore/mc_crypto_openssl.cpp ../lib/meshcore/mc_lora.cpp ../lib/meshcore/mc_node.cpp ../lib/meshcore/mc_frames.cpp ../lib/meshcore/mc_ps2.cpp ../lib/meshcore/mc_alert.cpp ../lib/meshcore/mc_weather.cpp ../lib/meshcore/mc_hid.cpp
FLAGS = -std=c++17 -O1 -g -Wall -Wextra -Wno-deprecated-declarations -DMC_HOST

host_tests: host_tests.cpp $(SRC)
	$(CXX) $(FLAGS) -o $@ host_tests.cpp $(SRC) -lcrypto

run: host_tests
	./host_tests

interop: interop.cpp $(SRC)
	$(CXX) $(FLAGS) -o $@ interop.cpp $(SRC) -lcrypto

clean:
	rm -f host_tests interop
