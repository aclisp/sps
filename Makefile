BRPC_PATH=$(HOME)/brpc
NEED_GPERFTOOLS=1
RELEASE=0
#LINK_SO=1
ifeq ($(RELEASE),1)
	RELEASE_FLAGS= -DNDEBUG -O2
else
	RELEASE_FLAGS= -O2
endif
include $(BRPC_PATH)/config.mk
# Notes on the flags:
# 1. Added -fno-omit-frame-pointer: perf/tcmalloc-profiler use frame pointers by default
# 2. Added -D__const__= : Avoid over-optimizations of TLS variables by GCC>=4.8
CXXFLAGS+=$(CPPFLAGS) -std=c++0x $(RELEASE_FLAGS) -D__const__= -pipe -W -Wall -Wno-unused-parameter -Wfatal-errors -fPIC -fno-omit-frame-pointer
ifeq ($(NEED_GPERFTOOLS), 1)
	CXXFLAGS+=-DBRPC_ENABLE_CPU_PROFILER
endif
DEBUGLIBS:=$(LIBS) $(BRPC_PATH)/test
HDRS+=$(BRPC_PATH)/output/include
LIBS+=$(BRPC_PATH)/output/lib
DEBUGLIBPATHS=$(addprefix -L, $(DEBUGLIBS))
HDRPATHS = $(addprefix -I, $(HDRS))
LIBPATHS = $(addprefix -L, $(LIBS))
COMMA=,
SOPATHS=$(addprefix -Wl$(COMMA)-rpath$(COMMA), $(LIBS))

CLIENT_SOURCES =
BENCHMARK_SOURCES =
SERVER_SOURCES = sps_server.cpp sps_bucket.cpp
TEST_SOURCES = sps_test.cpp sps_bucket.cpp
PROTOS = sps.proto

PROTO_OBJS = $(PROTOS:.proto=.pb.o)
PROTO_GENS = $(PROTOS:.proto=.pb.h) $(PROTOS:.proto=.pb.cc)
CLIENT_OBJS = $(addsuffix .o, $(basename $(CLIENT_SOURCES)))
BENCHMARK_OBJS = $(addsuffix .o, $(basename $(BENCHMARK_SOURCES)))
SERVER_OBJS = $(addsuffix .o, $(basename $(SERVER_SOURCES)))
TEST_OBJS = $(addsuffix .o, $(basename $(TEST_SOURCES)))

ifeq ($(SYSTEM),Darwin)
 ifneq ("$(LINK_SO)", "")
	STATIC_LINKINGS += -lbrpc
 else
	# *.a must be explicitly specified in clang
	STATIC_LINKINGS += $(BRPC_PATH)/output/lib/libbrpc.a
 endif
	LINK_OPTIONS_SO = $^ $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS)
	LINK_OPTIONS = $^ $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS)
else ifeq ($(SYSTEM),Linux)
	STATIC_LINKINGS += -lbrpc
	LINK_OPTIONS_SO = -Xlinker "-(" $^ -Xlinker "-)" $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS)
	LINK_OPTIONS = -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS)
	DEBUGLINK_OPTIONS=$(subst -lbrpc,-lbrpc.dbg,$(LINK_OPTIONS))
	TESTLINK_OPTIONS=$(subst -lbrpc,-lbrpc.dbg -lgtest,$(LINK_OPTIONS))
endif

print-%:
	@echo $* = $($*)

.SECONDARY:

.PHONY:all
all: sps_server

.PHONY:test
test: sps_test

.PHONY:debug
debug: sps_server.dbg

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf sps_client sps_benchmark sps_server sps_test sps_server.dbg $(PROTO_GENS) $(PROTO_OBJS) $(CLIENT_OBJS) $(BENCHMARK_OBJS) $(SERVER_OBJS) $(TEST_OBJS)

sps_client:$(CLIENT_OBJS)
	@echo "Linking $@"
ifneq ("$(LINK_SO)", "")
	@$(CXX) $(LIBPATHS) $(SOPATHS) $(LINK_OPTIONS_SO) -o $@
else
	@$(CXX) $(LIBPATHS) $(LINK_OPTIONS) -o $@
endif

sps_benchmark:$(BENCHMARK_OBJS)
	@echo "Linking $@"
ifneq ("$(LINK_SO)", "")
	@$(CXX) $(LIBPATHS) $(SOPATHS) $(LINK_OPTIONS_SO) -o $@
else
	@$(CXX) $(LIBPATHS) $(LINK_OPTIONS) -o $@
endif

sps_server:$(PROTO_OBJS) $(SERVER_OBJS)
	@echo "Linking $@"
ifneq ("$(LINK_SO)", "")
	@$(CXX) $(LIBPATHS) $(SOPATHS) $(LINK_OPTIONS_SO) -o $@
else
	@$(CXX) $(LIBPATHS) $(LINK_OPTIONS) -o $@
endif

sps_test:$(TEST_OBJS)
	@echo "Linking $@"
	@$(CXX) $(DEBUGLIBPATHS) $(TESTLINK_OPTIONS) -o $@

sps_server.dbg:$(PROTO_OBJS) $(SERVER_OBJS)
	@echo "Linking $@"
	@$(CXX) $(DEBUGLIBPATHS) $(DEBUGLINK_OPTIONS) -o $@

%.pb.cc %.pb.h:%.proto
	@echo "Generating $@"
	@$(PROTOC) --cpp_out=. --proto_path=. $<

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

