LLVM passes for compiling Rust code with Emscripten.  The code could use some
cleanup, but it more or less works.  Currently it appears only
`-break-struct-arguments` is needed, when compiling with [patched
emscripten-fastcomp][em-patch].

[em-patch]: https://github.com/epdtry/emscripten-fastcomp/commit/1331b061fcb813dad71719792a89fa5cc396864a

Compile with:

    make BreakStructArguments.so \
        LLVM_PREFIX=.../emscripten-fastcomp/build

Use with:

    .../emscripten-fastcomp/build/bin/opt -load=BreakStructArguments.so \
        -O3 -break-struct-arguments -globaldce \
        input.ll -S -o output.ll
