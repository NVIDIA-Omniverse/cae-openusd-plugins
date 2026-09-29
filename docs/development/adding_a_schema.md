<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Add or Extend a Schema

Use this workflow when adding a schema class, changing schema properties, or
introducing a schema library.

## 1. Define the Contract

Decide whether the concept belongs in the format-independent `omniSci` library
or in a domain-specific library.

- Use an API schema when the properties add semantics to an existing dataset
  or child prim.
- Use a multiple-apply API when one prim can carry several independently named
  instances of the concept.
- Add a concrete typed schema only when the prim has a stable identity that
  consumers need to query with `IsA`.
- Prefer composition of domain APIs with `OmniSciDataset` over a deep typed
  inheritance hierarchy.

For a source format, update or create its
[Conceptual Data Mapping](../conceptual_data_mapping/README.md) before
implementation. Define the hierarchy, property meanings, array shapes,
indexing, time behavior, fidelity guarantees, and capability boundaries.

## 2. Author the Schema Source

Edit an existing `source/schemas/<library>/schema.usda`, or create one for a
new library. The file must define:

- `GLOBAL` metadata including `libraryName`, `libraryPath`, `libraryPrefix`,
  and `useLiteralIdentifier`;
- stable schema and property names;
- `className` and `apiSchemaType` metadata for generated APIs;
- `propertyNamespacePrefix` for multiple-apply APIs;
- property types, variability, defaults, allowed tokens, display metadata, and
  complete semantic documentation.

Use `uniform` for configuration or identity metadata that is not intended to
vary over time. Give token properties an explicit default and
`allowedTokens` when their vocabulary is closed; leave a token unrestricted
only when consumers are expected to accept future values.

Schema documentation describes the current public contract. Do not put commit
history, implementation notes, or ADR references in `schema.usda`.

## 3. Register a New Library

Existing libraries need no CMake registration change. For a new library, add
one entry to the central `source/schemas/CMakeLists.txt` registry:

```cmake
cae_add_schema(omniSciExample
    DIR "${CMAKE_CURRENT_SOURCE_DIR}/omni_sci_example"
)
```

Do not add a one-line `CMakeLists.txt` inside every schema directory. The
central registry is the inventory of shipped schema plugins.

`cae_add_schema()` runs `usdGenSchema`, builds the native plugin, installs its
headers and resources, and adds generated Python bindings when enabled.

## 4. Regenerate and Build

`usdGenSchema` runs during CMake configuration, not as an ordinary compilation
step. Reconfigure after every `schema.usda` change, then rebuild:

```sh
cmake -S . -B build
cmake --build build --parallel
```

The configure step removes stale generated output when the schema source
changes. If a reused build reports missing generated methods or undefined
symbols, confirm that configuration completed with the intended OpenUSD and
Python runtimes before debugging the schema implementation.

## 5. Add and Register Tests

Add focused tests under `tests/python/omni_sci_<domain>/`. Verify:

- plugin and generated type registration;
- `Apply`, `HasAPI`, `Define`, or `IsA` behavior as appropriate;
- generated property names and USD types;
- variability, defaults, and allowed tokens;
- independent instance names for multiple-apply APIs;
- USDA save-and-reopen behavior;
- imports from the staged `pxr` namespace.

Register the suite in `tests.cmake` so CTest supplies the staged install and
runtime environment:

```cmake
cae_add_pytest(test_omni_sci_example
    TESTS   tests/python/omni_sci_example
    PLUGINS omniSciExample omniSci
    LABELS  unit
)
```

Add dependent schema plugins to `PLUGINS` for readability at the call site.

## 6. Update Consumers and Documentation

- Add a new library to [the schema index](../schemas/README.md).
- Link it from the relevant Conceptual Data Mapping.
- Add it to a reader's `LIBRARIES` and `PLUGIN_DEPS` when that reader applies
  the schema.
- Update reader tests to verify the observable applied schemas and properties.
- Register any license required by bundled third-party schema data.

If the change affects file-format configuration, also update
[`omniSciFileFormatArgs`](../../source/schemas/omni_sci_file_format_args/schema.usda),
the reader's dynamic argument mapping, and
[File-format arguments](../file_formats/arguments.md).

## 7. Validate

Run the focused schema test and then the complete configured suite:

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build -R '^test_omni_sci_example$' --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

Use the generated `schema.usda` output and generated APIs to diagnose
code-generation problems; do not treat older documentation as authoritative.
