// ============================================================
// backend/fuzz/fuzz_rdata_decode.cpp
//
// Fuzzer for the decoders that turn raw DNS answer bytes into text:
// decodeTxt(), decodeAaaa() and decodeName(). These bytes come off the
// network from a third-party resolver, so malformed input (overrunning
// lengths, compression-pointer loops, wild offsets) must never read out
// of bounds or run forever. Run under ASan/UBSan, any such bug is a crash.
//
// Checked for every input:
//   - decodeAaaa() returns text only for exactly 16 bytes (and then
//     eight colon-separated groups), and nothing otherwise,
//   - decodeTxt() never returns more bytes than the input held,
//   - decodeName() terminates for every offset, including past the end.
// ============================================================

#include "checks/RDataDecode.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    VectorPro::Vector<std::byte> bytes;
    for (std::size_t i = 0; i < size; ++i)
        bytes.push_back(static_cast<std::byte>(data[i]));

    const std::string txt = DnsCheckup::decodeTxt(bytes);
    if (txt.size() > size)
        __builtin_trap();

    const std::string aaaa = DnsCheckup::decodeAaaa(bytes);
    if (size != 16 && !aaaa.empty())
        __builtin_trap();
    if (size == 16 && std::count(aaaa.begin(), aaaa.end(), ':') != 7)
        __builtin_trap();

    // Every offset, plus a few past the end, must terminate and stay in bounds.
    for (std::size_t offset = 0; offset < size + 4; ++offset)
        (void)DnsCheckup::decodeName(bytes, offset);

    return 0;
}
