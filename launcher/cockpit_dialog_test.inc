// Explicit read-only UI fixture: no update checks, game extraction or owner settings.
int operation_ui_test(const fs::path& output) {
    fs::create_directories(output);
    GdiplusStartupInput input;
    if (GdiplusStartup(&app.gdiplus_token, &input, nullptr) != Ok) return 2;
    app.background.reset(Image::FromFile((app.root / L"launcher/assets/launcher-background.png").c_str()));
    app.launcher_icon.reset(Image::FromFile((app.root / L"launcher/assets/launcher-icon.png").c_str()));
    app.game_logo.reset(Image::FromFile((app.root / L"launcher/assets/star-ocean-logo.png").c_str()));
    app.page = Page::Home;
    So5Update::Release release;
    release.version = *So5Update::Version::Parse("1.1.4"); // Illustrative offer, not a network result.
    app.update_release = release;
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    std::ofstream report(output / L"operations.json");
    report << "{\"samples\":[\n";
    bool first = true, passed = true;
    bool pixels_match = true;
    const wchar_t* languages[] = {L"English", L"Français", L"Español", L"Português", L"Português (Brasil)", L"Italiano", L"Deutsch", L"Русский", L"简体中文", L"繁體中文", L"한국어", L"日本語"};
    const CLSID png = {0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    for (int language = 0; language < 12; ++language) {
        app.launcher_language = languages[language];
        for (const auto size : {std::pair{1080,640}, std::pair{1580,940}}) {
            Bitmap canvas(size.first, size.second, PixelFormat32bppPARGB);
            app.modal_visible = false; app.popup_hit = HitFsr; app.popup_values = {L"A",L"B",L"C"};
            app.popup_enabled = {true,false,true};
            { Graphics g(&canvas); render_launcher(g, size.first, size.second); }
            passed &= hit_at(POINT{400,60}) == 1999;
            for (int navigation : {HitHome,HitGame,HitCheats,HitTweaks,HitVideo,HitControls,HitAdvanced,HitLogs}) {
                activate(navigation); passed &= app.page == Page::Home && app.popup_hit == HitFsr;
            }
            app.hover = HitNone; app.popup_scroll = 0;
            navigate_selector(1); passed &= app.hover == 2000;
            navigate_selector(1); passed &= app.hover == 2002;
            navigate_selector(-1); passed &= app.hover == 2000;
            activate(1999); passed &= app.popup_hit == HitNone;
            app.popup_hit = HitFsr; app.popup_values = {L"A",L"B"};
            std::thread([] { show_modal(L"Fixture",L"Worker result",L"OK"); }).join();
            { Graphics g(&canvas); render_launcher(g,size.first,size.second); }
            passed &= app.popup_hit == HitNone && app.popup_values.empty();
            app.modal_visible = false;
            for (int state = 0; state < 8; ++state) {
                app.modal_visible = true; app.modal_style = state == 4 ? ModalStyle::UpdateSuccess : state >= 5 ? ModalStyle::Pkg : ModalStyle::Update;
                app.modal_title = update_dialog_title(); app.modal_primary = loc(L"Terminé",L"Done");
                app.modal_secondary.clear(); app.modal_tertiary.clear(); app.modal_body = loc(L"Mise à jour installée avec succès.",L"Update installed successfully.");
                app.modal_action = ModalAction::None; app.extraction_active = false; app.update_cancel = false;
                if (state == 0) {
                    app.update_release->experimental = size.first == 1080;
                    app.modal_action = ModalAction::UpdateOffer;
                    app.modal_primary = loc(L"Télécharger et installer",L"Download & install");
                    app.modal_secondary = loc(L"Plus tard",L"Later"); app.modal_tertiary = loc(L"Notes de version",L"Release notes");
                } else if (state < 4) {
                    app.modal_action = ModalAction::UpdateProgress; app.modal_primary = loc(L"Annuler",L"Cancel");
                    app.update_phase = state == 1 ? So5Update::Phase::Download : state == 2 ? So5Update::Phase::Verify : So5Update::Phase::Install;
                    app.update_total = 100000000; app.update_done = 47000000;
                    app.modal_body = loc(L"Préparation de la mise à jour. Le lanceur va se fermer et redémarrer automatiquement.",L"Preparing the update. The launcher will close and restart automatically.");
                } else if (state == 4) {
                    app.modal_action = ModalAction::UpdateResult; app.modal_tertiary = loc(L"Ouvrir le dossier de secours",L"Open backup folder");
                } else if (state < 7) {
                    app.modal_action = state == 5 ? ModalAction::ConfirmExtraction : ModalAction::CancelExtraction;
                    app.modal_primary = state == 5 ? loc(L"Extraire",L"Extract") : loc(L"Annuler l’extraction",L"Cancel extraction");
                    app.extraction_active = state == 6; app.extraction_started = GetTickCount64() - 65000;
                    app.extraction_source = L"Star.Ocean.Integrity.and.Faithlessness-CUSA03219-EUR-Game.pkg";
                    app.extraction_destination = L"D:\\Games\\STAR OCEAN 5\\Extracted game files\\CUSA03219";
                    app.extraction_size = 26600000000ULL; app.extraction_done = 12300000000ULL; app.extraction_total = 26600000000ULL;
                    if (state == 5) { app.modal_secondary = loc(L"Annuler",L"Cancel"); app.modal_tertiary = loc(L"Changer le dossier",L"Change folder"); }
                } else app.modal_body = loc(L"Impossible de démarrer la capture des diagnostics.",L"Could not start the diagnostics capture.");
                app.hover = HitNone;
                { Graphics g(&canvas); render_launcher(g, size.first, size.second); }
                RECT hover{};
                bool primary = false, tertiary = false;
                for (const auto& [id, area] : app.hits) {
                    passed &= id == HitModalBackdrop || id == HitModalPrimary || id == HitModalSecondary || id == HitModalTertiary;
                    if (id == HitModalPrimary) { primary = true; hover = area; }
                    tertiary |= id == HitModalTertiary;
                }
                passed &= primary == (state != 3);
                if (state == 5) passed &= tertiary;
                passed &= app.modal_bounds.left >= 0 && app.modal_bounds.top >= 0 && app.modal_bounds.right <= size.first && app.modal_bounds.bottom <= size.second;
                std::vector<double> partial, hover_times;
                for (int frame = 0; frame < 32; ++frame) {
                    app.update_done = 47000000 + frame * 1000;
                    for (int kind = 0; kind < 2; ++kind) {
                        if (kind == 1 && !primary) continue;
                        app.hover = frame % 2 ? HitModalPrimary : HitNone;
                        RECT area = kind == 0 ? (app.modal_progress_bounds.right > app.modal_progress_bounds.left ? app.modal_progress_bounds : app.modal_bounds) : hover;
                        if (area.right <= area.left) area = app.modal_bounds;
                        InflateRect(&area, 3, 3);
                        LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
                        { Graphics g(&canvas); g.SetClip(Rect(area.left,area.top,area.right-area.left,area.bottom-area.top));
                          if (kind == 0 && app.modal_progress_bounds.right > app.modal_progress_bounds.left) {
                              const auto& action = app.modal_action_bounds;
                              g.SetClip(Rect(action.left-3,action.top-3,action.right-action.left+6,action.bottom-action.top+6),CombineModeUnion);
                          }
                          render_launcher(g,size.first,size.second); g.Flush(FlushIntentionSync); }
                        QueryPerformanceCounter(&end);
                        if (frame >= 2) (kind == 0 ? partial : hover_times).push_back((end.QuadPart-begin.QuadPart)*1000.0/frequency.QuadPart);
                    }
                }
                std::sort(partial.begin(),partial.end()); std::sort(hover_times.begin(),hover_times.end());
                // Progress and hover clips must reconstruct the same pixels as a full
                // repaint. Indeterminate animations intentionally depend on real time.
                bool case_pixels = true;
                if (state != 2 && state != 3) {
                    app.extraction_started = GetTickCount64() - 65000;
                    { Graphics g(&canvas); Region region; region.MakeEmpty();
                      if (app.modal_progress_bounds.right > app.modal_progress_bounds.left) {
                          const auto& p = app.modal_progress_bounds; const auto& b = app.modal_action_bounds;
                          region.Union(Rect(p.left-3,p.top-3,p.right-p.left+6,p.bottom-p.top+6));
                          region.Union(Rect(b.left-3,b.top-3,b.right-b.left+6,b.bottom-b.top+6));
                      } else { const auto& b = app.modal_bounds; region.Union(Rect(b.left-3,b.top-3,b.right-b.left+6,b.bottom-b.top+6)); }
                      g.SetClip(&region); render_launcher(g,size.first,size.second); }
                    Bitmap expected(size.first,size.second,PixelFormat32bppPARGB);
                    { Graphics g(&expected); render_launcher(g,size.first,size.second); }
                    Rect area(0,0,size.first,size.second); BitmapData a{},b{}; bool match = false;
                    if (canvas.LockBits(&area,ImageLockModeRead,PixelFormat32bppPARGB,&a) == Ok) {
                        if (expected.LockBits(&area,ImageLockModeRead,PixelFormat32bppPARGB,&b) == Ok) {
                            match = true;
                            for (int row=0;row<size.second;++row) if (std::memcmp(static_cast<const char*>(a.Scan0)+row*a.Stride,static_cast<const char*>(b.Scan0)+row*b.Stride,size.first*4)) { match=false; break; }
                            expected.UnlockBits(&b);
                        }
                        canvas.UnlockBits(&a);
                    }
                    pixels_match &= match;
                    case_pixels = match;
                    if (!match && language == 0 && size.first == 1080) {
                        canvas.Save((output/(L"partial-"+std::to_wstring(state)+L".png")).c_str(),&png,nullptr);
                        expected.Save((output/(L"expected-"+std::to_wstring(state)+L".png")).c_str(),&png,nullptr);
                    }
                }
                if (!first) report << ",\n"; first = false;
                report << "{\"language\":" << language << ",\"width\":" << size.first << ",\"state\":" << state
                       << ",\"refresh_p95_ms\":" << partial[partial.size()*95/100] << ",\"hover_p95_ms\":"
                       << (hover_times.empty() ? "null" : std::to_string(hover_times[hover_times.size()*95/100]))
                       << ",\"partial_pixels_match\":" << (case_pixels?"true":"false") << "}";
                if (size.first == 1580 && (language == 0 || language == 1 || language == 6 || language == 11))
                    canvas.Save((output/(L"operation-"+std::to_wstring(language)+L"-"+std::to_wstring(state)+L".png")).c_str(),&png,nullptr);
            }
        }
    }
    report << "\n],\"input_and_geometry_passed\":" << (passed?"true":"false")
           << ",\"partial_redraw_matches_full\":" << (pixels_match?"true":"false") << "}\n"; report.flush();
    drawing_cache.Clear(); app.background.reset(); app.launcher_icon.reset(); app.game_logo.reset();
    GdiplusShutdown(app.gdiplus_token);
    return !report.good() ? 3 : passed && pixels_match ? 0 : 4;
}
