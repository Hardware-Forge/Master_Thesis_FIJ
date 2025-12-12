# Compiler
CXX      := g++

# 1. Setup pkg-config for OpenCV (Try opencv4, fallback to opencv)
PKG_CONFIG ?= pkg-config
OPENCV_CFLAGS := $(shell $(PKG_CONFIG) --cflags opencv4 2>/dev/null || $(PKG_CONFIG) --cflags opencv)
OPENCV_LIBS   := $(shell $(PKG_CONFIG) --libs opencv4 2>/dev/null || $(PKG_CONFIG) --libs opencv)

# 2. Add OPENCV_CFLAGS to CXXFLAGS
CXXFLAGS := -Wall -Wextra -O2 -std=c++17 -fopenmp \
            -I. -I../fij/include -I../fij/include/uapi \
            $(OPENCV_CFLAGS)

# Add extra libs here if needed, e.g. -lpthread
LDFLAGS  := -fopenmp

# 3. Add OPENCV_LIBS to LDLIBS
LDLIBS   := $(JSON_CFLAGS) $(OPENCV_LIBS)

# Check for nlohmann_json using pkg-config
CHECK_JSON := $(shell echo '#include <nlohmann/json.hpp>' | \
               $(CXX) -std=c++17 -x c++ -E - 2>/dev/null && echo OK)

ifeq ($(CHECK_JSON),)
$(error nlohmann_json headers not found. On Debian/Ubuntu: sudo apt install nlohmann-json3-dev)
endif

# Check if OpenCV was found (Optional but helpful safety check)
ifeq ($(OPENCV_LIBS),)
$(warning OpenCV not found via pkg-config. Ensure libopencv-dev is installed.)
endif

# Sources
SRCS := \
    fij_campaign.cpp \
    fij_config.cpp  \
    fij_core.cpp    \
    fij_ioctls.cpp  \
    fij_run.cpp     \
    fij_analyzer/campaign_analyzer.cpp  \
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