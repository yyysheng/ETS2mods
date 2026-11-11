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
    for (const auto &profile : build_profiles)
    {
        for (const auto &hook : profile.enabled_hooks)
            std::copy(hook.signature.begin(), hook.signature.end(),
                      image.begin() + hook.rva);
        for (const auto &helper : profile.engine_helpers)
            if (helper.signature_size != 0)
                std::copy_n(helper.signature.begin(), helper.signature_size,
                            image.begin() + helper.rva);
    }

    const auto reader = [&](std::uintptr_t rva, const std::uint8_t *bytes,
                            std::size_t size) {
        return rva <= image.size() && size <= image.size() - rva &&
               std::memcmp(image.data() + rva, bytes, size) == 0;
    };

    bool exact = false;
    std::size_t failed = 99;
    const auto *public_build = select_build_profile(
        build_profiles[3].executable_sha256, reader, &exact, &failed);
    assert(public_build == &build_profiles[3]);
    assert(exact);

    const auto *current_public_build = select_build_profile(
        build_profiles[0].executable_sha256, reader, &exact, &failed);
    assert(current_public_build == &build_profiles[0] && exact);
    assert(engine_helper_signatures_match(*current_public_build, reader));

    const auto *previous_public_build = select_build_profile(
        build_profiles[1].executable_sha256, reader, &exact, &failed);
    assert(previous_public_build == &build_profiles[1] && exact);

    const auto *earlier_public_build = select_build_profile(
        build_profiles[2].executable_sha256, reader, &exact, &failed);
    assert(earlier_public_build == &build_profiles[2] && exact);

    const auto find_profile = [](std::string_view game_version) {
        const auto found = std::find_if(
            build_profiles.begin(), build_profiles.end(),
            [&](const BuildProfile &profile) {
                return profile.game_version == game_version;
            });
        return found == build_profiles.end() ? nullptr : &*found;
    };
    const auto *previous_162_public_build = find_profile("1.62.1.0");
    assert(previous_162_public_build != nullptr);
    const auto *selected_previous_162_build = select_build_profile(
        previous_162_public_build->executable_sha256, reader, &exact, &failed);
    assert(selected_previous_162_build == previous_162_public_build && exact);
    assert(engine_helper_signatures_match(*selected_previous_162_build, reader));

    const auto *current_162_public_build = find_profile("1.62.2.1");
    assert(current_162_public_build != nullptr);
    const auto *selected_current_162_build = select_build_profile(
        current_162_public_build->executable_sha256, reader, &exact, &failed);
    assert(selected_current_162_build == current_162_public_build && exact);
    assert(engine_helper_signatures_match(*selected_current_162_build, reader));

    // A second binary digest is supported by the separately declared
    // signature-compatible profile when all enabled hooks remain verified.
    const auto *compatible_build = select_build_profile(
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        reader, &exact, &failed);
    assert(compatible_build == &build_profiles[4]);
    assert(!exact);

    // Bytes at an old, inactive lifecycle helper are intentionally irrelevant.
    image[0x015288d0] ^= 0xff;
    compatible_build = select_build_profile("SECOND-BUILD", reader, &exact, &failed);
    assert(compatible_build == &build_profiles[4]);

    // Any mismatch in an actually enabled hook causes safe rejection.
    const auto &required = verified_160_1_7_hooks[2];
    image[required.rva] ^= 0xff;
    assert(select_build_profile("UNSUPPORTED", reader, &exact, &failed) == nullptr);
    assert(failed == 2);

    image[required.rva] ^= 0xff;
    const auto &required_helper = build_profiles[0].engine_helpers[2];
    image[required_helper.rva] ^= 0xff;
    assert(!engine_helper_signatures_match(build_profiles[0], reader, &failed));
    assert(failed == 2);

    std::cout << "build profile tests passed: 1.60, 1.61.1.0, 1.61.1.1, 1.61.1.9, "
                 "1.62.1.0, 1.62.2.1 exact profiles + compatible profile; "
                 "inactive hook ignored; required hook/helper mismatches rejected\n";

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
        const auto *expected_profile = find_profile("1.62.2.1");
        assert(expected_profile != nullptr);
        const auto *exact_file = select_build_profile(
            argv[2], file_reader, &exact, &failed);
        assert(exact_file == expected_profile && exact);
        assert(engine_helper_signatures_match(*exact_file, file_reader));
        // The 1.62.2.1 vehicle dispatch must retain the trailer member and
        // virtual render slots used to draw both vehicle types.
        const std::array<std::uint8_t, 7> trailer_member{
            0x48, 0x8b, 0x8e, 0xc8, 0x00, 0x00, 0x00};
        const std::array<std::uint8_t, 6> virtual_render{
            0xff, 0x90, 0x00, 0x03, 0x00, 0x00};
        assert(file_signature_matches(executable, 0x00784201,
                                      trailer_member.data(), trailer_member.size()));
        assert(file_signature_matches(executable, 0x0078421a,
                                      virtual_render.data(), virtual_render.size()));
        assert(file_signature_matches(executable, 0x007842ab,
                                      virtual_render.data(), virtual_render.size()));
        std::cout << "installed executable verified: " << argv[1]
                  << "; exact=" << exact_file->id
                  << "; enabled-hooks=" << exact_file->enabled_hooks.size()
                  << '\n';
    }
    return 0;
}
