# Unity 2022.3 Assembly Shadow fork

This fork is the source of truth for changes to Unity's public libil2cpp
implementation. The interpreter remains in the separate hybridclr repository;
the installer composes the two pinned trees without modifying either checkout.

## M00 baseline

- Upstream: https://github.com/focus-creative-games/il2cpp_plus
- Upstream tag: v2022-8.14.0
- Exact upstream commit: 11251b938d2ce7fa865165130bf257ca239db69f
- Supported Editor for this experiment: Unity 2022.3.62f2
- Paired initial runtime: 210cbe0ecc878a6ab7e392b1ce191a46d509ba25
- Package initial revision: fd32708e7f5106f0ffa375bcfe43342ee594c1e5
- Authoritative updated pairing: demo ProjectSettings/AssemblyShadowSourcePins.json
- Development branches: codex/assembly-shadow-m00, then codex/assembly-shadow-m01

The pre-existing 2022-3.x branch is retained unchanged at
54db003dc8294524ed21c4b0ebec92b5e9c02b08. It is not compatible evidence for
the current package's v2022-8.14.0 selection.

The M00 change defines HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW as zero unless the
native compiler explicitly overrides it. There are no resolver hooks in M00.
M01 hooks must compile out when this feature is zero. Managed scripting defines
are not an authority for the native feature mode.

## Rebase policy

1. Preserve the current four-repository pins and milestone tags.
2. Fetch the intended upstream Unity 2022 tag and record its exact commit.
3. Rebase in a new branch, review every conflict and upstream runtime change.
4. Re-run the feature-OFF ordinary HybridCLR Player and feature-ON old-bundle
   Prefab/Scene test matrix before updating the demo's pins.
5. Obtain an independent review and create a new pairing/tag.

Do not merge an unreviewed upstream major version, change Unity versions, or
reuse generated native build caches as proof that a new pairing works.
