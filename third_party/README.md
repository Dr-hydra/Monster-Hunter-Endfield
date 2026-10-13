# Build-time dependency provenance

The SDK headers in `include/BetterEndfield` are copied without edits from
`G:/Endfield Mod Loader/include/BetterEndfield` on 2026-10-13. They provide
the existing format 1 / ABI 1 interface; they are not a replacement host.
The source project's AGPL-3.0 license is retained in
`licenses/EML-AGPL-3.0.txt`.

`include/nlohmann/json.hpp` is the existing single-header nlohmann JSON
3.12.0 from `G:/Endfield Mod Loader/native/shared/third_party/nlohmann`.
Its copyright and SPDX comments are retained unchanged. The full MIT
license is included as `licenses/nlohmann-json-MIT.txt`, downloaded from
the author's [v3.12.0 release](https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT).

No game assets, credentials, host binaries or backend implementation are
included in these dependencies. No artifact hashes were computed.
