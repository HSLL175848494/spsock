CXX := g++
CXXFLAGS := -fPIC -std=c++17
TARGET_NAME := SPSock
BUILD_DIR := build

$(shell mkdir -p $(BUILD_DIR))

# debug
ifdef debug
    CXXFLAGS += -g -O0
else
    CXXFLAGS += -O2
endif

# static
ifdef static
    LIB_TYPE := ar
    TARGET := $(BUILD_DIR)/lib$(TARGET_NAME).a
else
    LIB_TYPE := so
    TARGET := $(BUILD_DIR)/lib$(TARGET_NAME).so
endif

# test samples
ifdef test
    SAMPLE_SRC := example/example_tcp.cpp example/example_udp.cpp
    SAMPLE_TARGETS := $(addprefix $(BUILD_DIR)/, $(notdir $(SAMPLE_SRC:.cpp=)))
endif

SOURCES := SPBuffer.cpp SPController.cpp SPInitializer.cpp SPSock.cpp

.PHONY: all clean

all: $(TARGET) $(SAMPLE_TARGETS)

ifeq ($(LIB_TYPE),ar)
    OBJS := $(addprefix $(BUILD_DIR)/,$(SOURCES:.cpp=.o))
    .INTERMEDIATE: $(OBJS)

    $(TARGET): $(OBJS)
		ar rcs $@ $^
    $(BUILD_DIR)/%.o: %.cpp
		$(CXX) $(CXXFLAGS) -c $< -o $@
else
    $(TARGET): $(SOURCES)
		$(CXX) $(CXXFLAGS) -shared $^ -o $@
endif

# Build samples
ifdef test
$(BUILD_DIR)/%: example/%.cpp $(TARGET)
ifeq ($(LIB_TYPE),ar)
	$(CXX) $(CXXFLAGS) $< -o $@ $(TARGET)
else
	$(CXX) $(CXXFLAGS) $< -o $@ -L$(BUILD_DIR) -l$(TARGET_NAME)
endif
endif

clean:
	rm -rf $(BUILD_DIR)