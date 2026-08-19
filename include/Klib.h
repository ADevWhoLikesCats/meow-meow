#ifndef KLIB_H
#define KLIB_H

#include <string>
#include <vector>
#include <cstdint>

struct KlibSymbol {
    std::string Name;
    uint32_t Offset;
    uint32_t Size;
};

void WriteKlib(const std::string& Filename,
               const std::vector<uint8_t>& Code,
               const std::vector<KlibSymbol>& Symbols);

void EmitKlibFile(const std::string& ObjectFile, const std::string& KlibFile);

#endif // KLIB_H