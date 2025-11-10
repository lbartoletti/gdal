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
#ifndef DWG_R2004_H
#define DWG_R2004_H

#include "r2000.h"
#include "lz77.h"

#include <memory>
#include <vector>

/**
 * @brief Section locator record for R2004 files
 *
 * R2004 uses a different section locator structure than R2000.
 * Each section has an ID, address in file, size, and optional encryption flag.
 */
struct SectionLocatorR2004
{
    int nPageNumber;      ///< Page number in section map
    int nDataSize;        ///< Size of uncompressed data
    int nStartOffset;     ///< Start offset in file
    int nHeaderSize;      ///< Size of section header
    int nChecksumSeed;    ///< Seed for checksum calculation
    int nUnknown;         ///< Unknown field (reserved)
};

/**
 * @brief Section page for R2004 compressed sections
 *
 * R2004 sections are divided into pages, each compressed separately.
 * This allows random access to section data without decompressing everything.
 */
struct SectionPageR2004
{
    long nPageOffset;          ///< Offset of page in file
    int  nCompressedSize;      ///< Size of compressed page data
    int  nUncompressedSize;    ///< Size after decompression
    long nChecksumCompressed;  ///< CRC of compressed data
    long nChecksumUncompressed;///< CRC of uncompressed data
};

/**
 * @brief DWG R2004 file format handler
 *
 * Extends DWGFileR2000 to support the R2004 file format which introduced:
 * - Custom LZ77 compression for all sections
 * - New section organization with page-based compression
 * - 64-bit handle support
 * - CRC-32 checksums instead of CRC-16
 * - Encrypted file header and section locators
 *
 * Architecture:
 * - Inherits 80% of entity parsing from DWGFileR2000
 * - Overrides file structure parsing methods
 * - Uses DWGLZ77Decompressor for all compressed sections
 *
 * @note This is Phase 1 foundation. Full implementation in Phases 2-6.
 */
class DWGFileR2004 : public DWGFileR2000
{
public:
    /**
     * @brief Construct a new DWGFileR2004 object
     *
     * @param poFileIO File I/O handler (ownership transferred)
     */
    explicit DWGFileR2004(CADFileIO* poFileIO);

    /**
     * @brief Destroy the DWGFileR2004 object
     */
    ~DWGFileR2004() override;

protected:
    /**
     * @brief Read section locators from R2004 file
     *
     * R2004 section locators are stored differently than R2000:
     * - Located via system section map
     * - Each record describes a compressed section
     * - Contains page information for random access
     *
     * @return CADErrorCodes::SUCCESS or error code
     */
    int ReadSectionLocators() override;

    /**
     * @brief Read file header from R2004 file
     *
     * R2004 header includes:
     * - File signature "AC1018" (for R2004)
     * - Encrypted metadata section
     * - System section page map
     * - Data section map
     *
     * @param eOptions Open options
     * @return CADErrorCodes::SUCCESS or error code
     */
    int ReadHeader(enum OpenOptions eOptions) override;

    /**
     * @brief Read classes section from R2004 file
     *
     * Classes section describes custom object types in the drawing.
     * In R2004, this section is LZ77 compressed.
     *
     * @param eOptions Open options
     * @return CADErrorCodes::SUCCESS or error code
     */
    int ReadClasses(enum OpenOptions eOptions) override;

    /**
     * @brief Decompress a section using LZ77
     *
     * R2004 sections are compressed with custom LZ77 algorithm.
     * Each section may consist of multiple pages compressed separately.
     *
     * @param nAddress File offset of compressed section
     * @param nCompressedSize Size of compressed data
     * @param decompressed Output buffer for decompressed data
     * @return CADErrorCodes::SUCCESS or error code
     */
    int DecompressSection(long nAddress, int nCompressedSize,
                         std::vector<char>& decompressed);

    /**
     * @brief Verify CRC-32 checksum
     *
     * R2004 uses CRC-32 checksums (unlike R2000's CRC-16).
     * This verifies data integrity after decompression.
     *
     * @param data Data to verify
     * @param size Size of data
     * @param expectedCRC Expected CRC-32 value
     * @return true if CRC matches
     */
    bool VerifyCRC32(const char* data, size_t size, unsigned long expectedCRC);

private:
    /**
     * @brief Read the system section page map
     *
     * The system section contains metadata about all other sections.
     * This map describes where each system page is located.
     *
     * @return CADErrorCodes::SUCCESS or error code
     */
    int ReadSystemSectionMap();

    /**
     * @brief Read the data section map
     *
     * The data section contains the actual drawing data (entities, objects).
     * This map describes the page structure.
     *
     * @return CADErrorCodes::SUCCESS or error code
     */
    int ReadDataSectionMap();

private:
    std::unique_ptr<DWGLZ77Decompressor> m_pDecompressor; ///< LZ77 decompressor instance
    std::vector<SectionLocatorR2004>     m_sectionLocators; ///< Section locator records
    std::vector<SectionPageR2004>        m_systemPages;     ///< System section pages
    std::vector<SectionPageR2004>        m_dataPages;       ///< Data section pages

    // R2004 file structure offsets
    long m_nSystemSectionOffset;  ///< Offset to system section
    long m_nDataSectionOffset;    ///< Offset to data section
};

#endif // DWG_R2004_H
