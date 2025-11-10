/*******************************************************************************
 *  Project: libopencad
 *  Purpose: OpenSource CAD formats support library
 *  Author: LibreCAD project
 *  Language: C++
 *******************************************************************************
 *  The MIT License (MIT)
 *
 *  Copyright (c) 2024 LibreCAD project
 *
 *  SPDX-License-Identifier: MIT
 *******************************************************************************/
#ifndef DWG_LZ77_H
#define DWG_LZ77_H

#include <cstddef>
#include <string>

/**
 * @brief DWG LZ77 Decompressor for R2004+ files
 *
 * This class implements the custom LZ77 compression algorithm used in
 * DWG files from R2004 onwards. This is NOT standard DEFLATE compression
 * and cannot use zlib. The algorithm is documented in the ODA DWG
 * Specification section 4.7.
 *
 * The compression format consists of:
 * 1. Literal Length (RL): Initial uncompressed bytes count
 * 2. Sequence of compression opcodes:
 *    - litCount: Number of literal bytes to copy from input
 *    - compressedBytes: Number of bytes to copy from sliding window
 *    - compOffset: Offset backwards in output buffer (1-based)
 *
 * @note This decompressor is stateless and thread-safe for different
 *       instances operating on different buffers.
 */
class DWGLZ77Decompressor
{
public:
    /**
     * @brief Construct a new DWGLZ77Decompressor object
     */
    DWGLZ77Decompressor();

    /**
     * @brief Destroy the DWGLZ77Decompressor object
     */
    ~DWGLZ77Decompressor();

    // Disable copy and move (not needed for stateless operation)
    DWGLZ77Decompressor(const DWGLZ77Decompressor&) = delete;
    DWGLZ77Decompressor& operator=(const DWGLZ77Decompressor&) = delete;

    /**
     * @brief Decompress DWG R2004 LZ77 compressed data
     *
     * @param input Pointer to compressed input data (must not be nullptr)
     * @param inputSize Size of compressed input in bytes
     * @param output Pointer to output buffer (must not be nullptr)
     * @param outputSize Size of output buffer in bytes
     * @param actualOutputSize Pointer to receive actual decompressed size (optional)
     * @return true if decompression succeeded
     * @return false if decompression failed (check GetLastError())
     *
     * @note Output buffer must be large enough to hold decompressed data.
     *       The actual size written is returned in actualOutputSize if provided.
     */
    bool Decompress(const char* input, size_t inputSize,
                   char* output, size_t outputSize,
                   size_t* actualOutputSize = nullptr);

    /**
     * @brief Get the last error message
     *
     * @return const char* Error message string, or nullptr if no error
     */
    const char* GetLastError() const;

private:
    /**
     * @brief Read a compressed integer from the input stream
     *
     * DWG uses a variable-length encoding for integers in the compression
     * stream. This method decodes such integers.
     *
     * @param input Input buffer
     * @param inputPos Current position in input (updated)
     * @param inputSize Total input size
     * @param value Output value
     * @return true if successful, false if input truncated
     */
    bool ReadCompressedInt(const char* input, size_t& inputPos,
                          size_t inputSize, unsigned int& value);

    std::string m_lastError;  ///< Last error message
};

#endif // DWG_LZ77_H
