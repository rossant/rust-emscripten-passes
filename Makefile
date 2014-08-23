LLVM_PREFIX = /usr

%.so: %.cpp
	g++ -fPIC -fno-rtti -fno-exceptions -shared -o $@ $< `$(LLVM_PREFIX)/bin/llvm-config --cxxflags`

