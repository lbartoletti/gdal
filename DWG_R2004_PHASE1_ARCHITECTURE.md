# DWG R2004 Support - Phase 1 Architectural Design
## libopencad Extension for Compressed DWG Formats

**Date:** November 10, 2025
**Architect:** Distinguished Software Architect
**Project:** GDAL libopencad R2004 Implementation
**Phase:** 1 - Foundation & Architecture
**Status:** Design Document v1.0

---

## 1. Problem Definition

### Requirements

**Primary Goal:** Add support for reading DWG R2004 (AC1018) files to GDAL's libopencad library.

**Functional Requirements:**
1. Detect and validate R2004 DWG files (version string "AC1018")
2. Decompress LZ77-compressed sections
3. Parse R2004 file headers and section locators
4. Read and parse decompressed sections (Header, Classes, Objects, Entities)
5. Support 64-bit handles (extension from R2000's 32-bit)
6. Extract geometry from entities with ≥95% accuracy
7. Maintain backward compatibility with R2000

**Non-Functional Requirements:**
1. **Performance:** Decompression + parsing within 2x of R2000 speed
2. **Memory:** No leaks, reasonable working set (<2x file size)
3. **Reliability:** Handle corrupt files gracefully without crashes
4. **Maintainability:** Code comprehensible to future developers
5. **Testability:** Unit-testable components
6. **Portability:** Works on Linux, Windows, macOS (32/64-bit)

### Constraints

**Technical Constraints:**
- Must integrate with existing libopencad architecture (R2000 base)
- C++ only (matches GDAL/libopencad codebase)
- Cannot use external compression libraries directly (DWG LZ77 is custom)
- Must respect GDAL's MIT license (no GPL code)
- Zero new external dependencies beyond what GDAL already has

**Operational Constraints:**
- Must build with GDAL's CMake system
- Must pass GDAL's autotest framework
- Must work with GDAL's error reporting (CPLError)
- Must use GDAL's memory management (VSIMalloc/VSIFree where appropriate)

**Development Constraints:**
- 6-month timeline for R2004 complete
- Foundation architecture must support R2007/R2010/R2013 later
- Must be implementable by 1-2 senior C++ developers

---

## 2. Architectural Overview

### Design Philosophy

**UNIX Philosophy Applied:**
- **Do one thing well:** Each class has a single, clear responsibility
- **Composition:** Build R2004 support by composing with R2000 base
- **Simplicity:** Avoid over-engineering; no fancy patterns unless justified

**KISS Principle:**
- Minimal class hierarchy (don't create abstractions for future versions prematurely)
- Straightforward decompression algorithm (match ODA spec closely)
- Simple error handling (return codes, CPLError for user-facing errors)
- No template metaprogramming tricks

**Proven Technology:**
- Pure C++11 (GDAL's current standard)
- Standard library containers (std::vector, std::string)
- Existing CADBuffer bit-reading utilities from R2000
- GDAL's error/memory infrastructure

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     GDAL OGR Application                    │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  │ OpenCADFile("file.dwg", ...)
                  ▼
         ┌────────────────────┐
         │  opencad.cpp       │  Version Dispatch
         │  OpenCADFile()     │  switch(version) {
         └─────────┬──────────┘    case DWG_R2000: ...
                   │                case DWG_R2004: ... ◄── NEW
                   │              }
      ┌────────────┴────────────┐
      │                         │
      ▼                         ▼
┌──────────────┐      ┌──────────────────┐
│ DWGFileR2000 │      │  DWGFileR2004    │ ◄── NEW
│ (existing)   │      │  (extends R2000) │
└──────────────┘      └────────┬─────────┘
                               │
                               │ uses
                    ┌──────────┴──────────┐
                    │                     │
                    ▼                     ▼
         ┌─────────────────┐   ┌──────────────────┐
         │ DWGLZ77         │   │ Enhanced         │
         │ Decompressor    │   │ CADHandle (64b)  │
         │                 │   │                  │
         └─────────────────┘   └──────────────────┘
                    │
                    │ reads compressed data
                    ▼
         ┌─────────────────────┐
         │  CADBuffer          │  (existing bit reader)
         │  bit-level reading  │
         └─────────────────────┘
```

**Key Design Decisions:**

1. **Inheritance over Composition:** `DWGFileR2004` extends `DWGFileR2000`
   - *Why:* ~80% of R2000 code is reusable (entity parsing, data structures)
   - *Tradeoff:* Slight coupling, but avoids massive code duplication
   - *Mitigation:* Override only section-reading methods, not entity parsing

2. **Separate Decompressor Class:** `DWGLZ77Decompressor` is standalone
   - *Why:* Single responsibility, unit-testable in isolation
   - *Tradeoff:* Extra class, but worth it for testability
   - *Future:* Can have `DWGLZ77AC18` and `DWGLZ77AC21` subclasses for R2007

3. **Minimal Handle Changes:** Extend existing `CADHandle`, don't replace
   - *Why:* R2000 code continues to work unchanged
   - *Tradeoff:* `CADHandle` becomes slightly more complex
   - *Mitigation:* Keep 32-bit paths unchanged, add 64-bit accessors

---

## 3. Component Breakdown

### 3.1 DWGFileR2004 Class

**Location:** `ogr/ogrsf_frmts/cad/libopencad/dwg/r2004.h/cpp`

**Responsibility:**
- Read and parse R2004 DWG file structure
- Decompress sections using DWGLZ77Decompressor
- Delegate entity parsing to inherited R2000 methods where possible

**Interface:**
```cpp
class DWGFileR2004 : public DWGFileR2000 {
public:
    DWGFileR2004(CADFileIO* pCADFileIO);
    virtual ~DWGFileR2004();

    // Override R2000's ParseFile to handle compressed sections
    CADErrorCodes ParseFile(CADFile::OpenOptions eOptions,
                           bool bReadUnsupportedGeometries) override;

protected:
    // Version-specific section reading
    CADErrorCodes readFileHeader() override;
    CADErrorCodes readSectionLocators();
    CADErrorCodes readSection(uint8_t sectionId);

    // Decompression pipeline
    CADErrorCodes decompressSection(uint32_t address,
                                    uint32_t compressedSize,
                                    std::vector<char>& decompressed);

    // CRC validation
    bool verifyCRC16(const char* data, size_t size, uint16_t expectedCRC);

    // 64-bit handle support
    CADHandle readHandle64(CADBuffer& buffer);

private:
    std::unique_ptr<DWGLZ77Decompressor> m_decompressor;

    struct SectionLocator {
        uint8_t id;
        uint32_t address;
        uint32_t size;
    };
    std::vector<SectionLocator> m_sectionLocators;
};
```

**Design Rationale:**
- *Extends DWGFileR2000:* Reuses 80% of code (entity parsing, data structures)
- *Override only section methods:* Minimal changes to base class behavior
- *Owns decompressor:* Created once, reused for all sections
- *Simple data structures:* SectionLocator is POD, easy to understand

**Why Not More Abstract?**
- Could create `IDWGFile` interface with `readSection()` virtual method
- **Rejected:** Premature abstraction. R2000 and R2004 share more than they differ.
- **YAGNI:** We'll add R2007 layer when needed, not before

### 3.2 DWGLZ77Decompressor Class

**Location:** `ogr/ogrsf_frmts/cad/libopencad/dwg/lz77.h/cpp`

**Responsibility:**
- Decompress DWG-specific LZ77 compressed data
- Single purpose: byte array in → byte array out
- **No file I/O, no GDAL dependencies** (pure algorithm)

**Interface:**
```cpp
class DWGLZ77Decompressor {
public:
    DWGLZ77Decompressor();
    ~DWGLZ77Decompressor() = default;

    /**
     * @brief Decompress DWG R2004 LZ77 data
     * @param input Compressed data
     * @param inputSize Size of compressed data in bytes
     * @param output Pre-allocated output buffer
     * @param outputSize Size of output buffer (must be large enough)
     * @param actualOutputSize [out] Actual decompressed size
     * @return true on success, false on error
     *
     * Error details available via getLastError()
     */
    bool decompress(const char* input, size_t inputSize,
                   char* output, size_t outputSize,
                   size_t* actualOutputSize);

    // Error reporting
    const char* getLastError() const { return m_lastError.c_str(); }

private:
    // Bit-level reading helpers
    uint8_t readOpcode(const char*& ptr, const char* end);
    size_t readVarInt(const char*& ptr, const char* end);
    size_t readLiteralLength(const char*& ptr, const char* end);

    // Copy operations
    bool copyLiteral(const char*& input, const char* inputEnd,
                    char*& output, const char* outputEnd,
                    size_t count);
    bool copyBackreference(char*& output, const char* outputStart,
                          const char* outputEnd,
                          size_t offset, size_t length);

    // Error state
    std::string m_lastError;

    // Prevent copies (move-only if needed later)
    DWGLZ77Decompressor(const DWGLZ77Decompressor&) = delete;
    DWGLZ77Decompressor& operator=(const DWGLZ77Decompressor&) = delete;
};
```

**Design Rationale:**
- *Pure algorithm class:* No I/O, no global state, testable in isolation
- *Caller provides buffer:* Avoids allocation; caller knows size from section header
- *Error via string:* Simple, debuggable (CPLError can wrap this at call site)
- *Stateless between calls:* Each decompress() is independent

**Why Not Use std::vector for output?**
- Could have `bool decompress(const char*, size_t, std::vector<char>&)`
- **Rejected:** Caller already knows decompressed size from section header
- **Performance:** Pre-allocated buffer avoids reallocation
- **Control:** Caller manages memory lifetime (matches R2000 pattern)

**Why Not Template for Input Type?**
- Could template `<typename InputIterator>`
- **Rejected:** YAGNI. Always decompressing from `char*` in practice
- **Simplicity:** Concrete types are easier to debug and understand

### 3.3 CADHandle Extensions

**Location:** `ogr/ogrsf_frmts/cad/libopencad/cadheader.h` (modify existing)

**Changes:**
```cpp
class OCAD_EXTERN CADHandle final {
public:
    // Existing constructors/methods unchanged

    // NEW: 64-bit accessors
    int64_t getAsLong64() const;
    int64_t getAsLong64(const CADHandle& ref_handle) const;

    // Existing methods still work for R2000 compatibility
    long getAsLong() const;  // Still returns 32-bit

private:
    // Internal implementation now supports up to 8 bytes
    static int64_t getAsLong64(const std::vector<unsigned char>& handle);

    // Existing fields unchanged
    unsigned char code;
    std::vector<unsigned char> handleOrOffset;
};
```

**Design Rationale:**
- *Extend, don't replace:* R2000 code uses `getAsLong()`, still works
- *New methods for 64-bit:* R2004 code uses `getAsLong64()`
- *Same storage:* `std::vector<unsigned char>` already variable-length

**Why Not Create CADHandle64?**
- Could have separate class for 64-bit handles
- **Rejected:** Unnecessary duplication. Same wire format, just read more bytes
- **Simplicity:** One class, one set of tests

---

## 4. Data Flow

### 4.1 File Reading Flow

```
User opens DWG file
    │
    ├─> opencad.cpp: CheckCADFile()
    │       └─> Read version string "AC1018"
    │       └─> Return CADVersions::DWG_R2004
    │
    ├─> opencad.cpp: OpenCADFile()
    │       └─> switch(DWG_R2004):
    │               └─> new DWGFileR2004(fileIO)
    │
    ├─> DWGFileR2004::ParseFile()
    │   │
    │   ├─> readFileHeader()
    │   │   └─> Seek to 0, read "AC1018" + metadata
    │   │
    │   ├─> readSectionLocators()
    │   │   └─> Read section map (ID, address, size for each section)
    │   │
    │   └─> For each section (Header, Classes, Objects, Entities):
    │       │
    │       ├─> Seek to section address
    │       ├─> Read compressed data (+ CRC)
    │       ├─> verifyCRC16()
    │       ├─> decompressSection() ──> DWGLZ77Decompressor::decompress()
    │       └─> Parse decompressed data (reuse R2000 parsers where possible)
    │
    └─> Return CADFile* to user
```

### 4.2 Decompression Flow (Critical Path)

```
decompressSection(address, compressedSize, decompressed)
    │
    ├─> Seek to address in file
    ├─> Read compressedSize bytes into temp buffer
    ├─> Extract decompressed size from header (first 4 bytes)
    ├─> Resize decompressed vector to decompressed size
    │
    ├─> DWGLZ77Decompressor::decompress()
    │   │
    │   ├─> Read initial literal length
    │   ├─> Copy literal bytes: input → output
    │   │
    │   ├─> Loop until end of input:
    │   │   ├─> Read opcode
    │   │   ├─> Switch on opcode:
    │   │   │   ├─> LITERAL_RUN: read count, copy input → output
    │   │   │   ├─> SHORT_BACKREF: read offset/length, copy output[pos-offset] → output
    │   │   │   ├─> LONG_BACKREF: (similar)
    │   │   │   └─> END_OF_STREAM: break
    │   │   └─> Advance pointers
    │   │
    │   └─> Return success
    │
    └─> Parse decompressed data with CADBuffer
```

### 4.3 Handle Reading (32-bit vs 64-bit)

```
R2000 code:
    handle = readHandle(buffer)  // Reads 32-bit handle
    id = handle.getAsLong()      // Returns long (32-bit)

R2004 code:
    handle = readHandle64(buffer)  // Reads up to 64-bit handle
    id = handle.getAsLong64()      // Returns int64_t
```

**Backward Compatibility Preserved:**
- R2000 code continues to call existing methods
- R2004 code uses new methods
- Same underlying storage (std::vector<unsigned char>)

---

## 5. Technology Choices

### 5.1 Language: C++11

**Rationale:**
- Matches GDAL/libopencad existing standard
- Modern enough for move semantics, smart pointers
- Not so modern as to break older compilers (C++14/17 features not needed)

**Specific Features Used:**
- `std::unique_ptr` for decompressor ownership (no manual delete)
- `std::vector` for dynamic arrays
- `override` keyword for virtual functions (clarity)
- `= delete` for non-copyable classes

**Avoided:**
- `std::shared_ptr` (not needed; ownership is clear)
- Lambda functions (not needed; simple enough)
- Template metaprogramming (KISS principle)

### 5.2 No External Dependencies

**Rationale:**
- GDAL already includes zlib, but DWG LZ77 ≠ standard DEFLATE
- Custom algorithm required per ODA spec
- Adding dependencies complicates build, increases maintenance

**Considered Alternatives:**
- *Use zlib's inflate:* **Rejected** - Wrong algorithm (DEFLATE has Huffman coding)
- *Use LZ4/LZMA:* **Rejected** - Wrong algorithm (different opcode structure)
- *Port ACadSharp C# to C++:* **Rejected** - Manual port is cleaner (no C#→C++ runtime issues)

### 5.3 Error Handling: Return Codes + CPLError

**Rationale:**
- Matches existing libopencad pattern
- GDAL uses CPLError for user-facing errors
- No C++ exceptions in GDAL (portability, performance)

**Pattern:**
```cpp
CADErrorCodes DWGFileR2004::readSection(uint8_t sectionId) {
    // Local errors via return code
    if (!m_decompressor->decompress(...)) {
        // Report to user via CPLError
        CPLError(CE_Failure, CPLE_AppDefined,
                "Failed to decompress section %d: %s",
                sectionId, m_decompressor->getLastError());
        return CADErrorCodes::FILE_PARSE_FAILED;
    }
    return CADErrorCodes::SUCCESS;
}
```

### 5.4 Memory Management: RAII + Pre-allocation

**Rationale:**
- RAII for automatic cleanup (std::unique_ptr, std::vector)
- Pre-allocated buffers for decompression (size known from section header)
- VSIMalloc/VSIFree only where GDAL API requires (rare)

**Pattern:**
```cpp
// Decompressed size is in section header
uint32_t decompSize = readUInt32(sectionHeader);
std::vector<char> decompressed(decompSize);  // RAII

if (!m_decompressor->decompress(compressed.data(), compSize,
                                decompressed.data(), decompSize,
                                &actualSize)) {
    // vector automatically cleaned up on return
    return CADErrorCodes::FILE_PARSE_FAILED;
}
// Use decompressed...
// Automatic cleanup when leaving scope
```

---

## 6. Alternatives Considered

### 6.1 Complete Rewrite vs. Extend R2000

**Alternative:** Create standalone R2004 reader, no inheritance

**Pros:**
- Clean slate, no coupling to R2000
- Easier to optimize R2004-specific paths

**Cons:**
- Massive code duplication (entity parsing logic is 80% identical)
- Entity classes (CADLine, CADCircle, etc.) are shared—duplication makes no sense
- Maintenance nightmare (bug fixes need two places)

**Decision:** **Extend R2000**
- Inheritance is the right tool here (true "is-a" relationship)
- Override only section-reading methods
- Reuse entity parsing (minor format tweaks handled via version checks)

### 6.2 Compression Library Integration

**Alternative:** Modify zlib to handle DWG LZ77

**Pros:**
- Reuse battle-tested compression code
- GDAL already has zlib embedded

**Cons:**
- DWG LZ77 opcodes are completely different from DEFLATE
- Would need to fork zlib (maintenance burden)
- Obfuscates intent (future developers confused)

**Decision:** **Custom DWGLZ77Decompressor**
- Clean separation of concerns
- Algorithm matches ODA spec exactly (reviewable)
- No risk of breaking GDAL's existing zlib usage

### 6.3 Single Monolithic Class vs. Decomposed Components

**Alternative:** Put decompression logic inside DWGFileR2004

**Pros:**
- One fewer class
- Slightly less indirection

**Cons:**
- Can't unit test decompression in isolation
- DWGFileR2004 becomes 2000+ lines (too large)
- Violates Single Responsibility Principle

**Decision:** **Separate DWGLZ77Decompressor**
- Testability is paramount (decompression is complex)
- ~500 lines in separate file is manageable
- Future: R2007 has different LZ77 variant → separate class, not if/else soup

### 6.4 Virtual Methods vs. Static Dispatch

**Alternative:** Make all section methods virtual in base

**Pros:**
- Pure polymorphism, textbook OOP

**Cons:**
- Virtual call overhead (minor but measurable)
- Base class needs to know about all possible overrides

**Decision:** **Virtual only where needed**
- Override `ParseFile()`, `readFileHeader()`, section readers
- Keep entity parsing non-virtual (version checks where needed)
- Pragmatic balance: polymorphism where it helps, performance where it matters

---

## 7. Operational Considerations

### 7.1 Build Integration

**CMake:**
```cmake
# ogr/ogrsf_frmts/cad/libopencad/CMakeLists.txt

set(LIB_HHEADERS
    ...existing...
    dwg/r2004.h
    dwg/lz77.h
)

set(LIB_CSOURCES
    ...existing...
    dwg/r2004.cpp
    dwg/lz77.cpp
)
```

**Build Time:**
- R2004 adds ~2000 lines of code
- Negligible build time impact (<5 seconds on modern machine)

### 7.2 Testing Strategy

**Unit Tests (C++):**
```cpp
// Test decompressor in isolation
TEST(DWGLZ77, DecompressSimpleLiteral) {
    char input[] = { /* compressed data */ };
    char output[100];
    size_t actual;

    DWGLZ77Decompressor dec;
    EXPECT_TRUE(dec.decompress(input, sizeof(input),
                               output, sizeof(output), &actual));
    EXPECT_EQ(actual, expectedSize);
    EXPECT_EQ(memcmp(output, expected, actual), 0);
}
```

**Integration Tests (Python - GDAL autotest):**
```python
def test_ogr_cad_r2004_open():
    ds = ogr.Open('data/cad/simple_r2004.dwg')
    assert ds is not None
    assert ds.GetLayerCount() > 0
```

**Test File Requirements:**
- Minimum 20 R2004 DWG files covering:
  - Simple geometry (lines, circles)
  - Complex polylines, splines
  - Multiple layers, blocks
  - Edge cases (empty drawings, large files)

### 7.3 Performance Monitoring

**Benchmark Points:**
1. Decompression speed (MB/s)
2. Total file read time vs. R2000
3. Memory usage (peak RSS)

**Acceptance Criteria:**
- Decompression: >10 MB/s on modern CPU
- Total read time: <2x R2000 for equivalent uncompressed file
- Memory: <2x file size peak usage

### 7.4 Debugging Support

**Logging:**
```cpp
#ifdef DEBUG_R2004
    CPLDebug("DWG", "Decompressed section %d: %zu bytes",
             sectionId, actualSize);
#endif
```

**Error Messages:**
- Always include context (section ID, file offset)
- Include hex dump of failing data (first 32 bytes)
- Suggest user action ("File may be corrupt, try with AutoCAD")

---

## 8. Risks and Mitigations

### Risk 1: Decompression Algorithm Bugs
**Probability:** HIGH
**Impact:** CRITICAL (data corruption)

**Mitigation:**
1. Extensive unit tests (100+ test vectors)
2. Cross-validate with ACadSharp on same files
3. Fuzz testing (random compressed data)
4. Checksum validation before/after decompression
5. **Defense:** Return error rather than corrupt data

### Risk 2: Performance Below Acceptable
**Probability:** MEDIUM
**Impact:** MEDIUM (user complaints)

**Mitigation:**
1. Profile early (Week 10 of implementation)
2. Optimize hot paths only after measurement
3. Consider lookup tables for opcode decoding
4. **Fallback:** Document "R2004 is slower due to decompression" if necessary

### Risk 3: ODA Spec Ambiguities
**Probability:** MEDIUM
**Impact:** HIGH (incorrect implementation)

**Mitigation:**
1. Use ACadSharp as second source of truth
2. Test with real-world files from various AutoCAD versions (2004-2006)
3. Engage with ODA community if available
4. **Contingency:** Document known limitations

### Risk 4: R2000 Code Breaks
**Probability:** LOW
**Impact:** HIGH (regression)

**Mitigation:**
1. Run full R2000 test suite after every change
2. Don't modify R2000 code unless absolutely necessary
3. Virtual method overrides should be minimal
4. **Safety:** Separate R2004 code into separate files (blast radius)

### Risk 5: 64-bit Handle Bugs
**Probability:** MEDIUM
**Impact:** MEDIUM (entity references broken)

**Mitigation:**
1. Dedicated test suite for large handles (>4GB values)
2. Test with files specifically created to have high handle values
3. Validate handle resolution logic separately
4. **Detection:** Add assert for handle overflow in debug builds

---

## 9. Evolution Path

### Phase 2: R2007 Support (After R2004 Complete)

**Changes Needed:**
1. New `DWGLZ77AC21Decompressor` class (enhanced LZ77 variant)
2. New `DWGFileR2007 : public DWGFileR2004`
3. Reed-Solomon error correction module
4. Metadata section parser (0x80 bytes at file start)

**Architecture Extension:**
```
DWGFileR2000 (base)
    └─> DWGFileR2004 (adds compression)
            └─> DWGFileR2007 (adds enhanced compression + metadata)
```

**Complexity Increase:**
- Moderate: R2007 builds on R2004 patterns
- Main addition: Reed-Solomon algorithm (well-documented)

### Phase 3-5: R2010, R2013, R2018

**R2010:** Minimal changes from R2007 (mostly ACIS version bumps)
**R2013:** Reverts to R2004 structure (**easier!**)
**R2018:** New entity types, security features

**Long-term Architecture:**
- May need factory pattern for decompressor selection
- Consider visitor pattern for entity type dispatch (if new entities proliferate)
- **Constraint:** Don't add patterns until pain is felt (YAGNI)

### Alternative Future: Plugin Architecture

If many formats accumulate:
```cpp
class IDWGVersionHandler {
    virtual CADErrorCodes readSection(...) = 0;
};

// Register handlers
DWGVersionRegistry::register(DWG_R2004, new R2004Handler());
```

**When to do this:** When we have 5+ versions, not before
**Why not now:** Premature abstraction, KISS principle

---

## 10. Implementation Checklist

### Week 1-2: Foundation
- [ ] Study ODA Specification R2004 section (40+ pages)
- [ ] Study ACadSharp R2004 implementation (DwgLZ77AC18Decompressor.cs)
- [ ] Document algorithm in pseudocode
- [ ] Collect 20+ R2004 test files

### Week 3: Class Structure
- [ ] Create `dwg/r2004.h` with class declaration
- [ ] Create `dwg/lz77.h` with decompressor declaration
- [ ] Extend `cadheader.h` for 64-bit handles
- [ ] Update `opencad_api.h` enum for DWG_R2004
- [ ] Update `opencad.cpp` version dispatch

### Week 4: Build Integration
- [ ] Add to CMakeLists.txt
- [ ] Verify builds on Linux, Windows, macOS
- [ ] Set up test framework structure
- [ ] Create stub implementations (compile only, no logic)

### Validation
- [ ] Architecture reviewed by 2+ senior developers
- [ ] No objections raised by GDAL maintainers
- [ ] Test file acquisition plan solid
- [ ] Timeline realistic (6 months for R2004)

---

## 11. Success Criteria

**Phase 1 Success (Month 1):**
- [ ] Architecture document approved
- [ ] Stub classes compile and link
- [ ] Test framework structure in place
- [ ] 20+ R2004 test files acquired

**R2004 Complete Success (Month 6):**
- [ ] Opens 95%+ of R2004 test files
- [ ] Geometry accuracy ≥95% vs. AutoCAD
- [ ] Performance within 2x of R2000
- [ ] Zero memory leaks (Valgrind clean)
- [ ] 200+ unit tests passing
- [ ] GDAL autotest suite passing
- [ ] Code reviewed and approved for merge

---

## 12. Open Questions

1. **Decompressed size location:** Is it always at offset 0 in compressed data? Or in section locator?
   - *Action:* Verify in ODA spec and ACadSharp code

2. **CRC magic number:** Spec says XOR with magic number—is it 0xA598 for all sections?
   - *Action:* Cross-check ACadSharp implementation

3. **64-bit handle encoding:** Are there multiple encoding modes? How to detect?
   - *Action:* Study ODA spec "Handle Reference" section

4. **Section ID mapping:** What are the actual IDs for Header (0x00?), Classes, Objects, Entities?
   - *Action:* Extract from ODA spec or ACadSharp constants

---

## 13. References

1. **Open Design Specification for .dwg files (v5.4.1)**
   - Section: "R2004 DWG File Format Organization"
   - URL: https://www.opendesign.com/files/guestdownloads/OpenDesign_Specification_for_.dwg_files.pdf

2. **ACadSharp R2004 Implementation**
   - File: `src/ACadSharp/IO/DWG/Decompressors/DwgLZ77AC18Decompressor.cs`
   - GitHub: https://github.com/DomCR/ACadSharp

3. **Existing libopencad R2000**
   - Files: `ogr/ogrsf_frmts/cad/libopencad/dwg/r2000.h/cpp`

4. **GDAL Coding Standards**
   - https://gdal.org/development/dev_practices.html

---

**Document Approval:**

- [ ] Architect: _____________________ Date: _______
- [ ] Lead Developer: _____________________ Date: _______
- [ ] GDAL Maintainer: _____________________ Date: _______

**END OF ARCHITECTURAL DESIGN DOCUMENT**
