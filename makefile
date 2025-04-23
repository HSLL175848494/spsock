CXX := g++
CXXFLAGS := -Wall -I. -fPIC -fvisibility=default
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
    SAMPLE_SRC := sample/sample_tcp.cpp sample/sample_udp.cpp
    SAMPLE_TARGETS := $(addprefix $(BUILD_DIR)/, $(notdir $(SAMPLE_SRC:.cpp=)))
endif

SOURCES := SPController.cpp SPSock.cpp

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
ifdef TEST
$(BUILD_DIR)/%: sample/%.cpp $(TARGET)
	$(CXX) $(CXXFLAGS) $< -o $@ -L$(BUILD_DIR) -l$(TARGET_NAME)
endif

clean:
	rm -rf $(BUILD_DIR)