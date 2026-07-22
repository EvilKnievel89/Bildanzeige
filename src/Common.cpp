#include "Common.h"

#include <cstdio>

std::wstring FormatHResult(HRESULT hr)
{
    LPWSTR text = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);

    std::wstring result;
    if (length != 0 && text != nullptr)
    {
        result.assign(text, length);
        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n'))
            result.pop_back();
    }
    if (text != nullptr)
        LocalFree(text);

    wchar_t code[16];
    swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(hr));

    if (result.empty())
        return code;
    return result + L" (" + code + L")";
}
