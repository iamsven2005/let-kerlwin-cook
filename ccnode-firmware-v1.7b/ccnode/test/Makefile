# Host-side tests. Needs g++ and a copy of the TinyScreen library (for its fonts).
#   make TINYSCREEN_LIB=~/Arduino/libraries/TinyScreen
TINYSCREEN_LIB ?= $(HOME)/Arduino/libraries/TinyScreen
FW := ../firmware/CCNode
SRC := test_main.cpp $(FW)/gestures.cpp $(FW)/protocol.cpp $(FW)/card_layout.cpp $(FW)/node_id.cpp $(FW)/display.cpp $(FW)/accel_core.cpp
CXXFLAGS := -std=c++14 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation \
            -fsanitize=address,undefined -Imock -I$(FW) -I$(TINYSCREEN_LIB) -I$(TINYSCREEN_LIB)/src

all: run

ccnode_tests: $(SRC) mock/TinyScreen.h i2c_sim.h $(wildcard $(FW)/*.h)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

run: ccnode_tests
	@mkdir -p out
	./ccnode_tests
	@command -v python3 >/dev/null && python3 ppm2png.py out || true

clean:
	rm -rf ccnode_tests out

.PHONY: all run clean
