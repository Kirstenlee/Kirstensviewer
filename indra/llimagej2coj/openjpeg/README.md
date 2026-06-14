# Embedded OpenJPEG Integration - Ring-Fenced Development

## Overview

This directory contains an embedded, Second Life-optimized version of OpenJPEG 2.5.4. The integration is **ring-fenced** with a kill switch (`SL_EMBEDDED_OPENJPEG`) that allows safe parallel development without breaking the existing vcpkg-based build.

## Kill Switch Control

### Option 1: CMake Command Line (Recommended for testing)
```bash
# Enable embedded OpenJPEG
cmake -DSL_EMBEDDED_OPENJPEG=ON ..

# Disable (use vcpkg library - default)
cmake -DSL_EMBEDDED_OPENJPEG=OFF ..
```

### Option 2: CMakeLists.txt (For permanent switch)
Edit `llimagej2coj/CMakeLists.txt`:
```cmake
option(SL_EMBEDDED_OPENJPEG "Use embedded, optimized OpenJPEG code" OFF)  # Change to ON
```

### Option 3: Preprocessor Define (Advanced)
Add to compiler flags: `-DSL_EMBEDDED_OPENJPEG=1`

## Current Status

✅ **Phase 0: Infrastructure (COMPLETE)**
- [x] Directory structure created
- [x] Kill switch implemented in CMakeLists.txt
- [x] Configuration headers (opj_config.h, sl_config.h, sl_types.h)
- [x] Ring-fenced includes in llimagej2coj.cpp
- [x] Build system integration

🔄 **Phase 1: Core Integration (IN PROGRESS)**
- [ ] Copy core OpenJPEG files from vcpkg source
- [ ] Minimal j2k.c/h (J2K codestream parser)
- [ ] dwt.c/h (Discrete Wavelet Transform)
- [ ] tcd.c/h (Tile coder/decoder)
- [ ] t1.c/h, t2.c/h (Tier-1, Tier-2 coding)
- [ ] Supporting files (mqc, bio, mct, etc.)

⏳ **Phase 2: Dead Code Removal (TODO)**
- [ ] Remove JP2 file format code
- [ ] Remove file I/O functions
- [ ] Simplify codec selection

⏳ **Phase 3: DWT Optimizations (TODO - YOUR EXPERTISE!)**
- [ ] Port existing DWT modifications
- [ ] AVX2 SIMD wavelet transform
- [ ] Power-of-2 dimension fast paths

⏳ **Phase 4-6: Advanced Optimizations (TODO)**
- [ ] Custom memory allocator
- [ ] 64x64 tile fast path
- [ ] Profile-Guided Optimization

## Directory Structure

```
llimagej2coj/
├── llimagej2coj.cpp/h              [MODIFIED] - Ring-fenced includes
├── CMakeLists.txt                  [MODIFIED] - Kill switch integration
│
├── openjpeg/                       [NEW] - Embedded OpenJPEG
│   ├── opj_config.h                [CREATED] - Main configuration with kill switch
│   │
│   ├── core/                       [TODO] - Core J2K codec files
│   │   ├── j2k.c/h
│   │   ├── dwt.c/h                 <- Your DWT optimizations will go here!
│   │   ├── tcd.c/h
│   │   ├── t1.c/h
│   │   ├── t2.c/h
│   │   └── ... (more core files)
│   │
│   ├── utils/                      [TODO] - Utility files
│   │   ├── opj_malloc.c/h
│   │   ├── image.c/h
│   │   └── ... (more utils)
│   │
│   └── sl_optimized/               [CREATED] - SL-specific optimizations
│       ├── sl_config.h             [CREATED] - Feature flags
│       ├── sl_types.h              [CREATED] - SL constants and helpers
│       ├── sl_memory_pool.cpp/h    [TODO] - Custom allocator
│       ├── sl_dwt_avx2.cpp/h       [TODO] - SIMD wavelet transform
│       └── sl_tile_64x64.cpp/h     [TODO] - 64x64 tile fast path
```

## Source Files Location

OpenJPEG source files are available in the vcpkg package:
```
S:\Dev\S24\.vcpkg-repo\packages\openjpeg_x64-windows-dynamic-avx\
```

The original OpenJPEG source can also be found via vcpkg's GitHub mirror:
- https://github.com/uclouvain/openjpeg/tree/v2.5.4

## Testing the Kill Switch

### Test 1: Verify Default Behavior (Kill Switch OFF)
```bash
# Clean build
rm -rf build
cmake -B build -DSL_EMBEDDED_OPENJPEG=OFF
cmake --build build --target llimagej2coj

# Should see: "BUILDING WITH VCPKG OPENJPEG (LIBRARY)"
# Should link to openjp2.dll
```

### Test 2: Enable Embedded Version (Kill Switch ON)
```bash
# Enable embedded OpenJPEG
cmake -B build -DSL_EMBEDDED_OPENJPEG=ON
cmake --build build --target llimagej2coj

# Should see: "BUILDING WITH EMBEDDED OPENJPEG (SL-OPTIMIZED)"
# Should NOT link to openjp2.dll (once core files are added)
```

### Test 3: Runtime Verification
Check the engine info string in the viewer:
- **Kill Switch OFF**: "KV OJP: 2.5.4, Compile: ..."
- **Kill Switch ON**: "KV OJP: 2.5.4-SL-Embedded-Optimized (SL-Embedded), Compile: ..."

## Next Steps

1. **Copy Core Files** (Phase 1a)
   ```bash
   # From vcpkg source to llimagej2coj/openjpeg/core/
   # Start with minimal set: j2k.c/h, dwt.c/h, tcd.c/h
   ```

2. **Update CMakeLists.txt** (Phase 1b)
   ```cmake
   # Add copied files to llimagej2coj_SOURCE_FILES
   if(SL_EMBEDDED_OPENJPEG)
       list(APPEND llimagej2coj_SOURCE_FILES
           openjpeg/core/j2k.c
           openjpeg/core/dwt.c
           # ... more files
       )
   endif()
   ```

3. **Verify Build** (Phase 1c)
   ```bash
   cmake -B build -DSL_EMBEDDED_OPENJPEG=ON
   cmake --build build --target llimagej2coj
   # Should compile successfully (even if not fully functional yet)
   ```

4. **Incremental Testing** (Phase 1d)
   - Test decode with kill switch ON
   - Compare results with kill switch OFF
   - Verify identical output

## Advantages of This Approach

✅ **Zero Risk**: Existing system continues to work  
✅ **Parallel Development**: Can develop/test embedded version without affecting production  
✅ **Easy Rollback**: Just flip the switch if issues arise  
✅ **Incremental Progress**: Can add files one at a time  
✅ **Performance Comparison**: Easy A/B testing (switch ON vs OFF)  
✅ **Source Available**: vcpkg already downloaded OpenJPEG 2.5.4 source  

## Performance Tracking

As we add optimizations, track performance gains:

| Phase | Feature | Kill Switch | Decode Time (1024x1024) | Gain |
|-------|---------|-------------|-------------------------|------|
| Baseline | vcpkg DLL | OFF | 10.0ms | 0% |
| Phase 1 | Embedded (no opts) | ON | ~10.0ms | ~0% |
| Phase 2 | Dead code removed | ON | ~9.5ms | 5% |
| Phase 3 | DWT optimized | ON | ~7.5ms | 25% |
| Phase 4 | Memory pools | ON | ~6.5ms | 35% |
| Phase 5 | 64x64 fast path | ON | ~6.0ms | 40% |
| Phase 6 | PGO | ON | ~5.0ms | **50%** |

## Safety Notes

- **Always test with kill switch OFF first** to ensure no regressions
- **Keep kill switch OFF by default** until embedded version is proven stable
- **Use feature flags** in sl_config.h to enable optimizations incrementally
- **Profile before and after** each optimization phase

## Contributing

When adding new optimized code:

1. Add feature flag to `sl_optimized/sl_config.h`:
   ```c
   #define SL_FEATURE_MY_OPTIMIZATION 0  // Start disabled
   ```

2. Wrap code with feature check:
   ```c
   #if SL_FEATURE_MY_OPTIMIZATION
       // Optimized path
   #else
       // Original OpenJPEG code
   #endif
   ```

3. Test both paths before enabling by default

## Questions?

See documentation:
- `/docs/OPENJPEG_STATIC_LINKING_ANALYSIS.md` - Static vs Dynamic analysis
- `/docs/OPENJPEG_CODE_INTEGRATION_ANALYSIS.md` - Cherry-picking analysis
- `/docs/OPENJPEG_INTEGRATION_DETAILED_PLAN.md` - Full implementation plan

---

**Status**: Phase 0 Complete - Infrastructure Ready  
**Next**: Copy core OpenJPEG files and update CMakeLists.txt  
**Kill Switch**: Currently OFF (using vcpkg library)
