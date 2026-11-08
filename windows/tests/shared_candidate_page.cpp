#include <algorithm>
#include <cstdio>
#include <string>

#define _countof(array) (sizeof(array) / sizeof((array)[0]))

struct SharedMemoryDouble
{
    wchar_t candidate_string[8] = {};
    wchar_t adjacent_field[8] = {};
};
SharedMemoryDouble *sharedData = nullptr;

#include "candidate_page_source.h"

int main()
{
    std::wstring page = L"unchanged";
    if (TryReadCandidatePageFromSharedMemory(&page) || page != L"unchanged")
        return 1;
    SharedMemoryDouble mapping;
    sharedData = &mapping;
    if (TryReadCandidatePageFromSharedMemory(nullptr))
        return 2;
    if (TryReadCandidatePageFromSharedMemory(&page) || !page.empty())
        return 3;
    std::copy_n(L"你,好", 4, mapping.candidate_string);
    if (!TryReadCandidatePageFromSharedMemory(&page) || page != L"你,好")
        return 4;

    // Even a last-slot terminator is valid; never truncate a well-formed page.
    std::fill_n(mapping.candidate_string, 7, L'你');
    mapping.candidate_string[7] = L'\0';
    if (!TryReadCandidatePageFromSharedMemory(&page) || page != std::wstring(7, L'你'))
        return 5;

    // No terminator inside the ABI field: reject it without interpreting the
    // adjacent field (or scanning outside the mapping) as candidate text.
    std::fill_n(mapping.candidate_string, 8, L'好');
    const auto previous = page;
    if (TryReadCandidatePageFromSharedMemory(&page) || page != previous)
    {
        std::fprintf(stderr, "unterminated shared-memory candidate page accepted\n");
        return 6;
    }
    return 0;
}
