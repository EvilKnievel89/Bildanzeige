#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <string>

template <class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// Systemmeldung zu einem HRESULT, fuer Fehlertexte in der UI.
std::wstring FormatHResult(HRESULT hr);
