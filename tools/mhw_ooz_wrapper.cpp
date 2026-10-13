// Local GPL-3.0-or-later research wrapper around powzix/ooz.
// This is not linked into the EML module. Input must be owned, trusted game data.
#include <cstddef>
typedef unsigned char byte;
int Kraken_Decompress(const byte*, size_t, byte*, size_t);
extern "C" __declspec(dllexport) int MHE_Decompress(const byte* source,
    size_t sourceSize, byte* destination, size_t destinationSize) {
    return Kraken_Decompress(source, sourceSize, destination, destinationSize);
}
