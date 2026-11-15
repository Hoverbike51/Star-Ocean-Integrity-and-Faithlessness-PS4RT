// Shared operation panels. The updater/extractor still owns progress and cancellation.
void draw_operation_modal(Graphics& g, REAL width, REAL height) {
    app.hits.clear();
    hit(HitModalBackdrop, 0, 0, width, height);
    SolidBrush veil(Color(205, 0, 5, 12));
    g.FillRectangle(&veil, 0.f, 0.f, width, height);
    const bool pkg = app.modal_style == ModalStyle::Pkg;
    const bool offer = app.modal_action == ModalAction::UpdateOffer;
    const bool confirm = app.modal_action == ModalAction::ConfirmExtraction;
    const bool extracting = app.extraction_active && app.modal_action == ModalAction::CancelExtraction;
    const bool updating = app.modal_action == ModalAction::UpdateProgress;
    const bool progress = extracting || updating;
    const bool success = app.modal_style == ModalStyle::UpdateSuccess;
    const REAL pw = std::min<REAL>(840, width - 64);
    // Cache text measurement by content/language/width; counters do not resize the panel.
    static std::wstring measured_body, measured_language;
    static REAL measured_width = 0, measured_height = 0;
    if (measured_body != app.modal_body || measured_language != app.launcher_language || measured_width != pw) {
        RectF bounds;
        g.MeasureString(app.modal_body.c_str(), -1, cached_font(app.modal_body, 15, FontStyleRegular),
                        RectF(0, 0, pw - 64, 2000), &bounds);
        measured_body = app.modal_body; measured_language = app.launcher_language;
        measured_width = pw; measured_height = bounds.Height + 12;
    }
    const REAL ph = confirm ? 490 : extracting ? 540 : offer ?
                    (app.update_release && app.update_release->experimental ? 454 : 430) : updating ? 408 :
                    success ? 416 : std::clamp<REAL>(measured_height + (pkg ? 260 : 210), pkg ? 350 : 310, height - 56);
    const REAL x = (width - pw) / 2, y = (height - ph) / 2;
    app.modal_bounds = {LONG(std::floor((x - 3) * app.ui_scale)), LONG(std::floor((y - 3) * app.ui_scale)),
                        LONG(std::ceil((x + pw + 3) * app.ui_scale)), LONG(std::ceil((y + ph + 3) * app.ui_scale))};
    app.modal_progress_bounds = progress ? RECT{
        LONG(std::floor((x + 28) * app.ui_scale)), LONG(std::floor((y + ph - 171) * app.ui_scale)),
        LONG(std::ceil((x + pw - 28) * app.ui_scale)), LONG(std::ceil((y + ph - 118) * app.ui_scale))} : RECT{};
    app.modal_action_bounds = progress ? RECT{
        LONG(std::floor((x + pw - 273) * app.ui_scale)), LONG(std::floor((y + ph - 70) * app.ui_scale)),
        LONG(std::ceil((x + pw - 29) * app.ui_scale)), LONG(std::ceil((y + ph - 18) * app.ui_scale))} : RECT{};
    const Color cyan(255, 82, 207, 244), green(255, 105, 223, 173);
    const Color white(255, 240, 247, 252), muted(255, 177, 200, 216);
    SolidBrush border(Color(150, 64, 169, 198)), panel(Color(255, 5, 22, 36));
    rounded(g, RectF(x - 1.5f, y - 1.5f, pw + 3, ph + 3), 20, border);
    rounded(g, RectF(x, y, pw, ph), 18, panel);
    SolidBrush icon_bg(Color(255, 13, 47, 63));
    rounded(g, RectF(x + 28, y + 24, 44, 44), 12, icon_bg);
    Pen icon(success ? green : cyan, 2);
    const REAL cx = x + 50, cy = y + 45;
    if (success) {
        g.DrawLine(&icon, cx - 10, cy, cx - 3, cy + 7);
        g.DrawLine(&icon, cx - 3, cy + 7, cx + 11, cy - 8);
    } else {
        g.DrawLine(&icon, cx, cy - 11, cx, cy + 5);
        g.DrawLine(&icon, cx - 6, cy, cx, cy + 6);
        g.DrawLine(&icon, cx + 6, cy, cx, cy + 6);
        g.DrawLine(&icon, cx - 11, cy + 11, cx + 11, cy + 11);
    }
    text(g, pkg ? loc(L"Extraction du PKG", L"PKG extraction") : update_dialog_title(),
         x + 86, y + 24, pw - 164, 44, 20, white, FontStyleBold);
    if (!progress) button(g, app.modal_secondary.empty() ? HitModalPrimary : HitModalSecondary,
                          L"×", x + pw - 62, y + 27, 34, 36);
    SolidBrush divider(Color(65, 82, 207, 244)), field(Color(255, 12, 38, 54));
    g.FillRectangle(&divider, RectF(x + 28, y + 85, pw - 56, 1));
    if (offer || success) {
        text(g, success ? loc(L"Installation terminée", L"Installation complete") : loc(L"Mise à jour disponible", L"Update available"),
             x + 32, y + 103, pw - 64, 32, 22, success ? green : white, FontStyleBold);
        const REAL band_y = y + 151, col = (pw - 94) / 2;
        rounded(g, RectF(x + 32, band_y, pw - 64, 78), 10, field);
        if (offer) {
            text(g, loc(L"Version installée", L"Installed version"), x + 48, band_y + 5, col - 24, 28, 12, muted);
            text(g, kVersion, x + 48, band_y + 32, col - 24, 34, 22, white, FontStyleBold);
            text(g, L"→", x + pw / 2 - 16, band_y + 22, 32, 34, 22, cyan, FontStyleBold, StringAlignmentCenter);
            text(g, loc(L"Nouvelle version", L"New version"), x + pw / 2 + 28, band_y + 5, col - 24, 28, 12, muted);
            const auto version = app.update_release ? app.update_release->version.String() : std::string{};
            text(g, from_utf8(version.c_str(), int(version.size())), x + pw / 2 + 28, band_y + 32, col - 24, 34, 22, cyan, FontStyleBold);
        } else {
            text(g, loc(L"Version installée", L"Installed version"), x + 48, band_y + 5, pw - 96, 28, 12, muted);
            text(g, kVersion, x + 48, band_y + 32, pw - 96, 34, 22, green, FontStyleBold);
        }
        paragraph(g, success ? app.modal_body : loc(L"Le lanceur redémarrera après l’installation.", L"The launcher will restart after installation."),
                  x + 32, y + 244, pw - 64, 44, 15, muted);
        text(g, loc(L"Vos sauvegardes et paramètres sont conservés.", L"Your saves and settings are kept."),
             x + 32, y + 290, pw - 64, 30, 14, green);
        if (offer && app.update_release && app.update_release->experimental)
            text(g, loc(L"Cette version est expérimentale.", L"This release is experimental."), x + 32, y + 324, pw - 64, 24, 12, Color(255, 255, 204, 123));
    } else if (confirm || extracting) {
        text(g, confirm ? loc(L"Extraire ce PKG ?", L"Extract this PKG?") : loc(L"Extraction du PKG en cours…", L"Extracting PKG…"),
             x + 32, y + 103, pw - 64, 32, 22, white, FontStyleBold);
        text(g, loc(L"Paquet", L"Package"), x + 32, y + 145, pw - 64, 26, 12, cyan, FontStyleBold);
        paragraph(g, app.extraction_source, x + 32, y + 175, pw - 64, 48, 14, white);
        text(g, std::wstring(loc(L"Taille : ", L"Size: ")) + format_size(app.extraction_size),
             x + 32, y + 227, pw - 64, 26, 13, muted);
        text(g, loc(L"Dossier d’extraction", L"Extraction folder"), x + 32, y + 264, pw - 64, 26, 13, cyan, FontStyleBold);
        const REAL folder_width = confirm ? pw - 310 : pw - 64;
        rounded(g, RectF(x + 32, y + 299, folder_width, 49), 8, field);
        text(g, app.extraction_destination, x + 44, y + 303, folder_width - 24, 40, 13, white);
        if (confirm) {
            button(g, HitModalTertiary, app.modal_tertiary, x + pw - 264, y + 299, 232, 49);
            text(g, loc(L"Choisissez où extraire les fichiers du jeu.", L"Choose where the game files will be extracted."),
                 x + 32, y + 355, pw - 64, 30, 13, muted);
        }
    } else {
        if (pkg) {
            text(g, app.modal_title, x + 32, y + 101, pw - 64, 34, 20, white, FontStyleBold);
            paragraph(g, app.modal_body, x + 32, y + 145, pw - 64, ph - 245, 15, muted);
        } else paragraph(g, app.modal_body, x + 32, y + 107, pw - 64, ph - 212, 15, muted);
    }
    if (progress) {
        std::uint64_t done = 0, total = 0;
        bool cancel_enabled = true;
        std::wstring detail;
        if (updating) {
            const auto phase = app.update_phase.load();
            detail = app.update_cancel ? loc(L"Annulation…", L"Canceling…") :
                phase == So5Update::Phase::Download ? loc(L"Téléchargement…", L"Downloading…") :
                phase == So5Update::Phase::Verify ? loc(L"Vérification du paquet…", L"Verifying package…") :
                phase == So5Update::Phase::Extract ? loc(L"Extraction…", L"Extracting…") :
                phase == So5Update::Phase::Install ? loc(L"Préparation de l’installation…", L"Preparing installation…") :
                                                   loc(L"Vérification de l’application…", L"Checking application…");
            // Verify/check/install have no byte-based progress. Do not reuse the preceding phase's total.
            if (phase == So5Update::Phase::Download || phase == So5Update::Phase::Extract) {
                total = app.update_total.load(); done = std::min(app.update_done.load(), total);
            }
            cancel_enabled = !app.update_cancel && phase != So5Update::Phase::Install;
            paragraph(g, app.modal_body, x + 32, y + 107, pw - 64, 110, 15, muted);
        } else {
            total = app.extraction_total.load(); done = std::min(app.extraction_done.load(), total);
            detail = app.extraction_cancel_requested ? loc(L"Annulation…", L"Canceling…") :
                !total ? loc(L"Analyse du PKG…", L"Analyzing PKG…") : done == total ? loc(L"Finalisation…", L"Finalizing…") : L"";
            cancel_enabled = !app.extraction_cancel_requested;
        }
        if (total) {
            const auto percent = std::min<std::uint64_t>(extracting ? 99 : 100, std::uint64_t(100.0 * double(done) / double(total)));
            if (!detail.empty()) detail += L"  ";
            detail += std::to_wstring(percent) + L" % · " + format_size(done) + L" / " + format_size(total);
        }
        const REAL py = y + ph - 139;
        text(g, detail, x + 32, py - 29, pw - (extracting ? 177 : 64), 26, 13, cyan, FontStyleBold);
        if (extracting) {
            const auto elapsed = (GetTickCount64() - app.extraction_started) / 1000;
            wchar_t clock[32]{}; swprintf_s(clock, L"%02llu:%02llu", elapsed / 60, elapsed % 60);
            text(g, clock, x + pw - 145, py - 29, 113, 26, 13, muted, FontStyleRegular, StringAlignmentFar);
        }
        SolidBrush track(Color(255, 17, 49, 67)), fill(cyan);
        rounded(g, RectF(x + 32, py + 6, pw - 64, 10), 5, track);
        if (total && done) {
            const REAL filled = (pw - 64) * REAL(std::min(extracting ? .99 : 1., double(done) / double(total)));
            rounded(g, RectF(x + 32, py + 6, filled, 10), std::min<REAL>(5, filled / 2), fill);
        } else if (!total) {
            const REAL travel = pw - 224;
            const REAL offset = REAL((GetTickCount64() / 7) % ULONGLONG(std::max<REAL>(1, travel)));
            rounded(g, RectF(x + 32 + offset, py + 6, 160, 10), 5, fill);
        }
        g.FillRectangle(&divider, RectF(x + 32, y + ph - 88, pw - 64, 1));
        button(g, HitModalPrimary, app.modal_primary, x + pw - 270, y + ph - 67, 238, 46, false, cancel_enabled);
        return;
    }
    g.FillRectangle(&divider, RectF(x + 32, y + ph - 88, pw - 64, 1));
    const REAL by = y + ph - 67, primary_width = offer ? 276 : 220;
    if (!app.modal_secondary.empty()) button(g, HitModalSecondary, app.modal_secondary, x + 32, by, 160, 46);
    if (!app.modal_tertiary.empty() && !confirm) {
        const REAL bw = app.modal_secondary.empty() ? 310 : 242;
        const REAL bx = app.modal_secondary.empty() ? x + 32 : x + pw - primary_width - bw - 44;
        button(g, HitModalTertiary, app.modal_tertiary, bx, by, bw, 46);
    }
    button(g, HitModalPrimary, app.modal_primary, x + pw - primary_width - 32, by, primary_width, 46, true);
}
