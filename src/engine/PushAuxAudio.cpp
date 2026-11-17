// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS extra audio: desktop audio (process loopback without PushFPS) and an extra input
// (engine/PushAuxAudio.h).
#include "veyra/engine/PushAuxAudio.h"

#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include "veyra/sink/RecordingAudioTap.h"
#include "veyra/Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <format>
#include <mutex>
#include <thread>

namespace veyra::engine {
using Microsoft::WRL::ComPtr;
namespace {

constexpr size_t kBlock = 480;          // 10 ms at 48 kHz
constexpr int64_t kBlockUs = 10000;

std::mutex mutex;
std::condition_variable wake;
PushAuxSettings settings;
uint64_t settingsVersion = 0;
std::vector<std::shared_ptr<PushAuxSink>> sinks;
bool meters = false;
PushAuxStatus status;
bool workerRunning = false;   // the thread is detached; it clears this when it leaves
PushLevelMeter desktopMeter, inputMeter;

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(std::max(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

bool wanted() { return (settings.desktop || settings.input) && (!sinks.empty() || meters); }

WAVEFORMATEX floatStereo() {
    WAVEFORMATEX f{};
    f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    f.nChannels = 2; f.nSamplesPerSec = 48000; f.wBitsPerSample = 32;
    f.nBlockAlign = 8; f.nAvgBytesPerSec = 48000 * 8;
    return f;
}

// Completes ActivateAudioInterfaceAsync (agile: called on any thread).
class ActivateHandler : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                                            Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT result = E_FAIL;
    ComPtr<IAudioClient> client;
    ~ActivateHandler() override { if (done) CloseHandle(done); }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation) override {
        HRESULT activated = E_FAIL;
        ComPtr<IUnknown> unknown;
        const HRESULT hr = operation->GetActivateResult(&activated, &unknown);
        result = FAILED(hr) ? hr : activated;
        if (SUCCEEDED(result) && unknown) unknown.As(&client);
        if (SUCCEEDED(result) && !client) result = E_NOINTERFACE;
        SetEvent(done);
        return S_OK;
    }
};

struct Input {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    HANDLE event = nullptr;
    PushAuxFifo fifo;
    float gain = 1, target = 1;
    bool open = false;
    void close() {
        if (client) client->Stop();
        capture.Reset(); client.Reset();
        if (event) { CloseHandle(event); event = nullptr; }
        fifo.clear();
        open = false;
    }
    HRESULT start(IAudioClient* activated, bool loopback) {
        close();
        client = activated;
        WAVEFORMATEX format = floatStereo();
        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        if (loopback) flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
        HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &format, nullptr);
        if (FAILED(hr)) { close(); return hr; }
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event || FAILED(hr = client->SetEventHandle(event)) || FAILED(hr = client->GetService(IID_PPV_ARGS(&capture))) ||
            FAILED(hr = client->Start())) { close(); return FAILED(hr) ? hr : E_FAIL; }
        open = true;
        return S_OK;
    }
    // Everything the device has into the fifo.
    bool drain(std::vector<float>& scratch) {
        if (!open) return true;
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
            const HRESULT hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) return false;
            scratch.assign(size_t(frames) * 2, 0.0f);
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) std::memcpy(scratch.data(), data, size_t(frames) * 8);
            capture->ReleaseBuffer(frames);
            fifo.push(scratch.data(), frames);
        }
        return true;
    }
};

std::wstring describe(HRESULT hr, bool desktop) {
    if (hr == E_ACCESSDENIED) return desktop ? L"Windows did not allow capturing desktop audio" : L"Windows did not allow PushFPS to use this input (Settings > Privacy & security > Microphone)";
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == E_NOTFOUND || hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) return L"The input was not found; plug it back in or pick another";
    if (hr == AUDCLNT_E_DEVICE_IN_USE) return L"Another app uses this input in exclusive mode";
    return std::format(L"{} could not start (0x{:08X})", desktop ? L"Desktop audio" : L"The input", uint32_t(hr));
}

HRESULT openDesktop(Input& input) {
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT activation;
    PropVariantInit(&activation);
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(params);
    activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    auto handler = Microsoft::WRL::Make<ActivateHandler>();
    if (!handler || !handler->done) return E_OUTOFMEMORY;
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &activation, handler.Get(), &operation);
    if (FAILED(hr)) return hr;
    if (WaitForSingleObject(handler->done, 5000) != WAIT_OBJECT_0) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    if (FAILED(handler->result)) return handler->result;
    return input.start(handler->client.Get(), true);
}

HRESULT openInput(Input& input, const std::wstring& id, std::wstring& name) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return hr;
    ComPtr<IMMDevice> device;
    hr = id.empty() ? enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device) : enumerator->GetDevice(id.c_str(), &device);
    if (FAILED(hr)) return hr;
    name.clear();
    ComPtr<IPropertyStore> properties;
    static const PROPERTYKEY kFriendlyName{{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
        PROPVARIANT value; PropVariantInit(&value);
        if (SUCCEEDED(properties->GetValue(kFriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal) name = value.pwszVal;
        PropVariantClear(&value);
    }
    ComPtr<IAudioClient> client;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr)) return hr;
    return input.start(client.Get(), false);
}

void run() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadDescription(GetCurrentThread(), L"PushFPS extra audio");
    Input desktop, input;
    std::vector<float> scratch, block(kBlock * 2), part(kBlock * 2);
    uint64_t seenVersion = UINT64_MAX;
    PushAuxSettings current;
    auto lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    int64_t baseUs = 0;
    uint64_t produced = 0;
    for (;;) {
        std::vector<std::shared_ptr<PushAuxSink>> targets;
        {
            std::unique_lock lock(mutex);
            if (!wanted()) { workerRunning = false; break; }
            targets = sinks;
            if (settingsVersion != seenVersion) {
                const bool desktopChanged = settings.desktop != current.desktop;
                const bool inputChanged = settings.input != current.input || settings.inputId != current.inputId;
                current = settings; seenVersion = settingsVersion;
                if (desktopChanged && !current.desktop) { desktop.close(); status.desktopOpen = false; status.desktopError.clear(); }
                if (inputChanged) { input.close(); status.inputOpen = false; status.inputError.clear(); }
                lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            }
        }
        desktop.target = mixGain(current.desktopDb, current.desktopMuted);
        input.target = mixGain(current.inputDb, current.inputMuted);
        // (Re)open what is wanted and closed, at most every 3 seconds after a failure.
        const auto now = std::chrono::steady_clock::now();
        if (now - lastRetry > std::chrono::seconds(3) && ((current.desktop && !desktop.open) || (current.input && !input.open))) {
            lastRetry = now;
            if (current.desktop && !desktop.open) {
                const HRESULT hr = openDesktop(desktop);
                std::lock_guard lock(mutex);
                status.desktopOpen = SUCCEEDED(hr);
                status.desktopError = SUCCEEDED(hr) ? std::wstring{} : describe(hr, true);
                veyra::log::info("push-aux", std::format("desktop audio {} hr=0x{:08X}", SUCCEEDED(hr) ? "open" : "failed", uint32_t(hr)));
            }
            if (current.input && !input.open) {
                std::wstring name;
                const HRESULT hr = openInput(input, current.inputId, name);
                std::lock_guard lock(mutex);
                status.inputOpen = SUCCEEDED(hr);
                status.inputName = name;
                status.inputError = SUCCEEDED(hr) ? std::wstring{} : describe(hr, false);
                veyra::log::info("push-aux", std::format("extra input {} name={} hr=0x{:08X}", SUCCEEDED(hr) ? "open" : "failed", narrow(name), uint32_t(hr)));
            }
            if (desktop.open || input.open) { baseUs = sink::recordingClockUs(); produced = 0; }
        }
        HANDLE events[2]; DWORD count = 0;
        if (desktop.open) events[count++] = desktop.event;
        if (input.open) events[count++] = input.event;
        if (count) WaitForMultipleObjects(count, events, FALSE, 5);
        else std::this_thread::sleep_for(std::chrono::milliseconds(20));
        for (Input* each : {&desktop, &input})
            if (each->open && !each->drain(scratch)) {
                each->close();
                std::lock_guard lock(mutex);
                if (each == &desktop) { status.desktopOpen = false; status.desktopError = L"Desktop audio stopped"; }
                else { status.inputOpen = false; status.inputError = L"The input stopped (unplugged?)"; }
            }
        if (!desktop.open && !input.open) continue;
        // 10 ms blocks on the recording clock, 20 ms behind it (the fifos' prebuffer).
        const int64_t nowUs = sink::recordingClockUs();
        int64_t due = (nowUs - baseUs) / kBlockUs;
        if (due - int64_t(produced) > 20) produced = uint64_t(due - 2); // stalled: skip ahead, keep in time
        while (int64_t(produced) < due) {
            std::fill(block.begin(), block.end(), 0.0f);
            for (auto [each, meter] : {std::pair{&desktop, &desktopMeter}, std::pair{&input, &inputMeter}}) {
                if (!each->open) continue;
                std::fill(part.begin(), part.end(), 0.0f);
                // The fader glides across the block, so moving it never clicks.
                each->fifo.mixInto(part.data(), kBlock, 1.0f);
                for (size_t i = 0; i < kBlock; ++i) {
                    const float g = each->gain + (each->target - each->gain) * float(i + 1) / float(kBlock);
                    part[i * 2] *= g; part[i * 2 + 1] *= g;
                }
                each->gain = each->target;
                meter->feed(part.data(), kBlock, 2);
                for (size_t i = 0; i < block.size(); ++i) block[i] += part[i];
            }
            for (float& v : block) v = std::clamp(v, -1.0f, 1.0f);
            const int64_t stamp = baseUs + int64_t(produced) * kBlockUs - 20000;
            for (const auto& sink : targets) sink->auxPcm(block.data(), kBlock, stamp);
            ++produced;
        }
    }
    desktop.close(); input.close();
    {
        std::lock_guard lock(mutex);
        if (!workerRunning) status.desktopOpen = status.inputOpen = false;
    }
    if (SUCCEEDED(com)) CoUninitialize();
    veyra::log::info("push-aux", "extra audio stopped");
}

// Starts the thread when it is wanted (caller holds the mutex).
void startIfWanted(std::unique_lock<std::mutex>&) {
    if (!wanted() || workerRunning) return;
    workerRunning = true;
    std::thread(run).detach();
}

} // namespace

void setPushAux(const PushAuxSettings& wantedSettings) {
    std::unique_lock lock(mutex);
    PushAuxSettings next = wantedSettings;
    next.desktopDb = std::isfinite(next.desktopDb) ? std::clamp(next.desktopDb, kMixFloorDb, 12.0f) : 0.0f;
    next.inputDb = std::isfinite(next.inputDb) ? std::clamp(next.inputDb, kMixFloorDb, 12.0f) : 0.0f;
    if (next == settings) return;
    settings = next;
    ++settingsVersion;
    startIfWanted(lock);
}

PushAuxSettings pushAux() { std::lock_guard lock(mutex); return settings; }
bool pushAuxEnabled() { std::lock_guard lock(mutex); return settings.desktop || settings.input; }

void attachPushAux(std::shared_ptr<PushAuxSink> sink) {
    if (!sink) return;
    std::unique_lock lock(mutex);
    sinks.push_back(std::move(sink));
    startIfWanted(lock);
}

void detachPushAux(const PushAuxSink* sink) {
    std::lock_guard lock(mutex);
    std::erase_if(sinks, [&](const auto& s) { return s.get() == sink; });
}

void setPushAuxMeters(bool on) {
    std::unique_lock lock(mutex);
    meters = on;
    startIfWanted(lock);
}

PushAuxStatus pushAuxStatus() { std::lock_guard lock(mutex); return status; }
PushLevelMeter& pushDesktopMeter() { return desktopMeter; }
PushLevelMeter& pushInputMeter() { return inputMeter; }

} // namespace veyra::engine
