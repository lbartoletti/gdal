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
#include "lz77.h"

#include <cstring>
#include <algorithm>

DWGLZ77Decompressor::DWGLZ77Decompressor()
{
}

DWGLZ77Decompressor::~DWGLZ77Decompressor()
{
}

bool DWGLZ77Decompressor::Decompress(const char* input, size_t inputSize,
                                     char* output, size_t outputSize,
                                     size_t* actualOutputSize)
{
    // Input validation
    if (input == nullptr || output == nullptr)
    {
        m_lastError = "Input or output buffer is null";
        return false;
    }

    if (inputSize == 0)
    {
        m_lastError = "Input size is zero";
        return false;
    }

    if (outputSize == 0)
    {
        m_lastError = "Output size is zero";
        return false;
    }

    // Initialize state
    size_t inputPos = 0;
    size_t outputPos = 0;
    const unsigned char* inPtr = reinterpret_cast<const unsigned char*>(input);
    unsigned char* outPtr = reinterpret_cast<unsigned char*>(output);

    // Step 1: Read the literal length (first compressed integer)
    unsigned int literalLength = 0;
    if (!ReadCompressedInt(input, inputPos, inputSize, literalLength))
    {
        m_lastError = "Failed to read initial literal length";
        return false;
    }

    // Check if literal length exceeds output buffer
    if (literalLength > outputSize)
    {
        m_lastError = "Literal length exceeds output buffer size";
        return false;
    }

    // Check if literal length exceeds remaining input
    if (inputPos + literalLength > inputSize)
    {
        m_lastError = "Literal length exceeds remaining input";
        return false;
    }

    // Step 2: Copy initial literal bytes
    if (literalLength > 0)
    {
        memcpy(outPtr + outputPos, inPtr + inputPos, literalLength);
        inputPos += literalLength;
        outputPos += literalLength;
    }

    // Step 3: Main decompression loop
    while (inputPos < inputSize && outputPos < outputSize)
    {
        // Read opcode (control byte)
        if (inputPos >= inputSize)
        {
            break; // End of compressed data
        }

        unsigned char opcode = inPtr[inputPos++];

        // Decode the opcode
        if (opcode == 0x00)
        {
            // Long compression offset (3 bytes total)
            if (inputPos + 2 > inputSize)
            {
                m_lastError = "Truncated long offset opcode";
                return false;
            }

            unsigned char byte1 = inPtr[inputPos++];
            unsigned char byte2 = inPtr[inputPos++];

            // Length is stored in bits 0-3 of opcode (already 0) plus byte1 bits 4-7
            unsigned int length = ((byte1 >> 4) & 0x0F) + 3;

            // Offset is in bits 0-3 of byte1 and all of byte2
            unsigned int offset = (((byte1 & 0x0F) << 8) | byte2) + 1;

            // Copy from sliding window
            if (!CopyFromWindow(outPtr, outputPos, outputSize, offset, length))
            {
                return false;
            }
        }
        else if (opcode >= 0x01 && opcode <= 0x0F)
        {
            // Short literal run (1-15 bytes)
            unsigned int litCount = opcode;

            if (inputPos + litCount > inputSize)
            {
                m_lastError = "Truncated literal run";
                return false;
            }

            if (outputPos + litCount > outputSize)
            {
                m_lastError = "Output buffer overflow (literal run)";
                return false;
            }

            memcpy(outPtr + outputPos, inPtr + inputPos, litCount);
            inputPos += litCount;
            outputPos += litCount;
        }
        else if (opcode >= 0x10 && opcode <= 0x1F)
        {
            // Medium compression (2 bytes total)
            if (inputPos >= inputSize)
            {
                m_lastError = "Truncated medium compression opcode";
                return false;
            }

            unsigned char byte1 = inPtr[inputPos++];

            // Length is in bits 0-3 of opcode
            unsigned int length = (opcode & 0x0F) + 3;

            // Offset is in byte1
            unsigned int offset = byte1 + 1;

            // Copy from sliding window
            if (!CopyFromWindow(outPtr, outputPos, outputSize, offset, length))
            {
                return false;
            }
        }
        else if (opcode >= 0x20)
        {
            // Long literal or long compression
            unsigned char firstByte = opcode;

            if (inputPos >= inputSize)
            {
                m_lastError = "Truncated opcode";
                return false;
            }

            unsigned char secondByte = inPtr[inputPos++];

            if ((firstByte & 0x80) == 0)
            {
                // Compression (bit 7 = 0)
                // Extract length and offset
                // Length: 6 bits from firstByte (bits 0-5) + 3
                unsigned int length = (firstByte & 0x3F) + 3;

                // Offset: secondByte gives lower 8 bits
                unsigned int offset = secondByte + 1;

                // For longer offsets, we might need a third byte
                if ((firstByte & 0x40) != 0)
                {
                    // Need third byte for extended offset
                    if (inputPos >= inputSize)
                    {
                        m_lastError = "Truncated extended offset";
                        return false;
                    }

                    unsigned char thirdByte = inPtr[inputPos++];
                    offset = (secondByte << 8) | thirdByte;
                    offset += 1;
                }

                // Copy from sliding window
                if (!CopyFromWindow(outPtr, outputPos, outputSize, offset, length))
                {
                    return false;
                }
            }
            else
            {
                // Literal run (bit 7 = 1)
                // Length is in bits 0-6 of firstByte
                unsigned int litCount = (firstByte & 0x7F);

                // secondByte was already read above - it's the first literal byte
                if (litCount == 0)
                {
                    // Special case: length actually in secondByte
                    litCount = secondByte;

                    if (inputPos + litCount > inputSize)
                    {
                        m_lastError = "Truncated long literal run";
                        return false;
                    }

                    if (outputPos + litCount > outputSize)
                    {
                        m_lastError = "Output buffer overflow (long literal)";
                        return false;
                    }

                    memcpy(outPtr + outputPos, inPtr + inputPos, litCount);
                    inputPos += litCount;
                    outputPos += litCount;
                }
                else
                {
                    // Normal literal run with secondByte as first literal
                    if (outputPos + litCount > outputSize)
                    {
                        m_lastError = "Output buffer overflow (literal)";
                        return false;
                    }

                    // Write the secondByte first
                    outPtr[outputPos++] = secondByte;
                    litCount--;

                    if (litCount > 0)
                    {
                        if (inputPos + litCount > inputSize)
                        {
                            m_lastError = "Truncated literal run data";
                            return false;
                        }

                        memcpy(outPtr + outputPos, inPtr + inputPos, litCount);
                        inputPos += litCount;
                        outputPos += litCount;
                    }
                }
            }
        }
    }

    // Return actual decompressed size
    if (actualOutputSize != nullptr)
    {
        *actualOutputSize = outputPos;
    }

    m_lastError.clear();
    return true;
}

const char* DWGLZ77Decompressor::GetLastError() const
{
    return m_lastError.empty() ? nullptr : m_lastError.c_str();
}

bool DWGLZ77Decompressor::ReadCompressedInt(const char* input, size_t& inputPos,
                                            size_t inputSize, unsigned int& value)
{
    if (inputPos >= inputSize)
    {
        m_lastError = "Input truncated while reading compressed integer";
        return false;
    }

    const unsigned char* inPtr = reinterpret_cast<const unsigned char*>(input);
    unsigned char firstByte = inPtr[inputPos++];

    // DWG compressed integers use variable-length encoding:
    // - If bits 7-6 == 00: 1-byte value (bits 5-0)
    // - If bits 7-6 == 01: 2-byte value
    // - If bits 7-6 == 10: 3-byte value
    // - If bits 7-6 == 11: 4-byte value

    unsigned char typeBits = (firstByte >> 6) & 0x03;

    switch (typeBits)
    {
        case 0x00:
            // 1-byte value (6 bits)
            value = firstByte & 0x3F;
            break;

        case 0x01:
            // 2-byte value (14 bits)
            if (inputPos >= inputSize)
            {
                m_lastError = "Input truncated reading 2-byte compressed int";
                return false;
            }
            value = ((firstByte & 0x3F) << 8) | inPtr[inputPos++];
            break;

        case 0x02:
            // 3-byte value (22 bits)
            if (inputPos + 1 >= inputSize)
            {
                m_lastError = "Input truncated reading 3-byte compressed int";
                return false;
            }
            value = ((firstByte & 0x3F) << 16) |
                    (inPtr[inputPos] << 8) |
                    inPtr[inputPos + 1];
            inputPos += 2;
            break;

        case 0x03:
            // 4-byte value (30 bits)
            if (inputPos + 2 >= inputSize)
            {
                m_lastError = "Input truncated reading 4-byte compressed int";
                return false;
            }
            value = ((firstByte & 0x3F) << 24) |
                    (inPtr[inputPos] << 16) |
                    (inPtr[inputPos + 1] << 8) |
                    inPtr[inputPos + 2];
            inputPos += 3;
            break;
    }

    return true;
}

bool DWGLZ77Decompressor::CopyFromWindow(unsigned char* output, size_t& outputPos,
                                         size_t outputSize, unsigned int offset,
                                         unsigned int length)
{
    // Validate offset
    if (offset > outputPos)
    {
        m_lastError = "Invalid backreference offset (points before start of output)";
        return false;
    }

    // Validate output space
    if (outputPos + length > outputSize)
    {
        m_lastError = "Output buffer overflow (backreference copy)";
        return false;
    }

    // Calculate source position
    size_t srcPos = outputPos - offset;

    // Copy bytes one at a time to handle overlapping regions correctly
    // (e.g., when length > offset, pattern repeats)
    for (unsigned int i = 0; i < length; i++)
    {
        output[outputPos++] = output[srcPos++];
    }

    return true;
}
