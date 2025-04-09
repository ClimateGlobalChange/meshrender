# meshrender


Author:  Paul Ullrich
Email:   paullrich@ucdavis.edu

Copyright 2025 Paul Ullrich

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Installation via CMake

Dependencies: OpenGL, glfw3, GLEW, NetCDF

On Mac all dependencies can be installed via homebrew.

meshrender can be built and installed on various systems using CMake:

     cmake -DCMAKE_INSTALL_PREFIX=. .
	 make all install

With the above command output binary will be put in ./bin

Usage
=====

     meshrender [-b img] [-lc lcol] [-lw lwidth] <mesh file>
       [-b img]           Globe image file
       [-lc lcol]         Line color spec (name or "R,G,B[,A]")
       [-lw lwidth]       Line width (default 1.0)

Summary
=======
If you enjoy this software please drop me a line and let me know!
