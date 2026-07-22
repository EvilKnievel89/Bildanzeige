// Steht die Grundlage überhaupt bereit?
//
// Die erste Frage des ganzen Vorhabens: gibt es auf diesem Rechner Direct2D
// und WIC, ohne dass irgendetwas nachinstalliert wird? Beide Fabriken werden
// erzeugt und die Zahl der Decoder gezählt. Belegt die Zeile "keine externen
// Abhängigkeiten" in PLAN.md, Abschnitt 1.

#include <windows.h>
#include <wincodec.h>
#include <d2d1.h>
#include <cstdio>
int main(){ ID2D1Factory* f=nullptr; IWICImagingFactory* w=nullptr;
  CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  HRESULT a=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&f);
  HRESULT b=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&w));
  printf("D2D=0x%08lX WIC=0x%08lX\n",(unsigned long)a,(unsigned long)b);
  IWICBitmapDecoderInfo* di=nullptr; UINT n=0; IEnumUnknown* e=nullptr;
  if(SUCCEEDED(w->CreateComponentEnumerator(WICDecoder,WICComponentEnumerateDefault,&e))){
    IUnknown* u=nullptr; ULONG fetched=0;
    while(e->Next(1,&u,&fetched)==S_OK && fetched){ n++; u->Release(); }
  }
  printf("WIC-Decoder installiert: %u\n",n); return 0; }
