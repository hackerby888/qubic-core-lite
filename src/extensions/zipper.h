#pragma once

#include <blosc2.h>
#include <stdexcept>
#include <thread>
#include <vector>

class Zipper
{
public:
    static std::vector<unsigned char> zip(unsigned char* src, size_t srcSize, int nthreads = 0) {
        // set nthreads to max if not specified
        if (nthreads <= 0) {
            nthreads = std::thread::hardware_concurrency();
        }
        // 1. Initialize the library (once per app)
        blosc2_init();

        // 2. Setup simple parameters
        blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
        cparams.typesize = 1;         // Size of data type (1 for char/binary)
        cparams.clevel = 1;           // Compression level
        cparams.nthreads = nthreads;         // SIMPLE: Just set the number
        cparams.compcode = BLOSC_BLOSCLZ; // Use Zstdx's power with Blosc's ease

        // 3. Allocate destination
        std::vector<unsigned char> dst(srcSize + BLOSC2_MAX_OVERHEAD);

        // 4. One-call compression
        blosc2_context* schunk = blosc2_create_cctx(cparams);
        // set compressor
        int cSize = blosc2_compress_ctx(schunk, src, srcSize, dst.data(), dst.size());

        dst.resize(cSize);
        blosc2_free_ctx(schunk);
        return dst;
    }

    static std::vector<unsigned char> unzip(unsigned char *compressed_data, size_t compressed_data_size, int nthreads = 0) {
        // set nthreads to max if not specified
        if (nthreads <= 0) {
            nthreads = std::thread::hardware_concurrency();
        }
        // 1. Initialize
        blosc2_init();

        // 2. Query the header to find the original (decompressed) size
        int32_t nbytes, cbytes, blocksize;
        blosc2_cbuffer_sizes(compressed_data, &nbytes, &cbytes, &blocksize);

        // 3. Allocate the destination vector based on the original size
        std::vector<unsigned char> decompressed_out(nbytes);

        // 4. Setup decompression parameters
        blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;
        dparams.nthreads = nthreads;

        // 5. One-call decompression
        blosc2_context* dctx = blosc2_create_dctx(dparams);
        int dSize = blosc2_decompress_ctx(dctx, compressed_data, compressed_data_size,
                                          decompressed_out.data(), decompressed_out.size());

        // 6. Error checking
        if (dSize <= 0) {
            blosc2_free_ctx(dctx);
            blosc2_destroy();
            throw std::runtime_error("Decompression failed!");
        }

        // 7. Cleanup
        blosc2_free_ctx(dctx);

        return decompressed_out;
    }
};