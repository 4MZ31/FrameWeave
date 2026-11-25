#include "veyra/engine/EngineController.h"
#include "veyra/Log.h"
#include "veyra/source/CaptureCardSource.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

int wmain(int argc, wchar_t** argv) {
    using namespace veyra::engine;
    using Clock = std::chrono::steady_clock;
    if (argc != 3 && argc != 7 && argc != 8 && argc != 9 && argc != 10 && argc != 12) return 2;
    const std::wstring backend = argc >= 7 ? argv[3] : L"dlss";
    const unsigned multiplier = argc >= 7 ? _wtoi(argv[4]) : 4;
    const bool native = argc >= 7 && std::wstring_view(argv[5]) == L"native";
    const bool resize = argc >= 7 && std::wstring_view(argv[6]) == L"resize";
    const std::wstring_view profile = argc >= 8 ? argv[7] : L"4k30";
    const std::wstring deviceName = argc >= 10 ? argv[9] : L"VC-007PRO";
    const double p95BudgetMs = argc == 12 ? _wtof(argv[10]) : 0;
    const int maxDrops = argc == 12 ? _wtoi(argv[11]) : 0;
    if (argc == 12 && (!(p95BudgetMs > 0) || maxDrops < 0)) return 2;
    const bool baseline = backend == L"baseline";
    const unsigned bufferMode = argc >= 9 ? _wtoi(argv[8]) : 0;
    if (bufferMode > 2) return 2;
    if (profile != L"4k30" && profile != L"1440p60" && profile != L"1080p60") return 2;
    const unsigned width = profile == L"1440p60" ? 2560 : profile == L"1080p60" ? 1920 : 3840;
    const unsigned height = profile == L"1440p60" ? 1440 : profile == L"1080p60" ? 1080 : 2160;
    const double fps = profile == L"4k30" ? 30.0 : 60.0;
    if (baseline ? multiplier != 1 : ((backend != L"dlss" && backend != L"xess") || (multiplier != 2 && multiplier != 4))) return 2;
    const int seconds = _wtoi(argv[2]);
    if (seconds < 3 || seconds > 120) return 2;
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS", GetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_TRACE",nullptr,0) ? L"1" : L"0");
    veyra::Logger::instance().openFile((dir / L"engine.log").wstring());
    veyra::Logger::instance().setConsoleEnabled(false);
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    const auto devices = veyra::source::CaptureCardSource::deviceDetails();
    const auto device = std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return d.name == deviceName; });
    if (device == devices.end()) { std::wcout << L"FAIL missing input " << deviceName << std::endl; return 3; }
    const auto formats = veyra::source::CaptureCardSource::formatsByPath(device->path);
    const auto format = std::find_if(formats.begin(), formats.end(), [&](const auto& f) {
        return f.width == width && f.height == height && std::abs(f.fps - fps) < .1 && f.label.find(L"NV12") != std::wstring::npos;
    });
    if (format == formats.end()) {
        std::cout << "FAIL requested NV12 format missing: " << width << 'x' << height << '@' << fps << std::endl;
        for (const auto& f : formats) std::wcout << f.index << L" " << f.label << std::endl;
        return 3;
    }
    const auto audios = veyra::source::CaptureCardSource::deviceDetails(true);
    const auto audio = std::find_if(audios.begin(), audios.end(), [](const auto& a) {
        return a.wasapi && a.name.find(L"HDMI (VC-007PRO)") != std::wstring::npos;
    });
    if (!baseline && audio == audios.end()) { std::cout << "FAIL identical WASAPI input unavailable" << std::endl; return 3; }
    const auto path = veyra::source::CaptureCardSource::makeCapturePath(unsigned(device - devices.begin()), *device,
        format->index, baseline ? veyra::source::kCaptureAudioDisabled : veyra::source::kCaptureAudioWasapi, baseline ? nullptr : &*audio);
    HWND window = CreateWindowExW(0, L"STATIC", L"Veyra historical capture comparison", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        40, 40, 1280, 760, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 2;
    MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    DEVMODEW dm{}; dm.dmSize = sizeof(dm); EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &dm);
    std::wcout << L"capture=" << deviceName << std::endl;
    std::cout << "input=" << width << 'x' << height << " fps=" << format->fps << " NV12 formatIndex=" << format->index
        <<  " audio=" << (baseline ? "off" : "WASAPI-muted") << " NR=" << (baseline ? "off" : native ? "native" : "realtime1080") << " FG=" << (baseline ? "off" : backend == L"xess" ? "XeSS" : "DLSS") << multiplier
        << " bufferMode=" << bufferMode << " resize=" << resize << " SR=off sync=default-off window=1280x760 display="
        << dm.dmPelsWidth << 'x' << dm.dmPelsHeight << '@' << dm.dmDisplayFrequency << std::endl;
    int failures = 0;
    auto check = [&](bool ok, const char* label) { std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl; failures += !ok; };
    auto pump = [] { MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } };
    {
        EngineController engine;
        engine.setVolume(0, true);
        EnhancementSettings settings; settings.nr = !baseline; settings.sr = false; settings.multiplier = multiplier;
        settings.captureBuffer = static_cast<veyra::source::CaptureBufferMode>(bufferMode);
        settings.frameGenerationBackend = backend == L"xess" ? FrameGenerationBackend::XeSS : FrameGenerationBackend::Dlss;
        if (native) settings.nrPolicy = veyra::pipeline::NrSizePolicy::Native;
        engine.open(window, path, PlayerOptions::from(settings));
        auto wait = [&](auto predicate, int ms) {
            const auto until = Clock::now() + std::chrono::milliseconds(ms);
            while (Clock::now() < until) {
                pump(); const auto s = engine.snapshot();
                if (s.failed) return false;
                if (predicate(s)) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return false;
        };
        const auto profileActive = [&](const auto& s) {
            return baseline ? !s.nrActive && !s.fgActive && !s.srActive :
                s.nrActive && s.fgActive && !s.srActive && s.applied.multiplier == multiplier && s.applied.frameGenerationBackend == settings.frameGenerationBackend;
        };
        const bool started = wait([&](const auto& s) { return s.frames > 10 && profileActive(s); }, 25000);
        check(started, "first frames with requested processing");
        const auto warm = Clock::now();
        const bool warmed = started && wait([&](const auto& s) {
            return Clock::now() - warm >= std::chrono::seconds(10) && profileActive(s);
        }, 20000);
        check(warmed, "ten second warmup");
        if (warmed) {
            std::ofstream csv(dir / L"snapshots.csv");
            csv << std::setprecision(10);
            csv << "wall,frames,received,dropped,generated,nr,fg,multiplier,limited,callbackFps,submitFps,ageMs,originalAgeP95,originalSamples,readAgeMs,realPresented,generatedPresented,fgSkipped,fgExpired,slotWaits,readyMeanMs,deadlineMeanMs,presentMeanMs,colorMeanMs,flowMeanMs,nrMeanMs,residualMeanMs,fgMeanMs,graphMeanMs\n";
            const auto first = engine.snapshot();
            auto last = first;
            veyra::log::info("capture-comparison", "measurement-start");
            const auto start = Clock::now();
            bool active = true;
            double worstP95 = -1;
            unsigned resizeStep = 0;
            while (Clock::now() - start < std::chrono::seconds(seconds) && !last.failed) {
                const unsigned step = unsigned(std::chrono::duration<double>(Clock::now() - start).count() / 10);
                if (resize && step > resizeStep) {
                    resizeStep = step;
                    SetWindowPos(window,nullptr,0,0,step % 2 ? 1800 : 1280,step % 2 ? 1000 : 760,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
                    std::cout << "resize step=" << step << std::endl;
                }
                pump(); last = engine.snapshot();
                const auto& flow = last.metrics.flow; const auto& c = flow.counters;
                active = active && profileActive(last);
                if (flow.latencySamples) worstP95 = std::max(worstP95, last.captureAgeP95Ms);
                auto cpu = [&](veyra::diagnostics::CpuStage stage) { return flow.cpuTiming[size_t(stage)].mean.value_or(-1); };
                auto gpu = [&](veyra::diagnostics::GpuStage stage) { return flow.gpuTiming[size_t(stage)].mean.value_or(-1); };
                using C = veyra::diagnostics::CpuStage; using G = veyra::diagnostics::GpuStage;
                csv << std::chrono::duration<double>(Clock::now() - start).count() << ',' << last.frames << ',' << last.captureReceived
                    << ',' << last.captureDropped << ',' << last.generated << ',' << last.nrActive << ',' << last.fgActive
                    << ',' << last.applied.multiplier << ',' << last.fgBudgetLimited << ',' << last.captureFps << ',' << last.submissionFps.value_or(0)
                    << ',' << last.captureAgeMs << ',' << last.captureAgeP95Ms << ',' << flow.latencySamples << ',' << last.captureReadAgeMs << ',' << c.realPresented
                    << ',' << c.generatedPresented << ',' << c.fgSkippedBeforeEval << ',' << c.generatedExpiredAfterEval << ',' << flow.slotReuseWaitCount
                    << ',' << cpu(C::ReadyWait) << ',' << cpu(C::DeadlineWait) << ',' << cpu(C::Present)
                    << ',' << gpu(G::Color) << ',' << gpu(G::Flow) << ',' << gpu(G::Nr) << ',' << gpu(G::Residual)
                    << ',' << gpu(G::FgBatch) << ',' << flow.enhancementProcessing.mean.value_or(-1) << '\n';
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            veyra::log::info("capture-comparison", "measurement-end");
            std::cout << "elapsed=" << elapsed << " receivedDelta=" << last.captureReceived - first.captureReceived
                << " droppedDelta=" << last.captureDropped - first.captureDropped << " nrDelta=" << last.nrEvaluated - first.nrEvaluated
                << " generatedDelta=" << last.generated - first.generated << " realPresentedDelta="
                << last.metrics.flow.counters.realPresented - first.metrics.flow.counters.realPresented << std::endl;
            check(!last.failed && elapsed >= seconds && last.frames > first.frames + 10, "full measurement");
            check(active && (baseline || (last.nrEvaluated > first.nrEvaluated && last.generated > first.generated)), "requested processing continuously active");
            check(worstP95 >= 0, "original presentation latency sampled");
            std::cout << "worstWindowP95Ms=" << worstP95 << std::endl;
            if (argc == 12) {
                check(worstP95 <= p95BudgetMs, "original p95 within supplied budget");
                check(last.captureDropped - first.captureDropped <= uint64_t(maxDrops), "drops within supplied budget");
            }
            const double measuredFps = double(last.captureReceived - first.captureReceived) / elapsed;
            std::cout << "measuredCallbackFps=" << measuredFps << std::endl;
            check(std::abs(measuredFps - fps) < fps * .05, "actual callback rate matches requested capture rate");
        }
        engine.stop(); check(wait([&](const auto&) { return engine.idle(); }, 15000), "clean stop");
    }
    DestroyWindow(window); CoUninitialize();
    return failures ? 1 : 0;
}
