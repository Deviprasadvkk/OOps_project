CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

TARGET_EPOLL := concurrent_server
TARGET_BASELINE := baseline_server
TARGET_BENCH := benchmark_client

all: $(TARGET_EPOLL) $(TARGET_BASELINE) $(TARGET_BENCH)

$(TARGET_EPOLL): concureent.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

$(TARGET_BASELINE): baseline_thread_per_conn.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

$(TARGET_BENCH): benchmark_client.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

asan: concureent.cpp baseline_thread_per_conn.cpp benchmark_client.cpp
	$(CXX) $(CXXFLAGS) $(ASAN_FLAGS) concureent.cpp -o $(TARGET_EPOLL)_asan
	$(CXX) $(CXXFLAGS) $(ASAN_FLAGS) baseline_thread_per_conn.cpp -o $(TARGET_BASELINE)_asan
	$(CXX) $(CXXFLAGS) $(ASAN_FLAGS) benchmark_client.cpp -o $(TARGET_BENCH)_asan

clean:
	rm -f $(TARGET_EPOLL) $(TARGET_BASELINE) $(TARGET_BENCH)
	rm -f $(TARGET_EPOLL)_asan $(TARGET_BASELINE)_asan $(TARGET_BENCH)_asan

.PHONY: all asan clean
