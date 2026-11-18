#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

// Exported by the pinned Dobby POSIX backend. This test links the real patched
// Dobby target rather than a local reimplementation of its page writer.
int DobbyCodePatch(void *address, std::uint8_t *buffer, std::uint32_t buffer_size);

int main() {
    const long pageSizeLong = sysconf(_SC_PAGESIZE);
    assert(pageSizeLong > 0);
    const auto pageSize = static_cast<std::size_t>(pageSizeLong);

    std::array<std::uint8_t, 8> replacement{
        0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x17, 0x28
    };

    assert(DobbyCodePatch(nullptr, replacement.data(), 4) == -1);
    assert(DobbyCodePatch(reinterpret_cast<void *>(pageSize), nullptr, 4) == -1);
    assert(DobbyCodePatch(reinterpret_cast<void *>(pageSize), replacement.data(), 0) == -1);

    // First-page mprotect failure: an unmapped low address must be rejected
    // without reaching memcpy.
    assert(DobbyCodePatch(reinterpret_cast<void *>(1), replacement.data(), 4) == -1);

    // Successful patch on one executable page.
    auto *successPage = static_cast<std::uint8_t *>(
        mmap(nullptr, pageSize, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(successPage != MAP_FAILED);
    std::memset(successPage, 0x5A, pageSize);
    assert(mprotect(successPage, pageSize, PROT_READ | PROT_EXEC) == 0);
    assert(DobbyCodePatch(successPage + 64, replacement.data(),
                          static_cast<std::uint32_t>(replacement.size())) == 0);
    assert(std::memcmp(successPage + 64, replacement.data(), replacement.size()) == 0);
    assert(munmap(successPage, pageSize) == 0);

    // Second-page mprotect failure: leave the second page unmapped, then ask
    // the writer to patch across the boundary. The first page must remain
    // byte-for-byte unchanged because memcpy must not run on partial access.
    auto *crossing = static_cast<std::uint8_t *>(
        mmap(nullptr, pageSize * 2, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(crossing != MAP_FAILED);
    std::memset(crossing, 0x3C, pageSize);
    assert(munmap(crossing + pageSize, pageSize) == 0);
    assert(mprotect(crossing, pageSize, PROT_READ | PROT_EXEC) == 0);

    const auto *boundary = crossing + pageSize - 4;
    const std::array<std::uint8_t, 4> before{0x3C, 0x3C, 0x3C, 0x3C};
    assert(std::memcmp(boundary, before.data(), before.size()) == 0);
    assert(DobbyCodePatch(const_cast<std::uint8_t *>(boundary), replacement.data(),
                          static_cast<std::uint32_t>(replacement.size())) == -1);
    assert(std::memcmp(boundary, before.data(), before.size()) == 0);
    assert(munmap(crossing, pageSize) == 0);

    std::puts("patched Dobby POSIX code-writer tests passed");
    return 0;
}
