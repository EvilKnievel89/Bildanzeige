#include "Toolbar.h"

#include <cmath>
#include <initializer_list>

namespace
{
    // Entwurfsraster der Icons; alle Koordinaten unten beziehen sich darauf.
    constexpr float kIconBox = 24.0f;

    // Masse in DIP (das Render-Target laeuft auf 96 dpi, also 1 DIP == 1 px
    // bei 100 %). Die DPI-Skalierung wird explizit aufmultipliziert.
    constexpr float kBarHeight = 40.0f;
    constexpr float kButtonSize = 34.0f;
    constexpr float kIconSize = 20.0f;
    constexpr float kButtonGap = 2.0f;
    constexpr float kGroupGap = 13.0f;
    constexpr float kStrokeWidth = 2.0f;   // in Icon-Einheiten

    constexpr UINT32 kBarFill = 0x2B2B2B;
    constexpr UINT32 kBarBorder = 0x3C3C3C;
    constexpr UINT32 kSeparator = 0x4A4A4A;
    constexpr UINT32 kIconNormal = 0xD4D4D4;
    constexpr UINT32 kIconDisabled = 0x585858;
    constexpr UINT32 kHoverFill = 0x3A3A3A;
    constexpr UINT32 kPressedFill = 0x484848;

    constexpr float kPi = 3.14159265358979f;

    // Lupe: Kreis mit Griff nach unten rechts, darin ein Balken (und beim
    // Vergroessern ein zweiter quer dazu).
    constexpr float kLensCx = 10.5f;
    constexpr float kLensCy = 10.5f;
    constexpr float kLensR = 6.0f;
    constexpr float kLensBar = 3.2f;    // halbe Balkenlaenge

    // Vier Eckwinkel, nach aussen zeigend -- die gelaeufige Marke fuers
    // Einpassen. Die Striche ragen um eine halbe Strichbreite ueber die Ecke
    // hinaus, sonst bliebe dort mit stumpfen Enden eine Kerbe stehen.
    constexpr float kMarkInset = 4.0f;
    constexpr float kMarkArm = 5.0f;
    constexpr float kMarkOver = kStrokeWidth * 0.5f;

    // Bildwechsel: ein blosser Winkel, gestrichelt statt gefuellt. Die Leiste
    // hat damit drei unterscheidbare Pfeilformen -- Winkel fuer die Datei,
    // Dreieck mit Balken fuer die Seite, blosses Dreieck fuer die Wiedergabe.
    // Ein viertes gefuelltes Dreieck waere derselbe Fehler wie in M5.
    constexpr float kChevronStroke = 2.4f;

    // Schrittmarke: Dreieck mit Balken dahinter. Ohne den Balken saehe der Knopf
    // im Halt genauso aus wie der Wiedergabeknopf unmittelbar daneben, und man
    // wuesste nicht, welcher von beiden was tut. Das Dreieck rueckt dafuer um
    // eine halbe Balkenbreite nach links, damit das Ganze mittig bleibt.
    constexpr float kStepShift = 2.5f;
    constexpr float kStepBarLeft = 16.0f;
    constexpr float kStepBarWidth = 2.0f;
    constexpr float kStepBarTop = 4.5f;
    constexpr float kStepBarBottom = 19.5f;

    // Pausenmarke: zwei Balken, auf dieselbe Hoehe gesetzt wie das Dreieck der
    // Schrittknoepfe, damit die drei Knoepfe nebeneinander gleich schwer wirken.
    constexpr float kPauseTop = 5.0f;
    constexpr float kPauseBottom = 19.0f;
    constexpr float kPauseBar = 2.8f;
    constexpr float kPauseGap = 2.4f;

    // Vollbild: zwei Pfeile auf der Hauptdiagonalen. Sie bestehen wie die
    // Einpass-Marke aus Eckwinkeln -- unterschieden werden beide dadurch, dass
    // hier die Diagonale durch die Mitte laeuft, waehrend die Einpass-Marke
    // vier Ecken um eine leere Mitte setzt.
    //
    // Hinaus und zurueck brauchen verschiedene Abstaende. Beim Zurueckweg
    // sitzen die Spitzen innen, und mit denselben Massen wie fuer hinaus
    // stuenden sie einen halben Punkt auseinander -- aus zwei Pfeilen wuerde
    // ein Klecks. Sie ruecken deshalb auseinander, der Schaft nach aussen.
    constexpr float kArrowArm = 4.5f;
    constexpr float kOutVertex = 5.5f;   // Spitze aussen, Abstand von der Mitte
    constexpr float kOutTail = 0.5f;     // Schaftende dicht an der Mitte
    constexpr float kBackVertex = 2.5f;  // Spitze innen, aber mit Luft dazwischen
    constexpr float kBackTail = 7.5f;    // Schaftende aussen

    // Drucker: ein Blatt oben hinein, ein flacher Kasten, ein Blatt unten
    // heraus. Der erste Entwurf hatte statt des unteren Blattes einen
    // Ausgabeschlitz quer im Kasten -- am Bildschirm nachgesehen las sich das
    // wie ein Vorhaengeschloss: Buegel oben, Kasten, Balken darin. Der Kasten
    // ist deshalb flacher geworden, das Blatt breiter, und der Schlitz zum
    // heraustretenden Blatt.
    //
    // Der Kasten wird nicht als Rechteck gezogen, sondern zusammen mit dem
    // unteren Blatt als ein einziger geschlossener Umriss: seine Unterkante
    // bleibt dort offen, wo das Blatt hindurchtritt. Andernfalls laege das
    // Blatt vor einer durchgehenden Linie und saehe angeklebt aus statt
    // herauskommend -- und an den Stossstellen zweier Figuren blieben Kerben.
    constexpr float kPaperLeft = 6.5f;
    constexpr float kPaperRight = 17.5f;
    constexpr float kPaperTop = 3.5f;
    constexpr float kPaperOut = 20.5f;
    constexpr float kBodyLeft = 3.5f;
    constexpr float kBodyRight = 20.5f;
    constexpr float kBodyTop = 9.5f;
    constexpr float kBodyBottom = 16.5f;

    // Bogen von 315 Grad nach 225 Grad im Uhrzeigersinn -- die Luecke liegt
    // damit oben, die Pfeilspitze sitzt oben links und zeigt nach oben rechts.
    constexpr float kArcCx = 12.0f;
    constexpr float kArcCy = 12.0f;
    constexpr float kArcR = 6.5f;
    constexpr float kArcStartDeg = 315.0f;
    constexpr float kArcEndDeg = 225.0f;

    D2D1_POINT_2F OnCircle(float degrees)
    {
        const float rad = degrees * kPi / 180.0f;
        return D2D1::Point2F(kArcCx + kArcR * std::cos(rad), kArcCy + kArcR * std::sin(rad));
    }

    HRESULT CreateTriangle(ID2D1Factory* factory, ID2D1PathGeometry** out)
    {
        ComPtr<ID2D1PathGeometry> geometry;
        HRESULT hr = factory->CreatePathGeometry(&geometry);
        if (FAILED(hr))
            return hr;

        ComPtr<ID2D1GeometrySink> sink;
        hr = geometry->Open(&sink);
        if (FAILED(hr))
            return hr;

        sink->BeginFigure(D2D1::Point2F(8.5f, 4.5f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(17.0f, 12.0f));
        sink->AddLine(D2D1::Point2F(8.5f, 19.5f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);

        hr = sink->Close();
        if (FAILED(hr))
            return hr;

        *out = geometry.Detach();
        return S_OK;
    }

    HRESULT CreateChevron(ID2D1Factory* factory, ID2D1PathGeometry** out)
    {
        ComPtr<ID2D1PathGeometry> geometry;
        HRESULT hr = factory->CreatePathGeometry(&geometry);
        if (FAILED(hr))
            return hr;

        ComPtr<ID2D1GeometrySink> sink;
        hr = geometry->Open(&sink);
        if (FAILED(hr))
            return hr;

        // Ein Zug statt zweier Linien: die Spitze bekommt damit von selbst eine
        // saubere Gehrung, statt dass dort zwei stumpfe Enden aufeinandertreffen.
        sink->BeginFigure(D2D1::Point2F(9.0f, 4.5f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(16.0f, 12.0f));
        sink->AddLine(D2D1::Point2F(9.0f, 19.5f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);

        hr = sink->Close();
        if (FAILED(hr))
            return hr;

        *out = geometry.Detach();
        return S_OK;
    }

    HRESULT CreateArcBody(ID2D1Factory* factory, ID2D1PathGeometry** out)
    {
        ComPtr<ID2D1PathGeometry> geometry;
        HRESULT hr = factory->CreatePathGeometry(&geometry);
        if (FAILED(hr))
            return hr;

        ComPtr<ID2D1GeometrySink> sink;
        hr = geometry->Open(&sink);
        if (FAILED(hr))
            return hr;

        sink->BeginFigure(OnCircle(kArcStartDeg), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(OnCircle(kArcEndDeg), D2D1::SizeF(kArcR, kArcR), 0.0f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);

        hr = sink->Close();
        if (FAILED(hr))
            return hr;

        *out = geometry.Detach();
        return S_OK;
    }

    HRESULT CreateArcHead(ID2D1Factory* factory, ID2D1PathGeometry** out)
    {
        // Die Spitze folgt der Tangente am Bogenende, damit sie zwangslaeufig
        // in Bewegungsrichtung zeigt, statt von Hand gesetzt zu werden.
        const float rad = kArcEndDeg * kPi / 180.0f;
        const D2D1_POINT_2F end = OnCircle(kArcEndDeg);
        const float tx = -std::sin(rad);
        const float ty = std::cos(rad);
        const float nx = -ty;
        const float ny = tx;

        ComPtr<ID2D1PathGeometry> geometry;
        HRESULT hr = factory->CreatePathGeometry(&geometry);
        if (FAILED(hr))
            return hr;

        ComPtr<ID2D1GeometrySink> sink;
        hr = geometry->Open(&sink);
        if (FAILED(hr))
            return hr;

        sink->BeginFigure(D2D1::Point2F(end.x + tx * 3.8f, end.y + ty * 3.8f),
                          D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(end.x + nx * 2.7f, end.y + ny * 2.7f));
        sink->AddLine(D2D1::Point2F(end.x - nx * 2.7f, end.y - ny * 2.7f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);

        hr = sink->Close();
        if (FAILED(hr))
            return hr;

        *out = geometry.Detach();
        return S_OK;
    }

    HRESULT CreatePrinter(ID2D1Factory* factory, ID2D1PathGeometry** out)
    {
        ComPtr<ID2D1PathGeometry> geometry;
        HRESULT hr = factory->CreatePathGeometry(&geometry);
        if (FAILED(hr))
            return hr;

        ComPtr<ID2D1GeometrySink> sink;
        hr = geometry->Open(&sink);
        if (FAILED(hr))
            return hr;

        // Kasten und heraustretendes Blatt in einem Zug: links hoch, ueber die
        // Oberkante, rechts herunter, und die Unterkante nur bis zum Blatt,
        // dann um dieses herum. Alle Ecken bekommen dadurch eine Gehrung.
        sink->BeginFigure(D2D1::Point2F(kBodyLeft, kBodyBottom), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(kBodyLeft, kBodyTop));
        sink->AddLine(D2D1::Point2F(kBodyRight, kBodyTop));
        sink->AddLine(D2D1::Point2F(kBodyRight, kBodyBottom));
        sink->AddLine(D2D1::Point2F(kPaperRight, kBodyBottom));
        sink->AddLine(D2D1::Point2F(kPaperRight, kPaperOut));
        sink->AddLine(D2D1::Point2F(kPaperLeft, kPaperOut));
        sink->AddLine(D2D1::Point2F(kPaperLeft, kBodyBottom));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);

        // Das Blatt, das hineingeht: drei Seiten, unten offen -- dort steckt es
        // im Geraet, und die Oberkante des Kastens schliesst die Form ohnehin.
        sink->BeginFigure(D2D1::Point2F(kPaperLeft, kBodyTop), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(kPaperLeft, kPaperTop));
        sink->AddLine(D2D1::Point2F(kPaperRight, kPaperTop));
        sink->AddLine(D2D1::Point2F(kPaperRight, kBodyTop));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);

        hr = sink->Close();
        if (FAILED(hr))
            return hr;

        *out = geometry.Detach();
        return S_OK;
    }
}

Toolbar::Toolbar()
{
    buttons_ = {
        // Der Bildwechsel bleibt sichtbar, auch wenn der Ordner nur ein Bild
        // enthaelt: er ist der Hauptweg durch die Sammlung, und ein Betrachter,
        // der ihn erst zeigt, wenn es etwas zu zeigen gibt, sieht aus, als
        // koennte er es nicht.
        { ToolbarCommand::PreviousFile, L"Vorheriges Bild (Pfeil links)",
                                                                        false, true,  true,  false, {} },
        { ToolbarCommand::NextFile,     L"Nächstes Bild (Pfeil rechts)",
                                                                        false, false, true,  false, {} },

        // "Zurück"/"Weiter" statt "Seite": dieselben Knoepfe blaettern bei einem
        // TIFF durch Seiten und treten bei einem angehaltenen GIF durch
        // Einzelbilder. Ein fester Wortlaut passt in beiden Faellen.
        { ToolbarCommand::PreviousPage, L"Zurück (Bild auf)",           true,  true,  false, false, {} },
        { ToolbarCommand::PlayPause,    L"Anhalten / Fortsetzen (Leertaste)",
                                                                        false, false, false, false, {} },
        { ToolbarCommand::NextPage,     L"Weiter (Bild ab)",            false, false, false, false, {} },
        { ToolbarCommand::ZoomOut,      L"Verkleinern (Minustaste)",    true,  false, true,  false, {} },
        { ToolbarCommand::ZoomIn,       L"Vergrößern (Plustaste)",      false, false, true,  false, {} },
        { ToolbarCommand::FitToWindow,  L"Einpassen (Strg+B)",          false, false, true,  false, {} },
        { ToolbarCommand::ActualSize,   L"Originalgröße (Strg+0)",      false, false, true,  false, {} },
        { ToolbarCommand::RotateLeft,   L"Nach links drehen (Strg+L)",  true,  true,  true,  false, {} },
        { ToolbarCommand::RotateRight,  L"Nach rechts drehen (Strg+R)", false, false, true,  false, {} },

        // Drucken steht fuer sich. Es ist die einzige Funktion der Leiste, die
        // den Bildschirm verlaesst und etwas anstoesst, das sich nicht mit dem
        // naechsten Klick zuruecknehmen laesst -- ein Nachbar der Drehknoepfe
        // waere er zu leicht im Vorbeigehen getroffen.
        { ToolbarCommand::Print,        L"Drucken (Strg+P)",            true,  false, true,  false, {} },

        // "ein/aus" statt "Vollbild": der Knopf fuehrt in beide Richtungen, die
        // Kurzhilfe steht aber fest -- sie jedes Mal umzumelden waere Aufwand
        // fuer einen Wortlaut, den ohnehin nur einer von beiden Zustaenden liest.
        { ToolbarCommand::Fullscreen,   L"Vollbild ein/aus (F11)",      true,  false, true,  false, {} },
    };
}

HRESULT Toolbar::CreateResources(ID2D1Factory* factory)
{
    HRESULT hr = CreateTriangle(factory, triangle_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
        return hr;
    hr = CreateChevron(factory, chevron_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
        return hr;
    hr = CreateArcBody(factory, arcBody_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
        return hr;
    hr = CreateArcHead(factory, arcHead_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
        return hr;
    return CreatePrinter(factory, printer_.ReleaseAndGetAddressOf());
}

void Toolbar::DiscardDeviceResources()
{
    brush_.Reset();
}

void Toolbar::SetDpiScale(float scale)
{
    dpiScale_ = scale > 0.0f ? scale : 1.0f;
}

float Toolbar::Height() const
{
    return std::floor(kBarHeight * dpiScale_ + 0.5f);
}

const ToolbarButton* Toolbar::Find(ToolbarCommand command) const
{
    for (const ToolbarButton& b : buttons_)
        if (b.command == command)
            return &b;
    return nullptr;
}

ToolbarButton* Toolbar::Find(ToolbarCommand command)
{
    for (ToolbarButton& b : buttons_)
        if (b.command == command)
            return &b;
    return nullptr;
}

void Toolbar::SetVisible(ToolbarCommand command, bool visible)
{
    if (ToolbarButton* b = Find(command))
        b->visible = visible;
}

void Toolbar::SetEnabled(ToolbarCommand command, bool enabled)
{
    if (ToolbarButton* b = Find(command))
        b->enabled = enabled;
}

bool Toolbar::IsEnabled(ToolbarCommand command) const
{
    const ToolbarButton* b = Find(command);
    return b != nullptr && b->visible && b->enabled;
}

void Toolbar::Layout(float clientWidth, float clientHeight)
{
    const float height = Height();
    const float button = std::floor(kButtonSize * dpiScale_ + 0.5f);
    const float gap = std::floor(kButtonGap * dpiScale_ + 0.5f);
    const float groupGap = std::floor(kGroupGap * dpiScale_ + 0.5f);

    strip_ = D2D1::RectF(0.0f, clientHeight - height, clientWidth, clientHeight);
    separators_.clear();

    float total = 0.0f;
    bool anyPlaced = false;
    for (const ToolbarButton& b : buttons_)
    {
        if (!b.visible)
            continue;
        if (anyPlaced)
            total += b.groupStart ? groupGap : gap;
        total += button;
        anyPlaced = true;
    }

    float x = std::floor((clientWidth - total) * 0.5f);
    const float y = std::floor(strip_.top + (height - button) * 0.5f);

    anyPlaced = false;
    for (ToolbarButton& b : buttons_)
    {
        if (!b.visible)
        {
            b.rect = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
            continue;
        }
        if (anyPlaced)
        {
            if (b.groupStart)
            {
                separators_.push_back(x + std::floor(groupGap * 0.5f));
                x += groupGap;
            }
            else
            {
                x += gap;
            }
        }
        b.rect = D2D1::RectF(x, y, x + button, y + button);
        x += button;
        anyPlaced = true;
    }
}

void Toolbar::Draw(ID2D1RenderTarget* target)
{
    if (!brush_)
    {
        if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(kBarFill), &brush_)))
            return;
    }

    brush_->SetColor(D2D1::ColorF(kBarFill));
    target->FillRectangle(strip_, brush_.Get());

    brush_->SetColor(D2D1::ColorF(kBarBorder));
    target->FillRectangle(D2D1::RectF(strip_.left, strip_.top, strip_.right, strip_.top + 1.0f),
                          brush_.Get());

    const float sepInset = std::floor(9.0f * dpiScale_ + 0.5f);
    brush_->SetColor(D2D1::ColorF(kSeparator));
    for (float sx : separators_)
    {
        target->FillRectangle(
            D2D1::RectF(sx, strip_.top + sepInset, sx + 1.0f, strip_.bottom - sepInset),
            brush_.Get());
    }

    for (const ToolbarButton& b : buttons_)
    {
        if (!b.visible)
            continue;

        if (b.enabled && b.command == pressed_)
        {
            brush_->SetColor(D2D1::ColorF(kPressedFill));
            target->FillRectangle(b.rect, brush_.Get());
        }
        else if (b.enabled && b.command == hover_ && pressed_ == ToolbarCommand::None)
        {
            brush_->SetColor(D2D1::ColorF(kHoverFill));
            target->FillRectangle(b.rect, brush_.Get());
        }

        brush_->SetColor(D2D1::ColorF(b.enabled ? kIconNormal : kIconDisabled));
        DrawIcon(target, b, brush_.Get());
    }
}

void Toolbar::DrawIcon(ID2D1RenderTarget* target, const ToolbarButton& button, ID2D1Brush* brush)
{
    const float icon = std::floor(kIconSize * dpiScale_ + 0.5f);
    const float scale = icon / kIconBox;
    const float cx = (button.rect.left + button.rect.right) * 0.5f;
    const float cy = (button.rect.top + button.rect.bottom) * 0.5f;

    D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Scale(scale, scale);
    if (button.mirrored)
    {
        transform = D2D1::Matrix3x2F::Scale(-1.0f, 1.0f,
                                            D2D1::Point2F(kIconBox * 0.5f, kIconBox * 0.5f)) *
                    transform;
    }
    transform = transform * D2D1::Matrix3x2F::Translation(cx - kIconBox * 0.5f * scale,
                                                          cy - kIconBox * 0.5f * scale);
    target->SetTransform(transform);

    switch (button.command)
    {
    case ToolbarCommand::PreviousFile:
    case ToolbarCommand::NextFile:
        if (chevron_)
            target->DrawGeometry(chevron_.Get(), brush, kChevronStroke);
        break;

    case ToolbarCommand::PreviousPage:
    case ToolbarCommand::NextPage:
        if (triangle_)
        {
            // Die Verschiebung wirkt im Entwurfsraster, also noch vor der
            // Spiegelung -- beim Zurueck-Knopf wandert der Balken dadurch von
            // selbst auf die andere Seite.
            target->SetTransform(D2D1::Matrix3x2F::Translation(-kStepShift, 0.0f) * transform);
            target->FillGeometry(triangle_.Get(), brush);
            target->SetTransform(transform);
        }
        target->FillRectangle(D2D1::RectF(kStepBarLeft, kStepBarTop,
                                          kStepBarLeft + kStepBarWidth, kStepBarBottom), brush);
        break;

    case ToolbarCommand::PlayPause:
        DrawPlayPause(target, brush);
        break;

    case ToolbarCommand::ZoomOut:
        DrawMagnifier(target, brush, false);
        break;

    case ToolbarCommand::ZoomIn:
        DrawMagnifier(target, brush, true);
        break;

    case ToolbarCommand::FitToWindow:
        DrawFitMarks(target, brush);
        break;

    case ToolbarCommand::ActualSize:
        DrawOneToOne(target, brush);
        break;

    case ToolbarCommand::Print:
        if (printer_)
            target->DrawGeometry(printer_.Get(), brush, kStrokeWidth);
        break;

    case ToolbarCommand::Fullscreen:
        DrawFullscreen(target, brush);
        break;

    case ToolbarCommand::RotateLeft:
    case ToolbarCommand::RotateRight:
        if (arcBody_)
            target->DrawGeometry(arcBody_.Get(), brush, kStrokeWidth);
        if (arcHead_)
            target->FillGeometry(arcHead_.Get(), brush);
        break;

    default:
        break;
    }

    target->SetTransform(D2D1::Matrix3x2F::Identity());
}

void Toolbar::DrawMagnifier(ID2D1RenderTarget* target, ID2D1Brush* brush, bool plus)
{
    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(kLensCx, kLensCy), kLensR, kLensR),
                        brush, kStrokeWidth);

    // Der Griff beginnt knapp innerhalb des Kreisrands, damit an der Naht
    // kein heller Spalt aufblitzt.
    const float edge = kLensR * 0.70710678f;
    target->DrawLine(D2D1::Point2F(kLensCx + edge - 0.5f, kLensCy + edge - 0.5f),
                     D2D1::Point2F(19.5f, 19.5f), brush, kStrokeWidth);

    target->DrawLine(D2D1::Point2F(kLensCx - kLensBar, kLensCy),
                     D2D1::Point2F(kLensCx + kLensBar, kLensCy), brush, kStrokeWidth);
    if (plus)
    {
        target->DrawLine(D2D1::Point2F(kLensCx, kLensCy - kLensBar),
                         D2D1::Point2F(kLensCx, kLensCy + kLensBar), brush, kStrokeWidth);
    }
}

void Toolbar::DrawFitMarks(ID2D1RenderTarget* target, ID2D1Brush* brush)
{
    const float lo = kMarkInset;
    const float hi = kIconBox - kMarkInset;

    for (int corner = 0; corner < 4; ++corner)
    {
        const bool right = (corner & 1) != 0;
        const bool bottom = (corner & 2) != 0;
        const float x = right ? hi : lo;
        const float y = bottom ? hi : lo;
        const float dx = right ? -1.0f : 1.0f;
        const float dy = bottom ? -1.0f : 1.0f;

        target->DrawLine(D2D1::Point2F(x, y - dy * kMarkOver),
                         D2D1::Point2F(x, y + dy * kMarkArm), brush, kStrokeWidth);
        target->DrawLine(D2D1::Point2F(x - dx * kMarkOver, y),
                         D2D1::Point2F(x + dx * kMarkArm, y), brush, kStrokeWidth);
    }
}

void Toolbar::DrawOneToOne(ID2D1RenderTarget* target, ID2D1Brush* brush)
{
    // "1:1" aus Strichen statt aus Text -- so bleibt die Leiste ohne
    // Schriftverwaltung auskommend und auf jeder DPI-Stufe gleich aufgebaut.
    for (const float stem : { 8.5f, 17.5f })
    {
        target->DrawLine(D2D1::Point2F(stem, 6.5f), D2D1::Point2F(stem, 17.5f),
                         brush, kStrokeWidth);
        target->DrawLine(D2D1::Point2F(stem, 6.5f), D2D1::Point2F(stem - 2.3f, 8.3f),
                         brush, kStrokeWidth);
    }

    target->FillRectangle(D2D1::RectF(12.05f, 8.65f, 13.95f, 10.55f), brush);
    target->FillRectangle(D2D1::RectF(12.05f, 13.45f, 13.95f, 15.35f), brush);
}

void Toolbar::DrawPlayPause(ID2D1RenderTarget* target, ID2D1Brush* brush)
{
    // Der Knopf zeigt, was ein Klick bewirkt: waehrend der Wiedergabe die
    // Pausenmarke, im Halt das Dreieck.
    if (!playing_)
    {
        if (triangle_)
            target->FillGeometry(triangle_.Get(), brush);
        return;
    }

    const float center = kIconBox * 0.5f;
    const float inner = kPauseGap * 0.5f;
    target->FillRectangle(
        D2D1::RectF(center - inner - kPauseBar, kPauseTop, center - inner, kPauseBottom), brush);
    target->FillRectangle(
        D2D1::RectF(center + inner, kPauseTop, center + inner + kPauseBar, kPauseBottom), brush);
}

void Toolbar::DrawFullscreen(ID2D1RenderTarget* target, ID2D1Brush* brush)
{
    // Zwei Pfeile, die auseinanderstreben -- im Vollbild umgekehrt, dann zeigen
    // sie zurueck zur Mitte. Der Knopf sagt damit wie der Wiedergabeknopf, was
    // ein Klick bewirkt, nicht was gerade gilt.
    const float center = kIconBox * 0.5f;
    const float vertexDistance = fullscreen_ ? kBackVertex : kOutVertex;
    const float tailDistance = fullscreen_ ? kBackTail : kOutTail;

    for (int i = 0; i < 2; ++i)
    {
        // i = 0 links oben, i = 1 rechts unten; das Vorzeichen dreht die ganze
        // Konstruktion, statt sie ein zweites Mal hinzuschreiben.
        const float sign = (i == 0) ? 1.0f : -1.0f;
        const float vertex = center - sign * vertexDistance;
        const float tail = center - sign * tailDistance;

        // Die Arme der Spitze weisen zum Schaft hin -- damit zeigt der Winkel
        // von selbst dorthin, wo es hingeht, ohne dass die Richtung noch
        // einmal gesondert bestimmt werden muesste.
        const float arm = (tail > vertex ? 1.0f : -1.0f) * kArrowArm;

        target->DrawLine(D2D1::Point2F(vertex, vertex), D2D1::Point2F(tail, tail), brush,
                         kStrokeWidth);
        target->DrawLine(D2D1::Point2F(vertex, vertex), D2D1::Point2F(vertex + arm, vertex),
                         brush, kStrokeWidth);
        target->DrawLine(D2D1::Point2F(vertex, vertex), D2D1::Point2F(vertex, vertex + arm),
                         brush, kStrokeWidth);
    }
}

ToolbarCommand Toolbar::HitTest(float x, float y) const
{
    for (const ToolbarButton& b : buttons_)
    {
        if (!b.visible || !b.enabled)
            continue;
        if (x >= b.rect.left && x < b.rect.right && y >= b.rect.top && y < b.rect.bottom)
            return b.command;
    }
    return ToolbarCommand::None;
}

bool Toolbar::SetHover(ToolbarCommand command)
{
    if (hover_ == command)
        return false;
    hover_ = command;
    return true;
}

bool Toolbar::SetPressed(ToolbarCommand command)
{
    if (pressed_ == command)
        return false;
    pressed_ = command;
    return true;
}
