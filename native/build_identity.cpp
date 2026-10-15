// Kept as an exported, readable marker so packaging can reject a stale DLL
// without loading native code into a tool process or computing a file hash.
#ifndef MHE_PACKAGE_VERSION
#error MHE_PACKAGE_VERSION must come from module.json at configure time
#endif

#ifndef MHE_BUILD_MODULE_ID
#define MHE_BUILD_MODULE_ID "endfield.longsword.showcase"
#endif

extern "C" __declspec(dllexport) const char* MHE_GetBuildIdentityV1() {
    return "MHE_BUILD_V1|" MHE_BUILD_MODULE_ID "|" MHE_PACKAGE_VERSION "|abi=1|";
}
