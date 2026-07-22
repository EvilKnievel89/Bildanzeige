// Welche Formate kann dieser Rechner lesen?
//
// Zaehlt die registrierten WIC-Decoder mit Namen und Dateiendungen auf. Daraus
// stammt die Tabelle in PLAN.md, Abschnitt 1 -- und die Aussage, dass ein
// nachinstalliertes Format von selbst in der Ordnerliste erscheint: die
// Anwendung fragt zur Laufzeit dieselbe Liste ab.

#include <windows.h>
#include <wincodec.h>
#include <cstdio>
int main(){ CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  IWICImagingFactory* w=nullptr;
  CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&w));
  IEnumUnknown* e=nullptr; w->CreateComponentEnumerator(WICDecoder,WICComponentEnumerateDefault,&e);
  IUnknown* u=nullptr; ULONG f=0; wchar_t buf[256]; UINT got=0;
  while(e->Next(1,&u,&f)==S_OK && f){
    IWICBitmapDecoderInfo* di=nullptr;
    if(SUCCEEDED(u->QueryInterface(IID_PPV_ARGS(&di)))){
      di->GetFriendlyName(256,buf,&got); wprintf(L"%-34s",buf);
      if(SUCCEEDED(di->GetFileExtensions(256,buf,&got))) wprintf(L"%s",buf);
      wprintf(L"\n"); di->Release(); }
    u->Release(); }
  return 0; }
