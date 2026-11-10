# Native-only host tests

This directory is outside `libil2cpp` so the Unity/IL2CPP vendor source installation and automatic native compilation cannot collect standalone `main()` or global `operator new/delete` test drivers. `AssemblyShadowFirstFailureV2Host.cpp` is compiled explicitly by `.github/workflows/r03-native-b1-first-origin.yml` with C++11/C++17. Do not move it into `libil2cpp`, glob it into production build sources, or link its allocation overrides into a Player. The VM implementation remains in `libil2cpp/vm/AssemblyShadowFirstFailureV2.h` and `AssemblyShadow.cpp`.

Source-only CI is not a substitute for checking the real Unity Bee compile DAG and defined symbols in the linked `GameAssembly` during Local Validation.
