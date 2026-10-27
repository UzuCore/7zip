# UnEGG decoder backend

This directory contains the UnEGG Linux v0.5 source by ESTsoft, used as a decode-only backend for ALZ and EGG archives. The original license is included in LICENSE.txt.

The CMake target builds the decoder and its bundled zlib, bzip2, LZMA, and AES dependencies without fetching packages during the build.

Redistribution must follow LICENSE.txt. It permits free use and distribution for non-commercial use; commercial use requires ESTsoft approval. The AZO compression algorithm must remain unmodified and must not be used to develop an EGG compression format or algorithm. Keep the license with redistributed source and documentation.
