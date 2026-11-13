#include "build_profiles.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace reverse_assist::compatibility;

bool file_signature_matches(const std::vector<std::uint8_t> &file,
                            std::uintptr_t rva,
                            const std::uint8_t *signature,
                            std::size_t signature_size)
{
    if (file.size() < sizeof(IMAGE_DOS_HEADER)) return false;
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(file.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        static_cast<std::size_t>(dos->e_lfanew) >
            file.size() - sizeof(IMAGE_NT_HEADERS64))
        return false;
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(
        file.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return false;
    const auto *section = IMAGE_FIRST_SECTION(nt);
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections;
         ++index, ++section)
    {
        const auto start = static_cast<std::uintptr_t>(section->VirtualAddress);
        const auto span = static_cast<std::uintptr_t>(
            std::max(section->Misc.VirtualSize, section->SizeOfRawData));
        if (rva < start || rva - start > span ||
            signature_size > span - (rva - start))
            continue;
        const auto offset = static_cast<std::size_t>(section->PointerToRawData) +
                            static_cast<std::size_t>(rva - start);
        return offset <= file.size() && signature_size <= file.size() - offset &&
               std::memcmp(file.data() + offset, signature, signature_size) == 0;
    }
    return false;
}

int main(int argc, char **argv)
{
    constexpr std::size_t image_size = 0x01800000;
    std::vector<std::uint8_t> image(image_size, 0xcc);
    for (const auto &hook : current_build.enabled_hooks)
        std::copy(hook.signature.begin(), hook.signature.end(),
                  image.begin() + hook.rva);
    for (const auto &helper : current_build.engine_helpers)
        std::copy_n(helper.signature.begin(), helper.signature_size,
                    image.begin() + helper.rva);

    const auto reader = [&](std::uintptr_t rva, const std::uint8_t *bytes,
                            std::size_t size) {
        return rva <= image.size() && size <= image.size() - rva &&
               std::memcmp(image.data() + rva, bytes, size) == 0;
    };

    bool exact = false;
    std::size_t failed = 99;
    const auto digest = current_build.executable_sha256;
    assert(select_build_profile(digest, reader, &exact, &failed) == &current_build);
    assert(exact);
    assert(engine_helper_signatures_match(current_build, reader));
    assert(select_build_profile(
        "8F17D3505C2E6C25A93F38A1119FFBBCCFBA6A3BE2EDDA3771E0186276AD9434",
        reader, &exact, &failed) == nullptr);
    assert(!exact);

    for (std::size_t i = 0; i < current_build.enabled_hooks.size(); ++i)
    {
        const auto rva = current_build.enabled_hooks[i].rva;
        image[rva] ^= 0xff;
        assert(select_build_profile(digest, reader, &exact, &failed) == nullptr);
        assert(failed == i);
        image[rva] ^= 0xff;
    }
    for (std::size_t i = 0; i < current_build.engine_helpers.size(); ++i)
    {
        const auto rva = current_build.engine_helpers[i].rva;
        image[rva] ^= 0xff;
        assert(!engine_helper_signatures_match(current_build, reader, &failed));
        assert(failed == i);
        image[rva] ^= 0xff;
    }

    std::cout << "build profile tests passed: exact 1.62.3.2; previous digest rejected; "
                 "all hook and helper mismatches detected\n";

    if (argc == 3)
    {
        std::ifstream stream(argv[1], std::ios::binary);
        assert(stream);
        std::vector<std::uint8_t> executable(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
        const auto file_reader =
            [&](std::uintptr_t rva, const std::uint8_t *bytes,
                std::size_t size) {
                return file_signature_matches(executable, rva, bytes, size);
            };
        const auto *exact_file = select_build_profile(
            argv[2], file_reader, &exact, &failed);
        assert(exact_file == &current_build && exact);
        assert(engine_helper_signatures_match(*exact_file, file_reader));
        // The 1.62.3.2 vehicle dispatch must retain the trailer member and
        // virtual render slots used to draw both vehicle types.
        const std::array<std::uint8_t, 7> trailer_member{
            0x48, 0x8b, 0x8e, 0xc8, 0x00, 0x00, 0x00};
        const std::array<std::uint8_t, 6> virtual_render{
            0xff, 0x90, 0x00, 0x03, 0x00, 0x00};
        assert(file_signature_matches(executable, 0x00784221,
                                      trailer_member.data(), trailer_member.size()));
        assert(file_signature_matches(executable, 0x0078423a,
                                      virtual_render.data(), virtual_render.size()));
        assert(file_signature_matches(executable, 0x007842cb,
                                      virtual_render.data(), virtual_render.size()));
        std::cout << "installed executable verified: " << argv[1]
                  << "; exact=" << exact_file->id
                  << "; enabled-hooks=" << exact_file->enabled_hooks.size()
                  << '\n';
    }
    return 0;
}
