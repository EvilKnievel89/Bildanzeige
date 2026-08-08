// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

// Welche Formate kann dieser Rechner lesen -- und welche nur auf dem Papier?
//
// Zählt die registrierten WIC-Decoder mit Namen und Dateiendungen auf. Daraus
// stammt die Tabelle in PLAN.md, Abschnitt 1 -- und die Aussage, dass ein
// nachinstalliertes Format von selbst in der Ordnerliste erscheint: die
// Anwendung fragt zur Laufzeit dieselbe Liste ab.
//
// Jeder Decoder wird zusätzlich einmal probeweise erzeugt. Angemeldet heißt
// nämlich nicht vorhanden: WebP, HEIF, AVIF und JPEG XL meldet Windows selbst
// an, geliefert werden sie aber von Erweiterungen aus dem Microsoft Store.
// Fehlt eine, bleibt der Eintrag stehen und erst das Erzeugen scheitert -- mit
// WINCODEC_ERR_COMPONENTINITIALIZEFAILURE (0x88982F8B). Genau das ist der Fall,
// den man auf einem Windows Server ohne Store vor sich hat.
//
// Ein nicht erzeugbarer Decoder ist keine Beanstandung, sondern ein Befund
// über diesen Rechner; der Rückgabewert bleibt deshalb 0.

#include <windows.h>
#include <wincodec.h>
#include <cstdio>
int main(){ CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  IWICImagingFactory* w=nullptr;
  CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&w));
  IEnumUnknown* e=nullptr; w->CreateComponentEnumerator(WICDecoder,WICComponentEnumerateDefault,&e);
  IUnknown* u=nullptr; ULONG f=0; wchar_t buf[256]; UINT got=0; int alle=0, tot=0;
  while(e->Next(1,&u,&f)==S_OK && f){
    IWICBitmapDecoderInfo* di=nullptr;
    if(SUCCEEDED(u->QueryInterface(IID_PPV_ARGS(&di)))){
      ++alle;
      di->GetFriendlyName(256,buf,&got); wprintf(L"%-34s",buf);
      if(SUCCEEDED(di->GetFileExtensions(256,buf,&got))) wprintf(L"%s",buf);
      IWICBitmapDecoder* d=nullptr; HRESULT hr=di->CreateInstance(&d);
      if(d) d->Release();
      if(FAILED(hr)){ ++tot; wprintf(L"\n   NICHT ERZEUGBAR (0x%08lX) -- Bilderweiterung fehlt",
                                     (unsigned long)hr); }
      wprintf(L"\n"); di->Release(); }
    u->Release(); }
  wprintf(L"\n  %d Decoder angemeldet, davon %d nicht erzeugbar\n", alle, tot);
  return 0; }
