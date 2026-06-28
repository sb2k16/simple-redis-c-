CXX      ?= c++
SDKROOT  ?= $(shell xcrun --show-sdk-path 2>/dev/null)
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -Iinclude
ifneq ($(SDKROOT),)
  CXXFLAGS += -isystem $(SDKROOT)/usr/include/c++/v1
endif
LDFLAGS  ?= -pthread

SERVER   = bin/simple-redis
CLIENT   = bin/redis-client

SERVER_SRCS = \
	src/main.cpp \
	src/protocol/resp.cpp \
	src/store/value.cpp \
	src/store/store.cpp \
	src/pubsub/hub.cpp \
	src/persistence/snapshot.cpp \
	src/commands/executor.cpp \
	src/server/server.cpp

SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)

.PHONY: all build run test clean client dirs

all: build

dirs:
	@mkdir -p bin data

build: dirs $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT): client/client.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: build
	./$(SERVER) --port 6380

test: build
	@./scripts/smoke_test.sh

clean:
	rm -rf bin data src/**/*.o src/*.o 2>/dev/null || true

client: build
	./$(CLIENT)
