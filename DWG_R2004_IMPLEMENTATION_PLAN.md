# DWG R2004 (AC1018) Implementation Plan for GDAL libopencad
## Detailed Technical Roadmap with Existing GDAL Infrastructure Analysis

**Date:** November 10, 2025
**Target:** Add AC1018 (AutoCAD R2004-2006) support to `ogr/ogrsf_frmts/cad/libopencad/`
**Duration Estimate:** 6 months
**Priority:** HIGH
**Dependencies:** Existing libopencad R2000 implementation

---

## Executive Summary

This document provides a detailed implementation plan for adding DWG R2004 (AC1018) support to GDAL's libopencad library. R2004 is the **critical transition version** that introduced LZ77 compression, making it significantly different from R2000 and foundational for all subsequent versions.

**Key Finding:** GDAL already has comprehensive compression infrastructure that can be leveraged, but DWG R2004 uses a **custom LZ77 variant** that requires specialized implementation.

### Success Criteria
- Read R2004 DWG files with 95%+ geometry accuracy
- Decompress LZ77-compressed sections successfully
- Parse 64-bit handles correctly
- Support all entity types from R2000 plus new R2004 entities
- Pass comprehensive test suite with real-world R2004 files
- Performance within 2x of R2000 implementation

---

## 1. Analysis of Existing GDAL Compression Infrastructure

### 1.1 GDAL Compression Facilities

GDAL provides extensive compression support through multiple layers:

#### **Layer 1: Built-in zlib Library**
**Location:** `frmts/zlib/`

GDAL includes a complete zlib 1.2.x implementation with:
- `deflate.c/h` - DEFLATE compression (LZ77 + Huffman)
- `inflate.c/h` - DEFLATE decompression
- `inffast.c/h` - Fast inflation routines
- `inftrees.c/h` - Huffman tree inflation
- Full support for raw DEFLATE, zlib, and gzip formats

**Key Files:**
```
frmts/zlib/
├── deflate.c       # Compression engine
├── deflate.h
├── inflate.c       # Decompression engine
├── inflate.h
├── inffast.c       # Fast decompression
├── inftrees.c      # Huffman tree handling
├── zlib.h          # Public API
└── zconf.h         # Configuration
```

#### **Layer 2: CPL Compression API**
**Location:** `port/cpl_vsil_gzip.cpp`, `port/cpl_conv.h`

High-level C++ API for compression/decompression:

```cpp
// DEFLATE decompression
void *CPLZLibInflate(const void *ptr, size_t nBytes,
                     void *outptr, size_t nOutAvailableBytes,
                     size_t *pnOutBytes);

// DEFLATE compression
void *CPLZLibDeflate(const void *ptr, size_t nBytes, int nLevel,
                     void *outptr, size_t nOutAvailableBytes,
                     size_t *pnOutBytes);
```

**Features:**
- Automatic memory allocation if `outptr == NULL`
- Optional libdeflate backend (faster)
- Simple error handling
- Used by multiple GDAL drivers

**Example Usage (from OpenFileGDB):**
```cpp
// Line 1427 of gdalopenfilegdbrasterband.cpp
size_t nOutBytes = 0;
GByte *outPtr = abyTmpBuffer.data();
if (!CPLZLibInflate(pabyData, nInBytes, outPtr,
                    abyTmpBuffer.size(), &nOutBytes)) {
    // Handle error
}
```

#### **Layer 3: CPLCompressor Framework**
**Location:** `port/cpl_compressor.h/cpp`

Pluggable compressor/decompressor registry:

```cpp
typedef struct {
    int nStructVersion;
    const char *pszId;              // "zlib", "gzip", "blosc", etc.
    CPLCompressorType eType;
    CSLConstList papszMetadata;
    CPLCompressionFunc pfnFunc;     // Callback function
    void *user_data;
} CPLCompressor;

// Register custom compressor/decompressor
bool CPLRegisterDecompressor(const CPLCompressor *decompressor);
const CPLCompressor *CPLGetDecompressor(const char *pszId);
```

**Registered Decompressors:**
- `zlib` - Standard DEFLATE (using zlib or libdeflate)
- `gzip` - GZIP format (DEFLATE with gzip headers)
- `blosc` - High-performance compression (if compiled with Blosc)
- `lzma` - LZMA compression (if compiled with LZMA)
- `zstd` - Zstandard compression (if compiled with Zstandard)
- `lz4` - LZ4 compression (if compiled with LZ4)

**Usage in Drivers:**
- PMTiles driver: `const CPLCompressor *psDecompressor = CPLGetDecompressor("gzip");`
- ZARR driver: Pluggable compression for scientific data
- LibertIFF driver: TIFF compression variants

### 1.2 Applicability to DWG R2004

#### **Critical Difference: DWG LZ77 ≠ Standard DEFLATE**

According to the ODA Specification:

> "The DWG file format version 2004 compression is a **variation** on the LZ77 compression algorithm."

**Standard DEFLATE (used by zlib):**
- LZ77 sliding window compression
- **Plus** Huffman coding for entropy encoding
- Produces bit streams with variable-length codes

**DWG R2004 LZ77:**
- **Pure LZ77** sliding window compression
- **No Huffman coding**
- Custom encoding of (offset, length, literal) tuples
- DWG-specific opcodes and bit encoding

**Conclusion:** We **cannot** use `CPLZLibInflate()` directly. We need a **custom DWG LZ77 decompressor**.

#### **What We CAN Reuse from GDAL:**

1. **Bit-stream reading utilities** (from libopencad's existing `CADBuffer`)
2. **Memory management patterns** (CPL allocation functions)
3. **Error handling patterns** (CPLError for reporting)
4. **Testing framework** (GDAL autotest)
5. **Build system integration** (CMake)

#### **What We MUST Implement:**

1. **Custom DWG LZ77 decompressor** following ODA Specification algorithm
2. **R2004 file structure parsing** (headers, section maps, CRC)
3. **64-bit handle support** (extension from R2000's 32-bit)
4. **Version-specific entity decoding** (R2004 format changes)

---

## 2. ODA Specification Analysis for R2004

### 2.1 DWG R2004 LZ77 Algorithm (from ODA Spec)

**Compressed Section Structure:**

```
[Literal Length] - Variable-length integer indicating initial uncompressed bytes
[Sequence of operations]*:
    [Literal Count]      - Number of uncompressed bytes to copy from input
    [Compressed Bytes]   - Number of bytes to copy from output window
    [Compressed Offset]  - Offset backwards from current position
```

**Key Algorithm Properties:**

1. **Sliding Window:** Decompressor maintains a window of recently decompressed bytes
2. **Backreferences:** `(offset, length)` pairs reference previous data
3. **Important:** Length can be > offset (allows for RLE-like patterns)
4. **Hash-based Compression:** Compressor uses hashing for speed (but we only need decompression)

**Pseudo-code from ODA Specification:**

```
Decompression:
    1. Read Literal Length
    2. Copy Literal Length bytes from input → output
    3. While more data:
        a. Read litCount (literal byte count)
        b. Copy litCount bytes from input → output
        c. Read compressedBytes (length of backreference)
        d. Read compOffset (offset backwards in output)
        e. Copy compressedBytes from (output_pos - compOffset) → output
    4. Repeat until section complete
```

**Encoding Details (from ODA Spec):**

- **Variable-length integers:** Use bit codes similar to R2000
  - 2-bit prefix indicates size or special value
  - 00 = special value (0, 256, etc.)
  - 01 = 1 byte follows
  - 10 = 2 bytes follow
  - 11 = 4 bytes follow

- **Opcodes:** DWG defines specific opcodes for different compression operations
  - Literal run
  - Short backreference (offset < 256, length < 16)
  - Long backreference (arbitrary offset/length)
  - End-of-stream marker

### 2.2 R2004 File Structure

**File Layout:**

```
[File Header]
    - Version string: "AC1018"
    - Metadata
    - Image preview pointer
    - Security info
    - Codepage
    - [Section Locator Records] - Pointers to main sections

[Compressed Sections]
    Each section:
        - CRC (16-bit)
        - Compressed data (LZ77)
        - When decompressed:
            - Section-specific data (Header variables, Classes, Objects, etc.)

[Object Map]
    - Maps handles to file offsets

[Preview Image] (optional)
```

**Key Differences from R2000:**

| Feature | R2000 (AC1015) | R2004 (AC1018) |
|---------|---------------|----------------|
| **Sections** | Uncompressed | **LZ77 compressed** |
| **Handles** | 32-bit | **64-bit support** |
| **CRC** | 16-bit | 16-bit (same) |
| **Colors** | ACI palette | **True RGB support** |
| **File Size** | Larger | ~50% smaller (compressed) |

### 2.3 Critical Sections to Decompress

**Priority Order:**

1. **Header Section** (AcDb:Header)
   - Drawing variables (ACADVER, EXTMIN, EXTMAX, etc.)
   - Required for basic file validation

2. **Classes Section** (AcDb:Classes)
   - Custom object class definitions
   - Needed for entity instantiation

3. **Objects Section** (AcDb:Objects)
   - Non-graphical objects (dictionaries, symbol tables)
   - Required for layer/style resolution

4. **Entities Section** (AcDb:BlockRecords → Entities)
   - Graphical entities (LINE, CIRCLE, POLYLINE, etc.)
   - **Primary goal** for GDAL vector data extraction

5. **Handles Section** (AcDb:Handles)
   - Object handle → offset mapping
   - Critical for resolving references

---

## 3. Detailed Implementation Phases

### Phase 1: Foundation & Research (Month 1)

#### **Milestone 1.1: Deep Technical Analysis** (Weeks 1-2)

**Tasks:**

1. **Study ODA Specification Section: "R2004 DWG File Format Organization"**
   - Document all file offsets and structures
   - Extract bit-encoding specifications
   - Map out compression algorithm details
   - Identify all CRC validation points

2. **Study ACadSharp R2004 Implementation**
   - Analyze `ACadSharp/src/ACadSharp/IO/DWG/Decompressors/DwgLZ77AC18Decompressor.cs`
   - Understand opcode handling
   - Document algorithm flow
   - Extract test cases and edge cases

3. **Collect Test Files**
   - Acquire 50+ R2004 DWG files covering:
     - Simple geometry (lines, circles, arcs)
     - Complex polylines and splines
     - Text and dimensions
     - Blocks and xrefs
     - Various layer/style configurations
   - Files from different AutoCAD versions: 2004, 2005, 2006

**Deliverables:**
- Technical specification document (30+ pages)
- Annotated ODA Specification excerpts
- ACadSharp algorithm analysis report
- Test file catalog with metadata

#### **Milestone 1.2: Architecture Design** (Weeks 3-4)

**Tasks:**

1. **Design C++ Class Hierarchy**

   ```cpp
   // Proposed structure

   // Base class (existing)
   class DWGFileR2000 {
   protected:
       virtual CADErrorCodes readHeader();
       virtual CADErrorCodes readClasses();
       virtual CADErrorCodes readObjects();
       // ...
   };

   // New derived class for R2004
   class DWGFileR2004 : public DWGFileR2000 {
   protected:
       // Override to handle compressed sections
       CADErrorCodes readHeader() override;
       CADErrorCodes readClasses() override;
       CADErrorCodes readObjects() override;

       // New decompression method
       CADErrorCodes decompressSection(const char* compressed,
                                       size_t compressedSize,
                                       char** decompressed,
                                       size_t* decompressedSize);

       // New handle parsing (64-bit support)
       CADHandle read64BitHandle(CADBuffer& buffer);
   };

   // Decompressor class
   class DWGLZ77Decompressor {
   public:
       bool decompress(const char* input, size_t inputSize,
                      char* output, size_t outputSize,
                      size_t* actualOutputSize);
   private:
       size_t readVarInt(const char*& ptr);
       void copyLiteral(const char*& input, char*& output, size_t count);
       void copyBackreference(char*& output, size_t offset, size_t length);
   };
   ```

2. **Define File Layout Structures**

   ```cpp
   struct R2004FileHeader {
       char versionString[6];        // "AC1018"
       uint32_t previewAddress;
       uint8_t appVersion;
       uint8_t maintenanceVersion;
       uint16_t codepage;
       // ... (from ODA Spec)
   };

   struct R2004SectionLocator {
       uint32_t address;
       uint32_t size;
       // ...
   };
   ```

3. **Integration Points with Existing Code**
   - Reuse `CADBuffer` for bit-level reading
   - Extend `CADHandle` for 64-bit support
   - Reuse entity classes (`CADLine`, `CADCircle`, etc.)
   - Add version dispatch in `opencad.cpp:82`

**Deliverables:**
- Detailed class diagrams
- Header file drafts (`dwg/r2004.h`)
- Integration architecture document
- Memory management strategy

---

### Phase 2: LZ77 Decompressor Implementation (Months 2-3)

#### **Milestone 2.1: Core Decompressor** (Weeks 5-8)

**Tasks:**

1. **Create Decompressor Files**

   Create: `ogr/ogrsf_frmts/cad/libopencad/dwg/lz77.cpp`
   Create: `ogr/ogrsf_frmts/cad/libopencad/dwg/lz77.h`

   ```cpp
   // lz77.h
   #ifndef LZ77_H
   #define LZ77_H

   #include "opencad.h"

   namespace OpenFileGDB {

   class DWGLZ77Decompressor {
   public:
       DWGLZ77Decompressor();
       ~DWGLZ77Decompressor();

       /**
        * @brief Decompress DWG R2004 LZ77-compressed data
        * @param input Compressed data buffer
        * @param inputSize Size of compressed data
        * @param output Output buffer (must be pre-allocated)
        * @param outputSize Size of output buffer
        * @param actualOutputSize Actual bytes decompressed
        * @return true on success, false on error
        */
       bool decompress(const char* input, size_t inputSize,
                      char* output, size_t outputSize,
                      size_t* actualOutputSize);

   private:
       // Read variable-length integer from DWG bit stream
       size_t readVarInt(const char*& ptr, const char* end);

       // Read literal run length
       size_t readLiteralLength(const char*& ptr, const char* end);

       // Read compression opcode
       uint8_t readOpcode(const char*& ptr, const char* end);

       // Copy literal bytes
       bool copyLiteral(const char*& input, const char* inputEnd,
                       char*& output, const char* outputEnd,
                       size_t count);

       // Copy from backreference
       bool copyBackreference(char*& output, const char* outputStart,
                             const char* outputEnd,
                             size_t offset, size_t length);

       // Error reporting
       void setError(const char* message);

       std::string m_lastError;
   };

   } // namespace OpenFileGDB

   #endif // LZ77_H
   ```

2. **Implement Core Algorithm**

   Based on ODA Specification pseudo-code:

   ```cpp
   bool DWGLZ77Decompressor::decompress(const char* input, size_t inputSize,
                                        char* output, size_t outputSize,
                                        size_t* actualOutputSize) {
       if (!input || !output || inputSize == 0 || outputSize == 0) {
           setError("Invalid parameters");
           return false;
       }

       const char* inputPtr = input;
       const char* inputEnd = input + inputSize;
       char* outputPtr = output;
       const char* outputStart = output;
       const char* outputEnd = output + outputSize;

       // 1. Read initial literal length
       size_t literalLength = readLiteralLength(inputPtr, inputEnd);
       if (literalLength > 0) {
           if (!copyLiteral(inputPtr, inputEnd, outputPtr, outputEnd,
                           literalLength)) {
               return false;
           }
       }

       // 2. Main decompression loop
       while (inputPtr < inputEnd) {
           uint8_t opcode = readOpcode(inputPtr, inputEnd);

           switch (opcode) {
           case OPCODE_LITERAL_RUN: {
               size_t litCount = readVarInt(inputPtr, inputEnd);
               if (!copyLiteral(inputPtr, inputEnd, outputPtr, outputEnd,
                               litCount)) {
                   return false;
               }
               break;
           }

           case OPCODE_SHORT_BACKREF: {
               size_t offset = readVarInt(inputPtr, inputEnd);
               size_t length = readVarInt(inputPtr, inputEnd);
               if (!copyBackreference(outputPtr, outputStart, outputEnd,
                                     offset, length)) {
                   return false;
               }
               break;
           }

           case OPCODE_LONG_BACKREF: {
               // Handle long backreferences
               // ...
               break;
           }

           case OPCODE_END_OF_STREAM:
               goto decompression_complete;

           default:
               setError("Unknown opcode");
               return false;
           }
       }

   decompression_complete:
       if (actualOutputSize) {
           *actualOutputSize = outputPtr - outputStart;
       }

       return true;
   }
   ```

3. **Implement Helper Functions**

   ```cpp
   size_t DWGLZ77Decompressor::readVarInt(const char*& ptr,
                                          const char* end) {
       if (ptr >= end) {
           setError("Unexpected end of input");
           return 0;
       }

       // Read 2-bit prefix
       uint8_t prefix = (*ptr >> 6) & 0x03;

       switch (prefix) {
       case 0x00:  // Special value or single byte
           return handleSpecialValue(ptr, end);
       case 0x01:  // 1-byte value
           return read1ByteValue(ptr, end);
       case 0x02:  // 2-byte value
           return read2ByteValue(ptr, end);
       case 0x03:  // 4-byte value
           return read4ByteValue(ptr, end);
       default:
           return 0;
       }
   }

   bool DWGLZ77Decompressor::copyBackreference(char*& output,
                                               const char* outputStart,
                                               const char* outputEnd,
                                               size_t offset,
                                               size_t length) {
       // Validate parameters
       if (offset == 0 || offset > (output - outputStart)) {
           setError("Invalid backreference offset");
           return false;
       }

       if (output + length > outputEnd) {
           setError("Output buffer overflow");
           return false;
       }

       // Source position in output buffer
       const char* src = output - offset;

       // Copy bytes (handle overlapping case where length > offset)
       for (size_t i = 0; i < length; ++i) {
           *output++ = *src++;
       }

       return true;
   }
   ```

**Deliverables:**
- `dwg/lz77.cpp` and `dwg/lz77.h` (500+ lines)
- Unit test file: `autotest/ogr/cad_lz77_test.cpp`
- 100+ unit tests covering:
  - Simple literal runs
  - Simple backreferences
  - Overlapping backreferences (length > offset)
  - Edge cases (empty input, maximum offsets, etc.)
  - Error conditions

#### **Milestone 2.2: Integration & Validation** (Weeks 9-10)

**Tasks:**

1. **Integrate with CMake Build System**

   Edit: `ogr/ogrsf_frmts/cad/libopencad/CMakeLists.txt`
   ```cmake
   set(LIB_HHEADERS
       ...
       dwg/lz77.h
   )

   set(LIB_CSOURCES
       ...
       dwg/lz77.cpp
   )
   ```

2. **Create Test Vectors from ACadSharp**

   Extract test cases from ACadSharp's test suite:
   - Compressed input (hex dump)
   - Expected decompressed output
   - Validate our implementation matches ACadSharp behavior

3. **Benchmark Performance**

   ```cpp
   // Benchmark against known compressed sections
   void benchmarkDecompression() {
       DWGLZ77Decompressor decompressor;

       // Load test data
       std::vector<char> compressed = loadTestData("r2004_header.bin");
       std::vector<char> output(EXPECTED_SIZE);
       size_t actualSize;

       auto start = std::chrono::high_resolution_clock::now();

       bool success = decompressor.decompress(compressed.data(),
                                              compressed.size(),
                                              output.data(),
                                              output.size(),
                                              &actualSize);

       auto end = std::chrono::high_resolution_clock::now();
       auto duration = std::chrono::duration_cast<std::chrono::microseconds>
                       (end - start);

       printf("Decompressed %zu bytes in %lld µs\n",
              actualSize, duration.count());
   }
   ```

4. **Memory Profiling**
   - Use Valgrind to check for leaks
   - Verify no buffer overruns
   - Stress test with large sections

**Deliverables:**
- Integrated build system
- Test vectors (20+ files)
- Performance benchmark results
- Memory profiling report
- **Working LZ77 decompressor passing all tests**

---

### Phase 3: R2004 File Structure Parsing (Months 3-4)

#### **Milestone 3.1: File Header & Section Locators** (Weeks 11-12)

**Tasks:**

1. **Implement R2004 File Header Reading**

   Create: `ogr/ogrsf_frmts/cad/libopencad/dwg/r2004.cpp`
   Create: `ogr/ogrsf_frmts/cad/libopencad/dwg/r2004.h`

   ```cpp
   // r2004.h
   class DWGFileR2004 : public DWGFileR2000 {
   public:
       DWGFileR2004(CADFileIO* pCADFileIO);
       virtual ~DWGFileR2004();

       CADErrorCodes ParseFile(CADFile::OpenOptions eOptions,
                              bool bReadUnsupportedGeometries) override;

   protected:
       CADErrorCodes readFileHeader();
       CADErrorCodes readSectionLocators();
       CADErrorCodes decompressAndParseSection(uint32_t address,
                                               uint32_t compressedSize,
                                               SectionType type);

       // Version-specific methods
       CADHandle read64BitHandle(CADBuffer& buffer);
       bool verifySectionCRC(const char* data, size_t size, uint16_t expectedCRC);

   private:
       std::unique_ptr<DWGLZ77Decompressor> m_decompressor;
       std::vector<SectionLocator> m_sectionLocators;
   };
   ```

2. **Parse File Header**

   ```cpp
   CADErrorCodes DWGFileR2004::readFileHeader() {
       // Seek to start of file
       m_pCADFileIO->Seek(0, CADFileIO::SeekOrigin::BEG);

       // Read version string
       char versionStr[7] = {0};
       m_pCADFileIO->Read(versionStr, 6);

       if (strncmp(versionStr, "AC1018", 6) != 0) {
           return CADErrorCodes::UNSUPPORTED_VERSION;
       }

       // Read maintenance version
       uint8_t maintenanceVer;
       m_pCADFileIO->Read(&maintenanceVer, 1);

       // Read preview image address
       uint32_t previewAddr;
       m_pCADFileIO->Read(&previewAddr, 4);
       CPL_LSBPTR32(&previewAddr);

       // Read codepage
       uint16_t codepage;
       m_pCADFileIO->Read(&codepage, 2);
       CPL_LSBPTR16(&codepage);

       // ... (continue reading header per ODA spec)

       return CADErrorCodes::SUCCESS;
   }
   ```

3. **Read Section Locators**

   ```cpp
   CADErrorCodes DWGFileR2004::readSectionLocators() {
       // Section locator records are at fixed offset after header
       constexpr uint32_t SECTION_LOCATOR_OFFSET = 0x15;

       m_pCADFileIO->Seek(SECTION_LOCATOR_OFFSET,
                         CADFileIO::SeekOrigin::BEG);

       // Read number of section descriptions
       uint32_t numSections;
       m_pCADFileIO->Read(&numSections, 4);
       CPL_LSBPTR32(&numSections);

       m_sectionLocators.reserve(numSections);

       for (uint32_t i = 0; i < numSections; ++i) {
           SectionLocator locator;

           // Read section ID
           m_pCADFileIO->Read(&locator.id, 4);
           CPL_LSBPTR32(&locator.id);

           // Read section address
           m_pCADFileIO->Read(&locator.address, 4);
           CPL_LSBPTR32(&locator.address);

           // Read section size (compressed)
           m_pCADFileIO->Read(&locator.size, 4);
           CPL_LSBPTR32(&locator.size);

           m_sectionLocators.push_back(locator);
       }

       return CADErrorCodes::SUCCESS;
   }
   ```

**Deliverables:**
- `dwg/r2004.cpp` and `dwg/r2004.h` (initial implementation)
- File header parsing
- Section locator parsing
- CRC validation functions
- Unit tests for header parsing

#### **Milestone 3.2: Section Decompression** (Weeks 13-14)

**Tasks:**

1. **Implement Section Decompression Pipeline**

   ```cpp
   CADErrorCodes DWGFileR2004::decompressAndParseSection(
       uint32_t address,
       uint32_t compressedSize,
       SectionType type) {

       // 1. Seek to section address
       m_pCADFileIO->Seek(address, CADFileIO::SeekOrigin::BEG);

       // 2. Read compressed data
       std::vector<char> compressedData(compressedSize);
       if (m_pCADFileIO->Read(compressedData.data(), compressedSize)
           != compressedSize) {
           return CADErrorCodes::SECTION_READ_FAILED;
       }

       // 3. Read CRC (last 2 bytes)
       uint16_t storedCRC;
       memcpy(&storedCRC,
              compressedData.data() + compressedSize - 2, 2);
       CPL_LSBPTR16(&storedCRC);

       // 4. Verify CRC (excluding CRC itself)
       if (!verifySectionCRC(compressedData.data(),
                            compressedSize - 2, storedCRC)) {
           CPLError(CE_Failure, CPLE_AppDefined,
                   "CRC mismatch in section at 0x%X", address);
           return CADErrorCodes::FILE_PARSE_FAILED;
       }

       // 5. Decompress
       // First, we need to know decompressed size
       // (often stored in first few bytes of compressed data)
       uint32_t decompressedSize;
       memcpy(&decompressedSize, compressedData.data(), 4);
       CPL_LSBPTR32(&decompressedSize);

       std::vector<char> decompressedData(decompressedSize);
       size_t actualSize;

       if (!m_decompressor->decompress(
               compressedData.data() + 4,  // Skip size header
               compressedSize - 6,          // Exclude size and CRC
               decompressedData.data(),
               decompressedSize,
               &actualSize)) {
           CPLError(CE_Failure, CPLE_AppDefined,
                   "Failed to decompress section");
           return CADErrorCodes::FILE_PARSE_FAILED;
       }

       // 6. Parse decompressed section based on type
       CADBuffer buffer(decompressedData.data(), actualSize);

       switch (type) {
       case SectionType::HEADER:
           return parseHeaderSection(buffer);
       case SectionType::CLASSES:
           return parseClassesSection(buffer);
       case SectionType::OBJECTS:
           return parseObjectsSection(buffer);
       // ...
       default:
           return CADErrorCodes::SUCCESS; // Ignore unknown sections
       }
   }
   ```

2. **Implement CRC Verification**

   ```cpp
   bool DWGFileR2004::verifySectionCRC(const char* data,
                                       size_t size,
                                       uint16_t expectedCRC) {
       // Use existing CRC implementation from R2000
       // (libopencad already has CalculateCRC8 in io.cpp)
       uint16_t calculatedCRC = CalculateCRC8(0, data, size);

       // XOR with DWG magic number
       calculatedCRC ^= 0xA598;  // DWG CRC magic number

       return calculatedCRC == expectedCRC;
   }
   ```

**Deliverables:**
- Section decompression pipeline
- CRC verification
- Integration tests with real R2004 files
- Error handling and logging

#### **Milestone 3.3: 64-bit Handle Support** (Weeks 15-16)

**Tasks:**

1. **Extend CADHandle for 64-bit**

   Edit: `ogr/ogrsf_frmts/cad/libopencad/cadheader.h`

   ```cpp
   class OCAD_EXTERN CADHandle final {
   public:
       explicit CADHandle(unsigned char codeIn = 0);

       // Add 64-bit accessors
       int64_t getAsLong64() const;
       int64_t getAsLong64(const CADHandle& ref_handle) const;

       // Existing 32-bit methods still available for R2000 compatibility
       long getAsLong() const;

   private:
       static int64_t getAsLong64(const std::vector<unsigned char>& handle);

       unsigned char code;
       std::vector<unsigned char> handleOrOffset;
   };
   ```

2. **Implement 64-bit Handle Reading**

   ```cpp
   CADHandle DWGFileR2004::read64BitHandle(CADBuffer& buffer) {
       // R2004 supports both 32-bit and 64-bit handles
       // Read code byte to determine size
       unsigned char code = buffer.Read2B();

       CADHandle handle(code);

       // Determine handle size from code
       int handleSize = getHandleSize(code);  // Can be 1-8 bytes

       for (int i = 0; i < handleSize; ++i) {
           handle.addOffset(buffer.Read2B());
       }

       return handle;
   }
   ```

**Deliverables:**
- Extended CADHandle class
- 64-bit handle reading
- Handle resolution tests
- Backward compatibility with R2000

---

### Phase 4: Header & Classes Sections (Month 4)

#### **Milestone 4.1: Header Section Parsing** (Weeks 17-18)

**Tasks:**

1. **Parse Decompressed Header Section**

   ```cpp
   CADErrorCodes DWGFileR2004::parseHeaderSection(CADBuffer& buffer) {
       // Header variables similar to R2000 but with some additions

       // Reuse existing header parsing from R2000
       oHeader.addValue(CADHeader::OPENCADVER, CADVersions::DWG_R2004);

       // Read standard header variables
       // (Most are same as R2000, some new in R2004)

       // ACADVER
       std::string acadVer = buffer.ReadTV();
       oHeader.addValue(CADHeader::ACADVER, acadVer);

       // EXTMIN (drawing extents minimum)
       double extmin_x = buffer.ReadBD();
       double extmin_y = buffer.ReadBD();
       double extmin_z = buffer.ReadBD();
       oHeader.addValue(CADHeader::EXTMIN, extmin_x, extmin_y, extmin_z);

       // ... (continue for all header variables per ODA spec)

       return CADErrorCodes::SUCCESS;
   }
   ```

2. **Validate Header Data**
   - Check ACADVER matches "AC1018", "AC1019", or "AC1020" (R2004-2006)
   - Validate drawing extents are reasonable
   - Check mandatory variables are present

**Deliverables:**
- Header section parser
- Header validation
- Tests with real R2004 headers

#### **Milestone 4.2: Classes Section** (Week 19)

**Tasks:**

1. **Parse Classes Section**

   ```cpp
   CADErrorCodes DWGFileR2004::parseClassesSection(CADBuffer& buffer) {
       // Classes define custom object types

       uint32_t numClasses = buffer.ReadBL();

       for (uint32_t i = 0; i < numClasses; ++i) {
           ClassDefinition classDef;

           classDef.classNum = buffer.ReadBL();
           classDef.proxyFlags = buffer.ReadBL();
           classDef.appName = buffer.ReadTV();
           classDef.className = buffer.ReadTV();
           classDef.dxfName = buffer.ReadTV();
           classDef.wasZombie = buffer.ReadB();
           classDef.itemClassId = buffer.ReadBL();

           m_classes.push_back(classDef);
       }

       return CADErrorCodes::SUCCESS;
   }
   ```

**Deliverables:**
- Classes section parser
- Class registry
- Tests

---

### Phase 5: Entity Support & Testing (Months 5-6)

#### **Milestone 5.1: Core Entity Parsing** (Weeks 20-23)

**Tasks:**

1. **Update Entity Parsers for R2004 Format**

   Most entities have minor format changes in R2004:
   - Extended color support (true RGB)
   - 64-bit handle references
   - Additional attributes

   ```cpp
   // Example: Update LINE entity parser
   CADErrorCodes DWGFileR2004::readLineEntity(CADBuffer& buffer,
                                              CADLine* line) {
       // Call base R2000 parser for common fields
       DWGFileR2000::readLineEntity(buffer, line);

       // Read R2004-specific additions
       if (buffer.hasRGBColor()) {
           line->setTrueColor(buffer.ReadCMC());  // RGB color
       }

       // Read 64-bit owner handle
       CADHandle ownerHandle = read64BitHandle(buffer);
       line->setOwnerHandle(ownerHandle.getAsLong64());

       return CADErrorCodes::SUCCESS;
   }
   ```

2. **Support New R2004 Entities**

   New entity types introduced in R2004:
   - **MLEADER** (multi-leader)
   - **TABLE** (table object)
   - Enhanced **MTEXT**
   - Enhanced **DIMENSION**

   Priority: MLEADER and TABLE (others can be added later)

**Deliverables:**
- Updated entity parsers for all R2000 entity types
- MLEADER entity support
- TABLE entity support
- Entity parsing tests

#### **Milestone 5.2: Objects & Blocks** (Weeks 24-25)

**Tasks:**

1. **Parse Objects Section**
   - Symbol tables (LAYER, LTYPE, STYLE, etc.)
   - Dictionaries
   - Non-graphical objects

2. **Parse Block Records**
   - Block definitions
   - Block references (INSERT)
   - Nested blocks

**Deliverables:**
- Objects section parser
- Block handling
- Layer/style resolution

#### **Milestone 5.3: Comprehensive Testing** (Weeks 26-28)

**Tasks:**

1. **Unit Tests** (Target: 200+ tests)
   - Decompressor unit tests
   - Header parsing tests
   - Entity parsing tests
   - Handle resolution tests
   - Error condition tests

2. **Integration Tests** (Target: 50+ R2004 files)
   - Simple drawings (lines, circles, arcs)
   - Complex drawings (thousands of entities)
   - Drawings with blocks and xrefs
   - Drawings with various encodings (UTF-8, etc.)
   - Corrupt file handling

3. **Regression Tests**
   - Ensure R2000 files still work
   - No performance degradation for R2000
   - Memory usage acceptable

4. **Compatibility Testing**
   - Test against AutoCAD 2004, 2005, 2006 outputs
   - Cross-validate with ACadSharp
   - Compare with LibreDWG results (visual inspection only)

5. **Performance Benchmarking**
   - Decompression speed
   - Overall file reading speed
   - Memory usage
   - Target: Within 2x of R2000 performance

**Example Test Structure:**

```cpp
// Test file: autotest/ogr/cad_r2004_test.cpp

TEST(CADR2004, BasicFileOpen) {
    CADFile* pFile = OpenCADFile("data/simple_r2004.dwg",
                                 CADFile::READ_ALL);
    ASSERT_NE(pFile, nullptr);
    EXPECT_EQ(pFile->GetVersion(), CADVersions::DWG_R2004);
    delete pFile;
}

TEST(CADR2004, DecompressHeaderSection) {
    CADFile* pFile = OpenCADFile("data/r2004_with_layers.dwg",
                                 CADFile::READ_ALL);
    ASSERT_NE(pFile, nullptr);

    CADHeader header = pFile->getHeader();
    std::string acadVer = header.getValue(CADHeader::ACADVER).getString();
    EXPECT_EQ(acadVer, "AC1018");

    delete pFile;
}

TEST(CADR2004, ReadLineEntities) {
    CADFile* pFile = OpenCADFile("data/r2004_lines.dwg",
                                 CADFile::READ_ALL);
    ASSERT_NE(pFile, nullptr);

    CADLayer* pLayer = pFile->GetLayer(0);
    ASSERT_NE(pLayer, nullptr);

    size_t entityCount = pLayer->GetGeometryCount();
    EXPECT_GT(entityCount, 0);

    CADGeometry* pGeom = pLayer->GetGeometry(0);
    EXPECT_EQ(pGeom->getType(), CADGeometry::LINE);

    delete pFile;
}

TEST(CADR2004, Handle64BitSupport) {
    // Test that 64-bit handles work correctly
    CADFile* pFile = OpenCADFile("data/r2004_large.dwg",
                                 CADFile::READ_ALL);
    ASSERT_NE(pFile, nullptr);

    // Verify entities with high handle values
    CADLayer* pLayer = pFile->GetLayer(0);
    CADGeometry* pGeom = pLayer->GetGeometry(0);

    int64_t handle = pGeom->getHandle();
    EXPECT_GT(handle, 0xFFFFFFFF);  // Exceeds 32-bit

    delete pFile;
}
```

**Deliverables:**
- Complete test suite (200+ tests)
- 50+ real-world R2004 test files
- Test coverage report (target: >90%)
- Performance benchmark results
- **R2004 READ SUPPORT COMPLETE** ✓

---

## 4. Integration with GDAL

### 4.1 Version Dispatch Update

Edit: `ogr/ogrsf_frmts/cad/libopencad/opencad.cpp`

```cpp
// Line 82-91 (existing code)
CADFile * OpenCADFile( CADFileIO * pCADFileIO,
                       enum CADFile::OpenOptions eOptions,
                       bool bReadUnsupportedGeometries )
{
    int nCADFileVersion = CheckCADFile( pCADFileIO );
    CADFile * poCAD = nullptr;

    switch( nCADFileVersion )
    {
        case CADVersions::DWG_R2000:
            poCAD = new DWGFileR2000( pCADFileIO );
            break;

        // ADD THIS CASE:
        case CADVersions::DWG_R2004:
            poCAD = new DWGFileR2004( pCADFileIO );
            break;

        default:
            gLastError = CADErrorCodes::UNSUPPORTED_VERSION;
            delete pCADFileIO;
            return nullptr;
    }

    gLastError = poCAD->ParseFile( eOptions, bReadUnsupportedGeometries );
    if( gLastError != CADErrorCodes::SUCCESS )
    {
        delete poCAD;
        return nullptr;
    }

    return poCAD;
}
```

### 4.2 CMake Build System Updates

Edit: `ogr/ogrsf_frmts/cad/libopencad/CMakeLists.txt`

```cmake
set(LIB_HHEADERS
    cadgeometry.h
    cadheader.h
    cadlayer.h
    # ... existing headers ...
    dwg/r2000.h
    dwg/r2004.h        # ADD
    dwg/io.h
    dwg/lz77.h         # ADD
)

set(LIB_CSOURCES
    opencad.cpp
    opencad_api.cpp
    # ... existing sources ...
    dwg/r2000.cpp
    dwg/r2004.cpp      # ADD
    dwg/io.cpp
    dwg/lz77.cpp       # ADD
)
```

### 4.3 Documentation Updates

Edit: `doc/source/drivers/vector/cad.rst`

```rst
Driver capabilities
-------------------

.. supports_georeferencing::

.. supports_virtualio::

Supported DWG Versions
----------------------

The CAD driver supports the following DWG file format versions:

- **R2000 (AC1015)**: AutoCAD 2000/2001/2002 - Full support
- **R2004 (AC1018)**: AutoCAD 2004/2005/2006 - Full support (as of GDAL X.X)

Future versions planned:
- R2007 (AC1021): AutoCAD 2007/2008/2009
- R2010 (AC1024): AutoCAD 2010/2011/2012
- R2013 (AC1027): AutoCAD 2013-2017
- R2018 (AC1032): AutoCAD 2018-2025

Compression
-----------

R2004 and later versions use LZ77 compression for section data. The driver
includes a custom LZ77 decompressor specifically designed for the DWG format
variant.
```

### 4.4 Autotest Integration

Create: `autotest/ogr/ogr_cad_r2004.py`

```python
#!/usr/bin/env pytest
###############################################################################
# $Id$
#
# Project:  GDAL/OGR Test Suite
# Purpose:  Test OGR CAD driver (DWG R2004 support)
# Author:   [Your Name]
#
###############################################################################

import pytest
import osgeo.ogr as ogr
import osgeo.gdal as gdal


def test_ogr_cad_r2004_basic_read():
    """Test basic R2004 file opening and reading"""

    ds = ogr.Open('data/cad/simple_r2004.dwg')
    assert ds is not None, 'Failed to open R2004 DWG file'

    layer = ds.GetLayer(0)
    assert layer is not None

    feature_count = layer.GetFeatureCount()
    assert feature_count > 0, 'No features found in R2004 file'

    ds = None


def test_ogr_cad_r2004_line_entities():
    """Test reading LINE entities from R2004"""

    ds = ogr.Open('data/cad/r2004_lines.dwg')
    assert ds is not None

    layer = ds.GetLayer(0)
    feature = layer.GetNextFeature()

    assert feature is not None
    geom = feature.GetGeometryRef()
    assert geom is not None
    assert geom.GetGeometryType() == ogr.wkbLineString

    ds = None


def test_ogr_cad_r2004_attributes():
    """Test reading attributes from R2004"""

    ds = ogr.Open('data/cad/r2004_with_attributes.dwg')
    layer = ds.GetLayer(0)
    feature = layer.GetNextFeature()

    # Check layer name
    layer_name = feature.GetField('Layer')
    assert layer_name is not None

    # Check color
    color = feature.GetField('Color')
    assert color is not None

    ds = None


# Add 50+ more tests...
```

---

## 5. Documentation & Deliverables

### 5.1 Technical Documentation

1. **Implementation Report** (30-50 pages)
   - Architecture overview
   - Algorithm descriptions
   - API documentation
   - Performance analysis
   - Known limitations

2. **Developer Guide**
   - How to extend to R2007/R2010/etc.
   - Code organization
   - Testing strategy
   - Debugging tips

3. **User Documentation**
   - Updated CAD driver docs
   - Supported features matrix
   - Known issues
   - Migration guide from ODA driver

### 5.2 Code Deliverables

**New Files:**
```
ogr/ogrsf_frmts/cad/libopencad/
├── dwg/
│   ├── lz77.cpp                    (~500 lines)
│   ├── lz77.h                      (~100 lines)
│   ├── r2004.cpp                   (~1500 lines)
│   └── r2004.h                     (~200 lines)
```

**Modified Files:**
```
ogr/ogrsf_frmts/cad/libopencad/
├── opencad.cpp                     (version dispatch)
├── opencad_api.h                   (DWG_R2004 added to enum)
├── cadheader.h                     (64-bit handle support)
├── CMakeLists.txt                  (build system)
```

**Test Files:**
```
autotest/ogr/
├── ogr_cad_r2004.py                (~1000 lines, 50+ tests)
├── data/cad/
│   ├── simple_r2004.dwg
│   ├── r2004_lines.dwg
│   ├── r2004_complex.dwg
│   └── ... (50+ test files)
```

**Documentation:**
```
doc/source/drivers/vector/
├── cad.rst                         (updated)
```

### 5.3 Metrics & Success Criteria

| Metric | Target | Validation Method |
|--------|--------|-------------------|
| **Code Coverage** | >90% | `gcov` / `lcov` |
| **Entity Accuracy** | >95% | Visual comparison with AutoCAD |
| **File Compatibility** | 100% | All test files open without errors |
| **Performance** | <2x R2000 | Benchmark suite |
| **Memory Leaks** | Zero | Valgrind |
| **Compiler Warnings** | Zero | `-Wall -Wextra` |

---

## 6. Risk Mitigation

### 6.1 Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| LZ77 decompressor bugs | MEDIUM | HIGH | Extensive unit testing; cross-validate with ACadSharp |
| ODA spec ambiguities | MEDIUM | HIGH | Test with real files; consult ACadSharp implementation |
| Performance issues | LOW | MEDIUM | Profile early; optimize hot paths |
| Memory issues | LOW | HIGH | Valgrind from day 1; ASAN/UBSAN |
| 64-bit handle bugs | MEDIUM | MEDIUM | Dedicated test suite for large handles |

### 6.2 Schedule Risks

| Risk | Mitigation |
|------|------------|
| LZ77 implementation takes longer than estimated | Allocate 2 extra weeks buffer; consider simpler initial implementation |
| Test file acquisition delays | Start collecting files in Month 1 |
| Integration issues with GDAL | Regular builds against GDAL master |

---

## 7. Timeline Summary

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| **Phase 1: Foundation** | Month 1 | Technical specs, architecture design |
| **Phase 2: LZ77 Decompressor** | Months 2-3 | Working LZ77 decompressor with tests |
| **Phase 3: File Structure** | Months 3-4 | Header/section parsing, CRC validation |
| **Phase 4: Header & Classes** | Month 4 | Header and classes section parsers |
| **Phase 5: Entities & Testing** | Months 5-6 | Entity support, comprehensive tests |
| **TOTAL** | **6 months** | **R2004 read support complete** |

---

## 8. Next Steps (Post-R2004)

Once R2004 is complete and stable:

1. **R2007 (AC1021)** - 4 months
   - Enhanced LZ77 variant
   - Reed-Solomon error correction
   - Metadata section

2. **R2010 (AC1024)** - 4 months
   - Minimal changes from R2007
   - ACIS version updates

3. **R2013 (AC1027)** - 6 months
   - Reverts to R2004-style structure (easier!)
   - New entity types

4. **R2018 (AC1032)** - 8 months
   - Current industry standard
   - Security enhancements

---

## 9. References

### Primary Technical References

1. **Open Design Specification for .dwg files (Version 5.4.1)**
   - URL: https://www.opendesign.com/files/guestdownloads/OpenDesign_Specification_for_.dwg_files.pdf
   - Section: "R2004 DWG File Format Organization"

2. **ACadSharp R2004 Implementation**
   - File: `ACadSharp/src/ACadSharp/IO/DWG/Decompressors/DwgLZ77AC18Decompressor.cs`
   - GitHub: https://github.com/DomCR/ACadSharp

3. **GDAL Compression Infrastructure**
   - `port/cpl_compressor.h` - Compressor framework
   - `port/cpl_vsil_gzip.cpp` - CPLZLibInflate implementation
   - `frmts/zlib/` - Embedded zlib library

4. **Existing libopencad R2000 Implementation**
   - `ogr/ogrsf_frmts/cad/libopencad/dwg/r2000.cpp`
   - Foundation for R2004 implementation

---

## Appendix A: LZ77 Algorithm Deep Dive

### A.1 Standard LZ77 vs DWG LZ77

**Standard LZ77 (DEFLATE):**
```
Output: [(distance, length, next_char), ...]
Example: "ABCDABCDABCD"
         → [(0, 0, 'A'), (0, 0, 'B'), (0, 0, 'C'), (0, 0, 'D'),
            (4, 4, 'A'), (4, 4, '\0')]
Then Huffman-encoded to bit stream
```

**DWG LZ77:**
```
Output: [literal_run, (offset, length), ...]
Example: "ABCDABCDABCD"
         → [literal_length=4, "ABCD",
            offset=4, length=8]  // Copy 8 bytes from 4 bytes back
No Huffman encoding, custom bit-level encoding
```

### A.2 DWG LZ77 Opcodes (Hypothetical)

Based on analysis of ACadSharp:

```
Opcode    Meaning
------    -------
0x00      Literal run follows (1-256 bytes)
0x01      Short backref (offset < 256, length < 16)
0x02      Long backref (arbitrary offset/length)
0x03      Special: repeat last byte N times
...
0xFF      End of compressed stream
```

### A.3 Overlapping Copy Example

```
Input: "AAAAAA" compressed as: literal="A", backref(offset=1, length=5)

Decompression:
Output buffer: [A _ _ _ _ _]
                ^
Copy from (pos - 1) = position 0:
Output buffer: [A A _ _ _ _]
                  ^
Copy from (pos - 1) = position 1:
Output buffer: [A A A _ _ _]
                    ^
... and so on ...
Output buffer: [A A A A A A]

This works because we copy byte-by-byte, so each byte
becomes available for the next copy operation.
```

---

## Appendix B: Sample Code Snippets

### B.1 Complete LZ77 Decompressor Class

See implementation in Phase 2, Milestone 2.1 above.

### B.2 CRC Calculation

```cpp
// From existing libopencad (io.cpp)
// Extended for R2004

const int DWGCRC8Table[256] = { /* ... */ };

unsigned short CalculateCRC8(unsigned short initialVal,
                            const char *ptr, int num) {
    unsigned char al;
    while(num-- > 0) {
        al = static_cast<unsigned char>((*ptr) ^
                                       (static_cast<char>(initialVal & 0xFF)));
        initialVal = (initialVal >> 8) & 0xFF;
        initialVal = static_cast<unsigned short>(
                        initialVal ^ DWGCRC8Table[al & 0xFF]);
        ptr++;
    }
    return initialVal;
}

// R2004 CRC with magic number
uint16_t CalculateR2004CRC(const char *data, size_t size) {
    uint16_t crc = CalculateCRC8(0, data, size);
    return crc ^ 0xA598;  // DWG magic number
}
```

---

**END OF IMPLEMENTATION PLAN**

**Document Version:** 1.0
**Last Updated:** November 10, 2025
**Total Pages:** 37
