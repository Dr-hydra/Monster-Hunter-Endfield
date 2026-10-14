# Contributor and agent guidance

- Work from this repository root. Keep machine-specific paths, credentials, local extensions, game resources and generated packages outside Git. Respect the user's workspace selection.
- Read README.md, docs/PROJECT_PLAN.md and docs/DEVELOPMENT.md before changing project structure. Historical validation records do not prove a modified checkout passes.
- The runtime is a Windows x64 C++ module for E Mod Loader format 1 / Native ABI 1. Keep the module ID `endfield.longsword.showcase` compatible with existing configurations.
- The public module displays actions and maintains simulated practice state. Do not add gameplay damage, reward, save writes or combat network traffic to the public build.
- Animation, attachment and control changes require source-resource or observed runtime evidence. Clearly identify approximations; offline previews do not constitute in-game validation.
- Bind character skeletons and display weapons separately. Resolve the main skin and full transform paths to avoid duplicate bone-name collisions.
- Decode sound assets offline and use the shared PCM playback path. Preserve third-party license notices; the source license does not grant game-asset redistribution rights.
- Game resources and derivatives stay local. Do not force-add ignored files or introduce placeholder assets to make missing-input validation appear successful.
- Run checks appropriate to the change. Distinguish tests that need locally prepared game data from tests usable with a clean clone. Do not launch the game without authorization.
- Do not compute artifact hashes unless explicitly requested. Normal Git object management is unaffected.
- Use Headroom MCP compression for long logs and multi-file results when available. If unavailable, save full logs in ignored output/ and return bounded summaries. Do not compress credentials or short content where exact wording matters.
