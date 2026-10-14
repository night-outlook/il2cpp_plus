# General diagnostic serializer: shared legacy and bounded sink

The actual AssemblyShadowDiagnostics::Serialize and SerializeTo use one schema/escaping body. Serialize preserves its legacy string API. SerializeTo accepts a caller-owned synchronous bounded sink, reports successful accepted bytes and fails closed on overflow, sink failure, re-entry, reuse, invalid inputs or unsupported non-classic C++ locale. A failed prefix is never a complete semantic result. Snapshot layouts and public StateName/UseKindName/Quote remain unchanged. Inputs must be immutable, disjoint, single-owner and fully captured before formatting; no VM/transaction lock may be held across an external sink.

This is formatting integration, NOT bounded live VM acquisition. Existing GetDiagnosticsJson still constructs a heap-backed snapshot; GetExecutionDiagnosticsJson remains unchanged. The new API is not wired to the Player, managed API, durable files or V2 semantic verifier. No engine scheduling/lifecycle behavior changes.

Run from an exact matching demo checkout:

```sh
python3 -B tools/r03/diagnostic_stream/run_tests.py --native /absolute/il2cpp_plus --demo /absolute/hybridclr_demo --output /new/absolute/external/result
```

The demo must contain the original c2e8 native codec/reference and N_SNAPSHOT_PUBLICATION_91b7 producer harness. The runner checks their Git blobs. Twenty-five cases run in strict C++17 and ASan/UBSan configurations. Tests compile the frozen original serializer, current legacy API and actual current SerializeTo; all fields, old string tokens/order, snapshot declarations and control-byte escaping must match. New code streams a synthetic 4096-class general diagnostic into the real bounded native Capture/Encoder and compares the wire byte-for-byte with the reference. Slot15 is a complete output from the unchanged original execution function with VM mocks, not a new streaming execution producer.

Test-only scalar/array/aligned operator-new accounting runs after all input/output buffers are prepared. Zero calls does not prove absence of C malloc, live metadata allocations, RSS, GC or filesystem overhead. ASan/UBSan enabled on all CI hosts; LeakSanitizer explicitly unavailable on Apple and enabled on Linux. Interrupted, overflowing or partial sink writes remain failure; original Failed tagged report stays Failed. Official full native SDK compilation and real Player behavior are separate gates.
