# Compiler
CXX      := g++
CXXFLAGS := -Wall -Wextra -O2 -std=c++17 -fopenmp \
            -I. -I../fij/include -I../fij/include/uapi

# Add extra libs here if needed, e.g. -lpthread
LDFLAGS  := -fopenmp
LDLIBS   := $(JSON_CFLAGS)

# Check for nlohmann_json using pkg-config
PKG_CONFIG ?= pkg-config


CHECK_JSON := $(shell echo '#include <nlohmann/json.hpp>' | \
               $(CXX) -std=c++17 -x c++ -E - 2>/dev/null && echo OK)

ifeq ($(CHECK_JSON),)
$(error nlohmann_json headers not found. On Debian/Ubuntu: sudo apt install nlohmann-json3-dev)
endif


# Sources
SRCS := \
    fij_campaign.cpp \
    fij_config.cpp  \
    fij_core.cpp    \
    fij_ioctls.cpp  \
    fij_run.cpp     \
    main.cpp

OBJS   := $(SRCS:.cpp=.o)
TARGET := fij_app

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
