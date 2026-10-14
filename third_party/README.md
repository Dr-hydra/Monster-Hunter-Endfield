# Build-time dependencies

The headers in `include/BetterEndfield` come from the
[E Mod Loader SDK](https://github.com/Dr-hydra/Endfield-Mod-Loader).
They provide the format 1 / Native ABI 1 interface. The SDK is covered by
AGPL-3.0-only; its license is retained in `licenses/EML-AGPL-3.0.txt`.
The loader itself must be installed separately.

`include/nlohmann/json.hpp` is nlohmann/json 3.12.0. Its original copyright
and SPDX comments are retained. The MIT license is included in
`licenses/nlohmann-json-MIT.txt`; the upstream release is
[v3.12.0](https://github.com/nlohmann/json/tree/v3.12.0).

This directory contains build-time headers and license notices. It does
not contain game assets, credentials, loader binaries or server code.
