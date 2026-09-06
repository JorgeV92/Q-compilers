CXX = clang++
CXXFLAGS ?= -O0 -g
LLVM_CONFIG ?= llvm-config
LLVM_AS ?= $(shell "$(LLVM_CONFIG)" --bindir)/llvm-as
CLANG ?= clang
PYTHON ?= python3

.PHONY: all llvm ast test clean
.DELETE_ON_ERROR:

all: llvm

llvm: build/qlang

ast: build/qlang-ast

build/qlang: src/qlang.cpp Makefile | build
	@command -v "$(LLVM_CONFIG)" >/dev/null 2>&1 || { \
		echo "error: LLVM development tools are required; set LLVM_CONFIG=/path/to/llvm-config or use 'make ast'" >&2; exit 1; }
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DQ_ENABLE_LLVM $< \
		$$("$(LLVM_CONFIG)" --cxxflags --ldflags --system-libs --libs core) \
		$(LDFLAGS) $(LDLIBS) -o $@

build/qlang-ast: src/qlang.cpp Makefile | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -Wall -Wextra -Wpedantic \
		$< $(LDFLAGS) $(LDLIBS) -o $@

build:
	mkdir -p $@

test: llvm
	LLVM_AS="$(LLVM_AS)" CLANG="$(CLANG)" "$(PYTHON)" tests/test_ir.py ./build/qlang

clean:
	$(RM) build/qlang build/qlang-ast
