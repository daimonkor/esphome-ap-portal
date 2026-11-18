CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable
CORE = ../../components/ap_portal/ap_portal_core.h

test: test_core
	./test_core

test_core: test_core.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp

asan: test_core.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -g -fsanitize=address,undefined -o test_core_asan test_core.cpp && ./test_core_asan

clean:
	rm -f test_core test_core_asan

.PHONY: test asan clean
