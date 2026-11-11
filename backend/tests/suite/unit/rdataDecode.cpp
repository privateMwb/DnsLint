// Unit RData Decode Test Suite
// Verifies decoding of the raw bytes inside DNS answers: TXT strings, AAAA
// addresses, and domain names (including compression pointers). These bytes
// come straight off the network, so malformed input must never read out of
// bounds or loop.
//
// Covers:
// - decodeTxt(): one string, several strings joined, an overrunning length byte, empty
// - decodeAaaa(): a real address, and anything not exactly 16 bytes
// - decodeName(): plain labels, a compression pointer, a pointer loop, bad offsets

#include <support/framework.h>

#include "checks/RDataDecode.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace {

using DnsCheckup::decodeAaaa;
using DnsCheckup::decodeName;
using DnsCheckup::decodeTxt;

VectorPro::Vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    VectorPro::Vector<std::byte> out;
    for (unsigned v : values)
        out.push_back(static_cast<std::byte>(v));
    return out;
}

VectorPro::Vector<std::byte> bytesOf(const std::string& text) {
    VectorPro::Vector<std::byte> out;
    for (char c : text)
        out.push_back(static_cast<std::byte>(c));
    return out;
}

} // namespace

// Verifies a single character-string decodes to its text.
static void txt_single_string() {
    CHK(decodeTxt(bytes({5, 'h', 'e', 'l', 'l', 'o'})) == "hello");
}

// Verifies several character-strings are joined back to back.
static void txt_multiple_strings_are_joined() {
    CHK(decodeTxt(bytes({2, 'a', 'b', 3, 'c', 'd', 'e'})) == "abcde");
}

// Verifies a length byte that overruns the data stops decoding without reading past the end.
static void txt_overrun_stops_cleanly() {
    CHK(decodeTxt(bytes({2, 'a', 'b', 9, 'x'})) == "ab");
    CHK(decodeTxt(bytes({200})).empty());
}

// Verifies empty data decodes to empty text.
static void txt_empty() {
    CHK(decodeTxt(VectorPro::Vector<std::byte>{}).empty());
    CHK(decodeTxt(bytes({0})).empty());
}

// Verifies a 16-byte address is printed as eight hex groups.
static void aaaa_valid_address() {
    CHK(decodeAaaa(bytes({0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01})) ==
        "2001:db8:0:0:0:0:0:1");
}

// Verifies any length other than 16 is refused.
static void aaaa_wrong_length_is_empty() {
    CHK(decodeAaaa(VectorPro::Vector<std::byte>{}).empty());
    CHK(decodeAaaa(bytes({1, 2, 3, 4})).empty());
    CHK(decodeAaaa(bytes({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})).empty());
}

// Verifies plain labels decode to a dotted name.
static void name_plain_labels() {
    const auto buffer = bytes({3, 'w', 'w', 'w', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o',
                               'm', 0});
    CHK(decodeName(buffer, 0) == "www.example.com");
}

// Verifies a compression pointer is followed back into the buffer.
static void name_compression_pointer() {
    // offset 0: example.com ; offset 13: "mail" then a pointer to offset 0.
    const auto buffer = bytes({7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 4, 'm',
                               'a', 'i', 'l', 0xC0, 0x00});
    CHK(decodeName(buffer, 13) == "mail.example.com");
}

// Verifies a pointer that points at itself ends in an empty result instead of looping.
static void name_pointer_loop_is_rejected() {
    CHK(decodeName(bytes({0xC0, 0x00}), 0).empty());
    CHK(decodeName(bytes({0xC0, 0x02, 0xC0, 0x00}), 0).empty());
}

// Verifies out-of-range offsets and empty buffers decode to nothing.
static void name_bad_offsets() {
    CHK(decodeName(VectorPro::Vector<std::byte>{}, 0).empty());
    CHK(decodeName(bytes({3, 'a', 'b', 'c', 0}), 99).empty());
    CHK(decodeName(bytes({9, 'a', 'b'}), 0).empty()); // label length runs off the end
}

static void run_tests() {
    RUN(txt_single_string);
    RUN(txt_multiple_strings_are_joined);
    RUN(txt_overrun_stops_cleanly);
    RUN(txt_empty);
    RUN(aaaa_valid_address);
    RUN(aaaa_wrong_length_is_empty);
    RUN(name_plain_labels);
    RUN(name_compression_pointer);
    RUN(name_pointer_loop_is_rejected);
    RUN(name_bad_offsets);
}

REGISTER_TEST_SUITE();
