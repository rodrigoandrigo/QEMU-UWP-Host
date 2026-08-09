#include "pch.h"
#include "QemuDirectHost.h"
#include "QemuInputKeys.h"

#include <ppltasks.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <sstream>
#include <windows.storage.h>

using namespace Qemu_UWP_host;
using namespace Windows::ApplicationModel;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::System::Threading;
using namespace concurrency;

namespace
{
	uint32_t ExpandColorBits(uint32_t value, int bits)
	{
		if (bits <= 0)
		{
			return 0;
		}
		if (bits >= 8)
		{
			return value & 0xffu;
		}

		uint32_t maxValue = (1u << bits) - 1u;
		return (value * 255u + (maxValue / 2u)) / maxValue;
	}

	uint32_t PackBgra(uint32_t r, uint32_t g, uint32_t b)
	{
		return 0xff000000u | ((r & 0xffu) << 16) | ((g & 0xffu) << 8) | (b & 0xffu);
	}
}

QemuDirectHost::QemuDirectHost() :
	m_module(nullptr),
	m_qemuHostInit(nullptr),
	m_qemuHostStartWithArgs(nullptr),
	m_qemuHostRun(nullptr),
	m_qemuHostStart(nullptr),
	m_qemuHostMainLoopStep(nullptr),
	m_qemuHostWakeMainLoop(nullptr),
	m_qemuHostPause(nullptr),
	m_qemuHostResume(nullptr),
	m_qemuHostRequestShutdown(nullptr),
	m_qemuHostRequestStop(nullptr),
	m_qemuHostReset(nullptr),
	m_qemuHostJoin(nullptr),
	m_qemuHostCleanup(nullptr),
	m_qemuHostSendKeyNumber(nullptr),
	m_qemuHostSendKeyQcode(nullptr),
	m_qemuHostSendPointerRel(nullptr),
	m_qemuHostSendPointerAbs(nullptr),
	m_qemuHostSendPointerAbsNormalized(nullptr),
	m_qemuHostSendPointerButton(nullptr),
	m_qemuHostCapturePointerAbs(nullptr),
	m_qemuHostSetLogCallback(nullptr),
	m_qemuHostSetVideoCallback(nullptr),
	m_qemuHostSetAudioCallback(nullptr),
	m_qemuHostRegisterLogCallback(nullptr),
	m_qemuHostRegisterVideoCallback(nullptr),
	m_qemuHostRegisterVideoUpdateCallback(nullptr),
	m_qemuHostRegisterAudioCallback(nullptr),
	m_qemuHostSetInputCallback(nullptr),
	m_qemuHostIsInitialized(nullptr),
	m_qemuHostIsRunning(nullptr),
	m_qemuHostPointerIsAbsolute(nullptr),
	m_qemuHostGetApiVersion(nullptr),
	m_hostApiVersion(0),
	m_usesLegacyHostApi(false),
	m_worker(nullptr),
	m_loadedDllName(),
	m_frameWidth(0),
	m_frameHeight(0),
	m_videoFrameCount(0),
	m_dirtyX(0),
	m_dirtyY(0),
	m_dirtyWidth(0),
	m_dirtyHeight(0),
	m_firstVideoFrameTick(0),
	m_frameValid(false),
	m_frameDirty(false),
	m_mouseLeft(false),
	m_mouseRight(false),
	m_mouseMiddle(false),
	m_pointerWidth(1),
	m_pointerHeight(1),
	m_loopStartTick(0),
	m_initializedOnce(false),
	m_initialized(false),
	m_deferredInit(false),
	m_running(false),
	m_pendingPause(false),
	m_pendingResume(false),
	m_pendingReset(false),
	m_pendingShutdown(false),
	m_pendingStop(false)
{
	m_logPath = std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\qemu-direct-host.log";
	m_stderrPath = std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\qemu-direct-stderr.log";
	FILE* stream = nullptr;
	_wfreopen_s(&stream, m_stderrPath.c_str(), L"w", stderr);
	_wfreopen_s(&stream, m_stderrPath.c_str(), L"a", stdout);
	setvbuf(stderr, nullptr, _IONBF, 0);
	setvbuf(stdout, nullptr, _IONBF, 0);
	SetStatus(L"Select supported QEMU media or a .qemu_cmd_line file.");
}

QemuDirectHost::~QemuDirectHost()
{
	Stop();
	if (m_module != nullptr)
	{
		FreeLibrary(m_module);
		m_module = nullptr;
	}
}

bool QemuDirectHost::LoadGame(StorageFile^ commandFile, std::wstring* error)
{
	if (m_initialized || m_running)
	{
		if (error)
		{
			*error = L"QEMU is already initialized in this app session. Reset the current VM or restart the app before loading another command.";
		}
		Trace(error ? *error : L"DirectHost: refusing to reinitialize QEMU.");
		return false;
	}
	Stop();
	if (commandFile == nullptr)
	{
		if (error)
		{
			*error = L"No qemu_cmd_line file was provided.";
		}
		return false;
	}

	std::wstring commandLine;
	try
	{
		commandLine = create_task(FileIO::ReadTextAsync(commandFile)).get()->Data();
	}
	catch (Platform::Exception^ ex)
	{
		if (error)
		{
			*error = L"Failed to read qemu_cmd_line: ";
			*error += ex != nullptr && ex->Message != nullptr ? ex->Message->Data() : L"unknown UWP error";
		}
		return false;
	}

	std::vector<std::wstring> parts = SplitCommandLine(commandLine);
	if (parts.empty())
	{
		if (error)
		{
			*error = L"qemu_cmd_line is empty.";
		}
		return false;
	}

	std::wstring dllName = QemuDllNameFromCommand(parts);
	Trace(L"DirectHost: loading command file " + std::wstring(commandFile->Path->Data()));
	if (CommandUsesHostDisplay(parts) &&
		!PackagedDllContainsAsciiString(dllName, "host-display") &&
		!PackagedDllContainsAsciiString(dllName, "DisplayHost"))
	{
		if (error)
		{
			*error = L"The selected QEMU DLL does not expose the host-display backend required by -display host. "
				L"This QEMU 11 DLL has the host embedding API, but it was built without display=host support; "
				L"rebuild the DLL with the host display backend or use a profile/display mode supported by this target.";
		}
		Trace(error ? *error : L"DirectHost: selected DLL does not support -display host.");
		return false;
	}
	if (!LoadQemuDll(dllName, error))
	{
		return false;
	}

	m_arguments.clear();
	m_argv.clear();
	{
		std::lock_guard<std::mutex> lock(m_controlMutex);
		m_pendingPause = false;
		m_pendingResume = false;
		m_pendingReset = false;
		m_pendingShutdown = false;
		m_pendingStop = false;
	}
	{
		std::lock_guard<std::mutex> frameLock(m_frameMutex);
		m_framePixels.clear();
		m_frameWidth = 0;
		m_frameHeight = 0;
		m_videoFrameCount = 0;
		m_dirtyX = 0;
		m_dirtyY = 0;
		m_dirtyWidth = 0;
		m_dirtyHeight = 0;
		m_firstVideoFrameTick = 0;
		m_frameValid = false;
		m_frameDirty = false;
	}
	ClearInput();
	for (const std::wstring& part : parts)
	{
		m_arguments.push_back(Narrow(part));
	}
	for (std::string& part : m_arguments)
	{
		m_argv.push_back(part.empty() ? const_cast<char*>("") : &part[0]);
	}
	for (size_t index = 0; index < m_argv.size(); index++)
	{
		Trace(L"DirectHost: argv[" + std::to_wstring(index) + L"] = " + Widen(m_argv[index]));
	}

	if (m_qemuHostRegisterLogCallback != nullptr)
	{
		m_qemuHostRegisterLogCallback(&QemuDirectHost::LogCallbackV2, this);
	}
	else if (m_qemuHostSetLogCallback != nullptr)
	{
		m_qemuHostSetLogCallback(&QemuDirectHost::LogCallback, this);
	}
	if (m_qemuHostRegisterVideoUpdateCallback != nullptr)
	{
		m_qemuHostRegisterVideoUpdateCallback(&QemuDirectHost::VideoUpdateCallbackV3, this);
	}
	else if (m_qemuHostRegisterVideoCallback != nullptr)
	{
		m_qemuHostRegisterVideoCallback(&QemuDirectHost::VideoCallbackV2, this);
	}
	else
	{
		m_qemuHostSetVideoCallback(&QemuDirectHost::VideoCallback, this);
	}
	if (m_qemuHostRegisterAudioCallback != nullptr)
	{
		m_qemuHostRegisterAudioCallback(&QemuDirectHost::AudioCallbackV2, this);
	}
	else if (m_qemuHostSetAudioCallback != nullptr)
	{
		m_qemuHostSetAudioCallback(&QemuDirectHost::AudioCallback, this);
	}
	if (m_qemuHostSetInputCallback != nullptr)
	{
		m_qemuHostSetInputCallback(&QemuDirectHost::InputCallback, this);
	}

	m_deferredInit = true;
	if (m_qemuHostStartWithArgs != nullptr)
	{
		Trace(L"DirectHost: qemu_host_start_with_args is available but not used; app worker uses qemu_host_init + qemu_host_main_loop_step so input stays on the QEMU step thread.");
	}
	else
	{
		Trace(L"DirectHost: qemu_host_start_with_args not found; app worker will use qemu_host_init + qemu_host_main_loop_step.");
	}

	m_initialized = true;
	m_initializedOnce = true;
	SetStatus(dllName + L" loaded. Ready to start QEMU.");
	return true;
}

void QemuDirectHost::RunLoadedGame()
{
	if (!m_initialized || m_running)
	{
		return;
	}

	m_running = true;
	if (m_deferredInit)
	{
		Trace(L"DirectHost: calling qemu_host_init on app worker thread.");
		int result = m_qemuHostInit(static_cast<int>(m_argv.size()), reinterpret_cast<const char* const*>(m_argv.data()));
		Trace(L"DirectHost: qemu_host_init returned " + std::to_wstring(result) + L".");
		m_deferredInit = false;
		if (result != 0)
		{
			m_running = false;
			m_initialized = false;
			SetStatus(L"qemu_host_init failed with code " + std::to_wstring(result) + L".");
			return;
		}
	}

	if (m_qemuHostMainLoopStep != nullptr)
	{
		RunQemu();
	}
}

void QemuDirectHost::Stop()
{
	bool wasRunning = m_running;
	if (m_initialized)
	{
		{
			std::lock_guard<std::mutex> lock(m_controlMutex);
		m_pendingStop = true;
		}
		WakeMainLoop();
		Trace(L"DirectHost: QEMU stop queued.");
		SetStatus(L"QEMU stop requested.");
	}
	else
	{
		SetStatus(L"Stop ignored: QEMU is not initialized.");
	}
	if (m_worker != nullptr)
	{
		try
		{
			create_task(m_worker).wait();
		}
		catch (...)
		{
			Trace(L"DirectHost: QEMU worker wait failed during stop.");
		}
		m_worker = nullptr;
	}
	if (m_initialized && m_qemuHostCleanup != nullptr && !wasRunning)
	{
		Trace(L"DirectHost: skipping qemu_host_cleanup during stop; current DLL cleanup path is not UWP-safe.");
		m_initialized = false;
	}
}

bool QemuDirectHost::Pause(std::wstring* error)
{
	if (!m_initialized || !m_running)
	{
		if (error)
		{
			*error = L"Pause ignored: QEMU is not running.";
		}
		return false;
	}
	if (m_qemuHostPause == nullptr)
	{
		if (error)
		{
			*error = L"Pause is not available: the selected qemu-system DLL does not export qemu_host_pause.";
		}
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_controlMutex);
		m_pendingPause = true;
		m_pendingResume = false;
	}
	WakeMainLoop();
	Trace(L"DirectHost: QEMU pause queued.");
	SetStatus(L"QEMU pause requested.");
	return true;
}

bool QemuDirectHost::Resume(std::wstring* error)
{
	if (!m_initialized || !m_running)
	{
		if (error)
		{
			*error = L"Resume ignored: QEMU is not running.";
		}
		return false;
	}
	if (m_qemuHostResume == nullptr)
	{
		if (error)
		{
			*error = L"Resume is not available: the selected qemu-system DLL does not export qemu_host_resume.";
		}
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_controlMutex);
		m_pendingResume = true;
		m_pendingPause = false;
	}
	WakeMainLoop();
	Trace(L"DirectHost: QEMU resume queued.");
	SetStatus(L"QEMU resume requested.");
	return true;
}

bool QemuDirectHost::RequestShutdown(std::wstring* error)
{
	if (!m_initialized)
	{
		if (error)
		{
			*error = L"Shutdown ignored: QEMU is not initialized.";
		}
		return false;
	}
	if (m_qemuHostRequestShutdown == nullptr)
	{
		if (error)
		{
			*error = L"Shutdown is not available: missing qemu_host_request_shutdown export.";
		}
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_controlMutex);
		m_pendingShutdown = true;
	}
	WakeMainLoop();
	Trace(L"DirectHost: QEMU shutdown queued.");
	SetStatus(L"QEMU shutdown requested.");
	return true;
}

void QemuDirectHost::Reset()
{
	if (m_initialized && m_qemuHostReset != nullptr)
	{
		{
			std::lock_guard<std::mutex> lock(m_controlMutex);
			m_pendingReset = true;
		}
		WakeMainLoop();
		Trace(L"DirectHost: QEMU reset queued.");
	}
}

void QemuDirectHost::SetProgressCallback(std::function<void(const std::wstring&)> callback)
{
	std::lock_guard<std::mutex> lock(m_progressMutex);
	m_progressCallback = callback;
}

std::wstring QemuDirectHost::StatusText() const
{
	std::lock_guard<std::mutex> lock(m_statusMutex);
	return m_status;
}

bool QemuDirectHost::HasVideoFrame() const
{
	std::lock_guard<std::mutex> lock(m_frameMutex);
	return m_videoFrameCount > 0;
}

unsigned QemuDirectHost::VideoFrameCount() const
{
	std::lock_guard<std::mutex> lock(m_frameMutex);
	return m_videoFrameCount;
}

uint64_t QemuDirectHost::FirstVideoFrameTick() const
{
	std::lock_guard<std::mutex> lock(m_frameMutex);
	return m_firstVideoFrameTick;
}

QemuHostFrameSnapshot QemuDirectHost::CopyFrame(bool forcePixels)
{
	std::lock_guard<std::mutex> lock(m_frameMutex);
	QemuHostFrameSnapshot frame = {};
	frame.width = m_frameWidth;
	frame.height = m_frameHeight;
	frame.frameNumber = m_videoFrameCount;
	frame.valid = m_frameValid;
	frame.dirty = m_frameDirty || forcePixels;
	if (frame.valid && frame.dirty)
	{
		frame.dirtyX = forcePixels ? 0 : m_dirtyX;
		frame.dirtyY = forcePixels ? 0 : m_dirtyY;
		frame.dirtyWidth = forcePixels ? m_frameWidth : m_dirtyWidth;
		frame.dirtyHeight = forcePixels ? m_frameHeight : m_dirtyHeight;
		if (frame.dirtyWidth > 0 && frame.dirtyHeight > 0)
		{
			frame.pixels.resize(static_cast<size_t>(frame.dirtyWidth) * frame.dirtyHeight);
			for (unsigned row = 0; row < frame.dirtyHeight; row++)
			{
				const uint32_t* source = m_framePixels.data() +
					static_cast<size_t>(frame.dirtyY + row) * m_frameWidth + frame.dirtyX;
				uint32_t* target = frame.pixels.data() + static_cast<size_t>(row) * frame.dirtyWidth;
				std::copy_n(source, frame.dirtyWidth, target);
			}
		}
		m_frameDirty = false;
		m_dirtyX = 0;
		m_dirtyY = 0;
		m_dirtyWidth = 0;
		m_dirtyHeight = 0;
	}
	return frame;
}

void QemuDirectHost::SetKey(unsigned key, bool down)
{
	if (!m_initialized || m_qemuHostSendKeyNumber == nullptr)
	{
		return;
	}

	int keyNumber = MapHostKeyNumber(key);
	if (keyNumber != 0)
	{
		std::lock_guard<std::mutex> lock(m_inputMutex);
		if (m_pendingInputEvents.size() < 2048)
		{
			m_pendingInputEvents.push_back({ PendingInputType::KeyNumber, keyNumber, 0, 0, 0, down });
		}
	}
	WakeMainLoop();
}

void QemuDirectHost::SetPointer(float x, float y, float pointerWidth, float pointerHeight, int deltaX, int deltaY, bool left, bool right, bool middle)
{
	if (!m_initialized ||
		(m_qemuHostSendPointerRel == nullptr &&
		 m_qemuHostSendPointerAbs == nullptr &&
		 m_qemuHostSendPointerAbsNormalized == nullptr &&
		 m_qemuHostSendPointerButton == nullptr))
	{
		return;
	}

	int width = (std::max)(1, static_cast<int>(pointerWidth + 0.5f));
	int height = (std::max)(1, static_cast<int>(pointerHeight + 0.5f));
	int ix = static_cast<int>((std::max)(0.0f, (std::min)(static_cast<float>(width), x)));
	int iy = static_cast<int>((std::max)(0.0f, (std::min)(static_cast<float>(height), y)));

	std::lock_guard<std::mutex> lock(m_inputMutex);
	if (m_pendingInputEvents.size() >= 2048)
	{
		return;
	}
	auto queuePointerMove = [this](PendingInputEvent event)
	{
		for (auto it = m_pendingInputEvents.rbegin(); it != m_pendingInputEvents.rend(); ++it)
		{
			if (it->type == PendingInputType::KeyNumber || it->type == PendingInputType::PointerButton)
			{
				break;
			}
			if (it->type == event.type)
			{
				if (event.type == PendingInputType::PointerRel)
				{
					it->a += event.a;
					it->b += event.b;
				}
				else
				{
					*it = event;
				}
				return;
			}
		}
		if (m_pendingInputEvents.size() < 2048)
		{
			m_pendingInputEvents.push_back(event);
		}
	};
	if (m_qemuHostSendPointerRel != nullptr && (deltaX != 0 || deltaY != 0))
	{
		queuePointerMove({ PendingInputType::PointerRel, deltaX, deltaY, 0, 0, false });
	}
	if (m_qemuHostSendPointerAbs != nullptr || m_qemuHostSendPointerAbsNormalized != nullptr)
	{
		queuePointerMove({ PendingInputType::PointerAbs, ix, iy, width, height, false });
	}
	if (m_qemuHostSendPointerButton != nullptr)
	{
		auto queuePointerButton = [this](int button, bool down)
		{
			if (m_pendingInputEvents.size() < 2048)
			{
				m_pendingInputEvents.push_back({ PendingInputType::PointerButton, button, 0, 0, 0, down });
			}
		};
		if (left != m_mouseLeft)
		{
			queuePointerButton(0, left);
			m_mouseLeft = left;
		}
		if (middle != m_mouseMiddle)
		{
			queuePointerButton(1, middle);
			m_mouseMiddle = middle;
		}
		if (right != m_mouseRight)
		{
			queuePointerButton(2, right);
			m_mouseRight = right;
		}
	}
	WakeMainLoop();
}

void QemuDirectHost::ClearPointer()
{
	std::lock_guard<std::mutex> lock(m_inputMutex);
	if (m_qemuHostSendPointerButton != nullptr)
	{
		if (m_mouseLeft && m_pendingInputEvents.size() < 2048)
		{
			m_pendingInputEvents.push_back({ PendingInputType::PointerButton, 0, 0, 0, 0, false });
		}
		if (m_mouseMiddle && m_pendingInputEvents.size() < 2048)
		{
			m_pendingInputEvents.push_back({ PendingInputType::PointerButton, 1, 0, 0, 0, false });
		}
		if (m_mouseRight && m_pendingInputEvents.size() < 2048)
		{
			m_pendingInputEvents.push_back({ PendingInputType::PointerButton, 2, 0, 0, 0, false });
		}
	}
	m_mouseLeft = false;
	m_mouseMiddle = false;
	m_mouseRight = false;
	WakeMainLoop();
}

void QemuDirectHost::ClearInput()
{
	{
		std::lock_guard<std::mutex> lock(m_inputMutex);
		m_pendingInputEvents.clear();
	}
	ClearPointer();
}

bool QemuDirectHost::LoadQemuDll(const std::wstring& dllName, std::wstring* error)
{
	if (m_module != nullptr)
	{
		if (_wcsicmp(m_loadedDllName.c_str(), dllName.c_str()) != 0)
		{
			if (error)
			{
				*error = L"Cannot switch QEMU target in the same app session. Loaded ";
				*error += m_loadedDllName;
				*error += L", requested ";
				*error += dllName;
				*error += L". Restart the app before switching targets.";
			}
			Trace(error ? *error : L"DirectHost: refusing to switch loaded QEMU DLL.");
			return false;
		}
		return m_qemuHostInit != nullptr;
	}

	Trace(L"DirectHost: LoadPackagedLibrary(" + dllName + L").");
	m_module = LoadPackagedLibrary(dllName.c_str(), 0);
	if (m_module == nullptr)
	{
		DWORD code = GetLastError();
		if (error)
		{
			std::wstringstream stream;
			stream << L"LoadPackagedLibrary(" << dllName << L") failed with Win32 error " << code
				<< L". The DLL and all of its non-system dependencies must be packaged and UWP-compatible.";
			*error = stream.str();
		}
		return false;
	}
	m_loadedDllName = dllName;
	m_qemuHostGetApiVersion = reinterpret_cast<qemu_host_get_api_version_t>(GetProcAddress(m_module, "qemu_host_get_api_version"));
	if (m_qemuHostGetApiVersion != nullptr)
	{
		m_hostApiVersion = m_qemuHostGetApiVersion();
		const unsigned int major = m_hostApiVersion >> 16;
		const unsigned int minor = m_hostApiVersion & 0xffffu;
		if (major != 1)
		{
			if (error)
			{
				*error = L"Unsupported QEMU host API " + std::to_wstring(major) + L"." + std::to_wstring(minor) +
					L". This application requires host API major version 1.";
			}
			Trace(error ? *error : L"DirectHost: incompatible QEMU host API major version.");
			FreeLibrary(m_module);
			m_module = nullptr;
			m_loadedDllName.clear();
			return false;
		}
		Trace(L"DirectHost: QEMU host API " + std::to_wstring(major) + L"." + std::to_wstring(minor) + L" accepted.");
	}
	else
	{
		m_usesLegacyHostApi = true;
		Trace(L"DirectHost: legacy unversioned host API detected; compatibility is inferred from required exports.");
	}

	m_qemuHostStartWithArgs = reinterpret_cast<qemu_host_start_with_args_t>(GetProcAddress(m_module, "qemu_host_start_with_args"));
	if (m_qemuHostStartWithArgs != nullptr)
	{
		Trace(L"DirectHost: found qemu_host_start_with_args export.");
	}
	else
	{
		Trace(L"DirectHost: qemu_host_start_with_args export not found.");
	}
	m_qemuHostPause = reinterpret_cast<qemu_host_pause_t>(GetProcAddress(m_module, "qemu_host_pause"));
	if (m_qemuHostPause != nullptr)
	{
		Trace(L"DirectHost: found qemu_host_pause export.");
	}
	else
	{
		Trace(L"DirectHost: qemu_host_pause export not found.");
	}
	m_qemuHostResume = reinterpret_cast<qemu_host_resume_t>(GetProcAddress(m_module, "qemu_host_resume"));
	if (m_qemuHostResume != nullptr)
	{
		Trace(L"DirectHost: found qemu_host_resume export.");
	}
	else
	{
		Trace(L"DirectHost: qemu_host_resume export not found.");
	}
	m_qemuHostRequestStop = reinterpret_cast<qemu_host_request_stop_t>(GetProcAddress(m_module, "qemu_host_request_stop"));
	if (m_qemuHostRequestStop != nullptr)
	{
		Trace(L"DirectHost: found force-stop lifecycle export.");
	}
	else
	{
		Trace(L"DirectHost: force-stop export not found; Stop will use shutdown fallback.");
	}
	m_qemuHostSendPointerAbsNormalized = reinterpret_cast<qemu_host_send_pointer_abs_normalized_t>(GetProcAddress(m_module, "qemu_host_send_pointer_abs_normalized"));
	if (m_qemuHostSendPointerAbsNormalized != nullptr)
	{
		Trace(L"DirectHost: found qemu_host_send_pointer_abs_normalized export.");
	}
	else
	{
		Trace(L"DirectHost: qemu_host_send_pointer_abs_normalized export not found.");
	}
	m_qemuHostPointerIsAbsolute = reinterpret_cast<qemu_host_pointer_is_absolute_t>(GetProcAddress(m_module, "qemu_host_pointer_is_absolute"));
	if (m_qemuHostPointerIsAbsolute != nullptr)
	{
		Trace(L"DirectHost: found qemu_host_pointer_is_absolute export.");
	}
	else
	{
		Trace(L"DirectHost: qemu_host_pointer_is_absolute export not found.");
	}
	m_qemuHostCapturePointerAbs = reinterpret_cast<qemu_host_capture_pointer_abs_t>(GetProcAddress(m_module, "qemu_host_capture_pointer_abs"));
	if (m_qemuHostCapturePointerAbs != nullptr)
	{
		Trace(L"DirectHost: found qemu_host_capture_pointer_abs export.");
	}
	else
	{
		Trace(L"DirectHost: qemu_host_capture_pointer_abs export not found.");
	}
	m_qemuHostSendPointerRel = reinterpret_cast<qemu_host_send_pointer_rel_t>(GetProcAddress(m_module, "qemu_host_send_pointer_rel"));
	if (m_qemuHostSendPointerRel != nullptr)
	{
		Trace(L"DirectHost: found qemu_host_send_pointer_rel export.");
	}
	else
	{
		Trace(L"DirectHost: qemu_host_send_pointer_rel export not found.");
	}

	m_qemuHostRun = reinterpret_cast<qemu_host_run_t>(GetProcAddress(m_module, "qemu_host_run"));
	if (m_qemuHostRun != nullptr)
	{
		Trace(L"DirectHost: found optional qemu_host_run export.");
	}
	else
	{
		Trace(L"DirectHost: optional qemu_host_run export not found.");
	}

	m_qemuHostRegisterLogCallback = reinterpret_cast<qemu_host_register_log_callback_t>(GetProcAddress(m_module, "qemu_host_register_log_callback"));
	if (m_qemuHostRegisterLogCallback != nullptr)
	{
		Trace(L"DirectHost: found QEMU v2 log callback registration export.");
	}
	else
	{
		m_qemuHostSetLogCallback = reinterpret_cast<qemu_host_set_log_callback_t>(GetProcAddress(m_module, "qemu_host_set_log_callback"));
		if (m_qemuHostSetLogCallback != nullptr)
		{
			Trace(L"DirectHost: found legacy QEMU log callback registration export.");
		}
		else
		{
			Trace(L"DirectHost: QEMU log callback registration export not found.");
		}
	}

	m_qemuHostRegisterVideoUpdateCallback = reinterpret_cast<qemu_host_register_video_update_callback_t>(
		GetProcAddress(m_module, "qemu_host_register_video_update_callback"));
	if (m_qemuHostRegisterVideoUpdateCallback != nullptr)
	{
		Trace(L"DirectHost: found dirty-rectangle video callback registration export.");
	}
	else
	{
		m_qemuHostRegisterVideoCallback = reinterpret_cast<qemu_host_register_video_callback_t>(GetProcAddress(m_module, "qemu_host_register_video_callback"));
		if (m_qemuHostRegisterVideoCallback != nullptr)
		{
			Trace(L"DirectHost: found QEMU v2 video callback registration export.");
		}
		else
		{
			m_qemuHostSetVideoCallback = reinterpret_cast<qemu_host_set_video_callback_t>(GetProcAddress(m_module, "qemu_host_set_video_callback"));
			if (m_qemuHostSetVideoCallback != nullptr)
			{
				Trace(L"DirectHost: found legacy QEMU video callback registration export.");
			}
			else
			{
				if (error)
				{
					*error = L"Missing QEMU host video callback registration export.";
				}
				return false;
			}
		}
	}

	m_qemuHostRegisterAudioCallback = reinterpret_cast<qemu_host_register_audio_callback_t>(GetProcAddress(m_module, "qemu_host_register_audio_callback"));
	if (m_qemuHostRegisterAudioCallback != nullptr)
	{
		Trace(L"DirectHost: found QEMU v2 audio callback registration export.");
	}
	else
	{
		m_qemuHostSetAudioCallback = reinterpret_cast<qemu_host_set_audio_callback_t>(GetProcAddress(m_module, "qemu_host_set_audio_callback"));
		if (m_qemuHostSetAudioCallback != nullptr)
		{
			Trace(L"DirectHost: found legacy QEMU audio callback registration export.");
		}
		else
		{
			Trace(L"DirectHost: QEMU audio callback registration export not found.");
		}
	}

	m_qemuHostSetInputCallback = reinterpret_cast<qemu_host_set_input_callback_t>(GetProcAddress(m_module, "qemu_host_set_input_callback"));
	if (m_qemuHostSetInputCallback != nullptr)
	{
		Trace(L"DirectHost: found optional qemu_host_set_input_callback export.");
	}
	else
	{
		Trace(L"DirectHost: optional qemu_host_set_input_callback export not found.");
	}

	m_qemuHostWakeMainLoop = reinterpret_cast<qemu_host_wake_main_loop_t>(GetProcAddress(m_module, "qemu_host_wake_main_loop"));
	if (m_qemuHostWakeMainLoop != nullptr)
	{
		Trace(L"DirectHost: blocking main-loop wake export found; busy polling disabled.");
	}
	else
	{
		Trace(L"DirectHost: main-loop wake export not found; using legacy nonblocking loop.");
	}
	return Resolve("qemu_host_init", m_qemuHostInit, error) &&
		Resolve("qemu_host_start", m_qemuHostStart, error) &&
		Resolve("qemu_host_main_loop_step", m_qemuHostMainLoopStep, error) &&
		Resolve("qemu_host_request_shutdown", m_qemuHostRequestShutdown, error) &&
		Resolve("qemu_host_reset", m_qemuHostReset, error) &&
		Resolve("qemu_host_join", m_qemuHostJoin, error) &&
		Resolve("qemu_host_cleanup", m_qemuHostCleanup, error) &&
		Resolve("qemu_host_send_key_number", m_qemuHostSendKeyNumber, error) &&
		Resolve("qemu_host_send_key_qcode", m_qemuHostSendKeyQcode, error) &&
		Resolve("qemu_host_send_pointer_abs", m_qemuHostSendPointerAbs, error) &&
		Resolve("qemu_host_send_pointer_button", m_qemuHostSendPointerButton, error) &&
		Resolve("qemu_host_is_initialized", m_qemuHostIsInitialized, error) &&
		Resolve("qemu_host_is_running", m_qemuHostIsRunning, error);
}

std::wstring QemuDirectHost::ApiCompatibilityText() const
{
	if (m_usesLegacyHostApi)
	{
		return L"QEMU host API: legacy/unversioned (validated by required exports).";
	}
	return L"QEMU host API: " + std::to_wstring(m_hostApiVersion >> 16) + L"." + std::to_wstring(m_hostApiVersion & 0xffffu) + L".";
}

template <typename T>
bool QemuDirectHost::Resolve(const char* name, T& target, std::wstring* error)
{
	FARPROC proc = GetProcAddress(m_module, name);
	if (proc == nullptr)
	{
		if (error)
		{
			*error = L"Missing QEMU host export: " + Widen(name);
		}
		return false;
	}

	target = reinterpret_cast<T>(proc);
	return true;
}

void QemuDirectHost::RunQemu()
{
	const bool blockingLoop = m_qemuHostWakeMainLoop != nullptr;
	Trace(blockingLoop ? L"DirectHost: entering blocking qemu_host_main_loop_step loop."
		: L"DirectHost: entering legacy nonblocking qemu_host_main_loop_step loop.");
	m_loopStartTick = GetTickCount64();
	int result = 0;
	int status = 0;
	unsigned int stepCount = 0;
	while (m_running)
	{
		ProcessPendingControl();
		ProcessPendingInput();
		result = m_qemuHostMainLoopStep(!blockingLoop, &status);
		stepCount++;
		if (result < 0)
		{
			Trace(L"DirectHost: qemu_host_main_loop_step failed with code " + std::to_wstring(result));
			break;
		}
		if (result > 0)
		{
			Trace(L"DirectHost: qemu_host_main_loop_step completed with status " + std::to_wstring(status));
			break;
		}
		if (!blockingLoop && (stepCount & 0xff) == 0)
		{
			Sleep(0);
		}
		if (!blockingLoop && (stepCount & 0xfff) == 0)
		{
			Sleep(1);
		}
		if ((stepCount & 0xfffff) == 0)
		{
			Trace(L"DirectHost: qemu_host_main_loop_step heartbeat, steps=" + std::to_wstring(stepCount) +
				L", elapsed_ms=" + std::to_wstring(GetTickCount64() - m_loopStartTick));
		}
	}
	if (m_initialized && m_qemuHostCleanup != nullptr)
	{
		Trace(L"DirectHost: skipping qemu_host_cleanup after run; current DLL cleanup path is not UWP-safe.");
		m_initialized = false;
	}
	m_running = false;
	SetStatus(L"QEMU stopped with code " + std::to_wstring(result) + L", status=" + std::to_wstring(status));
}

void QemuDirectHost::WakeMainLoop()
{
	if (m_qemuHostWakeMainLoop != nullptr && m_running)
	{
		m_qemuHostWakeMainLoop();
	}
}

void QemuDirectHost::ProcessPendingControl()
{
	bool pause = false;
	bool resume = false;
	bool reset = false;
	bool shutdown = false;
	bool stop = false;
	{
		std::lock_guard<std::mutex> lock(m_controlMutex);
		pause = m_pendingPause;
		resume = m_pendingResume;
		reset = m_pendingReset;
		shutdown = m_pendingShutdown;
		stop = m_pendingStop;
		m_pendingPause = false;
		m_pendingResume = false;
		m_pendingReset = false;
		m_pendingShutdown = false;
		m_pendingStop = false;
	}

	if (pause && m_initialized && m_qemuHostPause != nullptr)
	{
		Trace(L"DirectHost: processing queued QEMU pause.");
		int result = m_qemuHostPause();
		Trace(L"DirectHost: qemu_host_pause returned " + std::to_wstring(result) + L".");
		if (result == 0)
		{
			SetStatus(L"QEMU paused.");
		}
		else
		{
			SetStatus(L"qemu_host_pause failed with code " + std::to_wstring(result) + L".");
		}
	}

	if (resume && m_initialized && m_qemuHostResume != nullptr)
	{
		Trace(L"DirectHost: processing queued QEMU resume.");
		int result = m_qemuHostResume();
		Trace(L"DirectHost: qemu_host_resume returned " + std::to_wstring(result) + L".");
		if (result == 0)
		{
			SetStatus(L"QEMU resumed.");
		}
		else
		{
			SetStatus(L"qemu_host_resume failed with code " + std::to_wstring(result) + L".");
		}
	}
	if (reset && m_initialized && m_qemuHostReset != nullptr)
	{
		int result = m_qemuHostReset();
		if (result != 0)
		{
			Trace(L"DirectHost: qemu_host_reset failed with code " + std::to_wstring(result));
		}
	}

	if (stop && m_initialized)
	{
		Trace(L"DirectHost: processing queued QEMU stop.");
		int result = m_qemuHostRequestStop != nullptr
			? m_qemuHostRequestStop()
			: m_qemuHostRequestShutdown();
		Trace(L"DirectHost: QEMU stop request returned " + std::to_wstring(result) + L".");
		if (result != 0)
		{
			SetStatus(L"QEMU stop failed with code " + std::to_wstring(result) + L".");
		}
		else
		{
			SetStatus(L"QEMU stop requested.");
		}
	}

	if (shutdown && !stop && m_initialized && m_qemuHostRequestShutdown != nullptr)
	{
		Trace(L"DirectHost: processing queued QEMU shutdown.");
		int result = m_qemuHostRequestShutdown();
		Trace(L"DirectHost: qemu_host_request_shutdown returned " + std::to_wstring(result) + L".");
		if (result != 0)
		{
			SetStatus(L"qemu_host_request_shutdown failed with code " + std::to_wstring(result) + L".");
		}
		else
		{
			SetStatus(L"QEMU shutdown requested.");
		}
	}
}

void QemuDirectHost::ProcessPendingInput()
{
	std::vector<PendingInputEvent> events;
	{
		std::lock_guard<std::mutex> lock(m_inputMutex);
		if (m_pendingInputEvents.empty())
		{
			return;
		}
		events.swap(m_pendingInputEvents);
	}

	bool pointerAbsChecked = false;
	bool pointerAbsReady = false;
	auto ensurePointerAbsCaptured = [this, &pointerAbsChecked, &pointerAbsReady]() -> bool
	{
		if (m_qemuHostSendPointerAbs == nullptr && m_qemuHostSendPointerAbsNormalized == nullptr)
		{
			return false;
		}
		if (pointerAbsChecked)
		{
			return pointerAbsReady;
		}

		pointerAbsChecked = true;
		if (m_qemuHostPointerIsAbsolute != nullptr && m_qemuHostPointerIsAbsolute())
		{
			pointerAbsReady = true;
			return true;
		}

		if (m_qemuHostCapturePointerAbs != nullptr && m_qemuHostCapturePointerAbs() == 0)
		{
			pointerAbsReady = m_qemuHostPointerIsAbsolute == nullptr || m_qemuHostPointerIsAbsolute();
			return pointerAbsReady;
		}

		pointerAbsReady = m_qemuHostPointerIsAbsolute == nullptr && m_qemuHostCapturePointerAbs == nullptr;
		return pointerAbsReady;
	};

	for (const PendingInputEvent& event : events)
	{
		switch (event.type)
		{
		case PendingInputType::KeyNumber:
			if (m_qemuHostSendKeyNumber != nullptr)
			{
				m_qemuHostSendKeyNumber(event.a, event.down);
			}
			break;
		case PendingInputType::PointerRel:
			if (m_qemuHostSendPointerRel != nullptr && !ensurePointerAbsCaptured())
			{
				int remainingX = event.a;
				int remainingY = event.b;
				while (remainingX != 0 || remainingY != 0)
				{
					int stepX = (std::max)(-127, (std::min)(127, remainingX));
					int stepY = (std::max)(-127, (std::min)(127, remainingY));
					m_qemuHostSendPointerRel(stepX, stepY);
					remainingX -= stepX;
					remainingY -= stepY;
				}
			}
			break;
		case PendingInputType::PointerAbs:
			if (ensurePointerAbsCaptured() && m_qemuHostSendPointerAbsNormalized != nullptr)
			{
				const int absMax = 0x7fff;
				int width = (std::max)(1, event.c);
				int height = (std::max)(1, event.d);
				int nx = static_cast<int>((static_cast<long long>(event.a) * absMax) / width);
				int ny = static_cast<int>((static_cast<long long>(event.b) * absMax) / height);
				nx = (std::max)(0, (std::min)(absMax, nx));
				ny = (std::max)(0, (std::min)(absMax, ny));
				m_qemuHostSendPointerAbsNormalized(nx, ny);
			}
			else if (ensurePointerAbsCaptured() && m_qemuHostSendPointerAbs != nullptr)
			{
				m_qemuHostSendPointerAbs(event.a, event.b, event.c, event.d);
			}
			break;
		case PendingInputType::PointerButton:
			if (m_qemuHostSendPointerButton != nullptr)
			{
				m_qemuHostSendPointerButton(event.a, event.down);
			}
			break;
		default:
			break;
		}
	}
}

void QemuDirectHost::Trace(const std::wstring& text)
{
	std::wstring line = text + L"\r\n";
	OutputDebugStringW(line.c_str());

	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	HANDLE logFile = CreateFile2(m_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, OPEN_ALWAYS, &params);
	if (logFile != INVALID_HANDLE_VALUE)
	{
		std::string utf8 = Narrow(line);
		DWORD written = 0;
		WriteFile(logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
		CloseHandle(logFile);
	}

	std::function<void(const std::wstring&)> callback;
	{
		std::lock_guard<std::mutex> lock(m_progressMutex);
		callback = m_progressCallback;
	}
	if (callback)
	{
		callback(text);
	}
}

void QemuDirectHost::SetStatus(const std::wstring& text)
{
	std::lock_guard<std::mutex> lock(m_statusMutex);
	m_status = text;
}

std::string QemuDirectHost::Narrow(const std::wstring& value)
{
	if (value.empty())
	{
		return std::string();
	}
	int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	std::string result(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0], size, nullptr, nullptr);
	return result;
}

std::wstring QemuDirectHost::Widen(const char* value)
{
	if (value == nullptr || value[0] == '\0')
	{
		return std::wstring();
	}
	int size = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
	if (size <= 1)
	{
		return std::wstring();
	}
	std::wstring result(static_cast<size_t>(size - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value, -1, &result[0], size - 1);
	return result;
}

std::vector<std::wstring> QemuDirectHost::SplitCommandLine(const std::wstring& commandLine)
{
	std::vector<std::wstring> parts;
	std::wstring current;
	bool inQuotes = false;
	for (size_t i = 0; i < commandLine.size(); i++)
	{
		wchar_t ch = commandLine[i];
		if (ch == L'"')
		{
			inQuotes = !inQuotes;
			continue;
		}
		if (!inQuotes && iswspace(ch))
		{
			if (!current.empty())
			{
				parts.push_back(current);
				current.clear();
			}
			continue;
		}
		current.push_back(ch);
	}
	if (!current.empty())
	{
		parts.push_back(current);
	}
	return parts;
}

std::wstring QemuDirectHost::QemuDllNameFromCommand(const std::vector<std::wstring>& parts)
{
	if (parts.empty() || parts[0].empty())
	{
		return L"qemu-system-x86_64.dll";
	}

	std::wstring program = parts[0];
	size_t slash = program.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
	{
		program = program.substr(slash + 1);
	}

	const std::wstring prefix = L"qemu-system-";
	if (_wcsnicmp(program.c_str(), prefix.c_str(), prefix.size()) != 0)
	{
		return L"qemu-system-x86_64.dll";
	}

	std::wstring target = program.substr(prefix.size());
	const std::wstring exeSuffix = L".exe";
	const std::wstring dllSuffix = L".dll";
	if (target.size() > exeSuffix.size() &&
		_wcsicmp(target.c_str() + target.size() - exeSuffix.size(), exeSuffix.c_str()) == 0)
	{
		target.resize(target.size() - exeSuffix.size());
	}
	if (target.size() > dllSuffix.size() &&
		_wcsicmp(target.c_str() + target.size() - dllSuffix.size(), dllSuffix.c_str()) == 0)
	{
		target.resize(target.size() - dllSuffix.size());
	}

	if (target.empty())
	{
		return L"qemu-system-x86_64.dll";
	}
	for (wchar_t ch : target)
	{
		if (!iswalnum(ch) && ch != L'_')
		{
			return L"qemu-system-x86_64.dll";
		}
	}

	return L"qemu-system-" + target + L".dll";
}

bool QemuDirectHost::CommandUsesHostDisplay(const std::vector<std::wstring>& parts)
{
	for (size_t index = 0; index < parts.size(); index++)
	{
		const std::wstring& part = parts[index];
		if (_wcsicmp(part.c_str(), L"-display") == 0 && index + 1 < parts.size())
		{
			const std::wstring& value = parts[index + 1];
			return _wcsicmp(value.c_str(), L"host") == 0 || value.find(L"host,") == 0;
		}
		if (part.find(L"-display") == 0 && part.find(L"host") != std::wstring::npos)
		{
			return true;
		}
	}
	return false;
}

bool QemuDirectHost::PackagedDllContainsAsciiString(const std::wstring& dllName, const char* text)
{
	if (text == nullptr || text[0] == '\0')
	{
		return false;
	}

	std::wstring path = std::wstring(Package::Current->InstalledLocation->Path->Data()) + L"\\" + dllName;
	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	HANDLE file = CreateFile2(path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &params);
	if (file == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	LARGE_INTEGER size = {};
	if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 512ll * 1024ll * 1024ll)
	{
		CloseHandle(file);
		return false;
	}

	std::vector<char> bytes(static_cast<size_t>(size.QuadPart));
	DWORD bytesRead = 0;
	BOOL readOk = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesRead, nullptr);
	CloseHandle(file);
	if (!readOk || bytesRead != bytes.size())
	{
		return false;
	}

	size_t textLength = strlen(text);
	return std::search(bytes.begin(), bytes.end(), text, text + textLength) != bytes.end();
}

int QemuDirectHost::MapHostKeyNumber(unsigned key)
{
	static const int letterScancodes[] =
	{
		0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
		0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c
	};
	static const int digitScancodes[] =
	{
		0x0b, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a
	};

	if (key >= 'a' && key <= 'z')
	{
		return letterScancodes[key - 'a'];
	}
	if (key >= 'A' && key <= 'Z')
	{
		return letterScancodes[key - 'A'];
	}
	if (key >= '0' && key <= '9')
	{
		return digitScancodes[key - '0'];
	}
	if (key >= QEMU_KEY_F1 && key <= QEMU_KEY_F10)
	{
		return 0x3b + static_cast<int>(key - QEMU_KEY_F1);
	}
	if (key == QEMU_KEY_F11)
	{
		return 0x57;
	}
	if (key == QEMU_KEY_F12)
	{
		return 0x58;
	}
	if (key >= QEMU_KEY_KP0 && key <= QEMU_KEY_KP9)
	{
		static const int keypadScancodes[] = { 0x52, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49 };
		return keypadScancodes[key - QEMU_KEY_KP0];
	}

	switch (key)
	{
	case QEMU_KEY_ESCAPE: return 0x01;
	case QEMU_KEY_BACKSPACE: return 0x0e;
	case QEMU_KEY_TAB: return 0x0f;
	case QEMU_KEY_RETURN: return 0x1c;
	case QEMU_KEY_LCTRL: return 0x1d;
	case QEMU_KEY_LSHIFT: return 0x2a;
	case QEMU_KEY_RSHIFT: return 0x36;
	case QEMU_KEY_LALT: return 0x38;
	case QEMU_KEY_RCTRL: return 0x9d;
	case QEMU_KEY_RALT: return 0xb8;
	case QEMU_KEY_SPACE: return 0x39;
	case QEMU_KEY_CAPSLOCK: return 0x3a;
	case QEMU_KEY_NUMLOCK: return 0x45;
	case QEMU_KEY_SCROLLOCK: return 0x46;
	case QEMU_KEY_KP_MULTIPLY: return 0x37;
	case QEMU_KEY_KP_DIVIDE: return 0xb5;
	case QEMU_KEY_KP_MINUS: return 0x4a;
	case QEMU_KEY_KP_PLUS: return 0x4e;
	case QEMU_KEY_KP_ENTER: return 0x9c;
	case QEMU_KEY_KP_PERIOD: return 0x53;
	case QEMU_KEY_INSERT: return 0xd2;
	case QEMU_KEY_DELETE: return 0xd3;
	case QEMU_KEY_HOME: return 0xc7;
	case QEMU_KEY_END: return 0xcf;
	case QEMU_KEY_PAGEUP: return 0xc9;
	case QEMU_KEY_PAGEDOWN: return 0xd1;
	case QEMU_KEY_UP: return 0xc8;
	case QEMU_KEY_DOWN: return 0xd0;
	case QEMU_KEY_LEFT: return 0xcb;
	case QEMU_KEY_RIGHT: return 0xcd;
	case '-': return 0x0c;
	case '=': return 0x0d;
	case '[': return 0x1a;
	case ']': return 0x1b;
	case '\\': return 0x2b;
	case ';': return 0x27;
	case '\'': return 0x28;
	case '`': return 0x29;
	case ',': return 0x33;
	case '.': return 0x34;
	case '/': return 0x35;
	default:
		return 0;
	}
}

void QemuDirectHost::LogCallback(QemuHostLogLevel level, const char* message, void* opaque)
{
	QemuDirectHost* host = reinterpret_cast<QemuDirectHost*>(opaque);
	if (host == nullptr || message == nullptr)
	{
		return;
	}

	const wchar_t* prefix = L"INFO";
	if (level == QEMU_HOST_LOG_ERROR)
	{
		prefix = L"ERROR";
	}
	else if (level == QEMU_HOST_LOG_WARNING)
	{
		prefix = L"WARN";
	}
	else if (level == QEMU_HOST_LOG_DEBUG)
	{
		prefix = L"DEBUG";
	}

	host->Trace(std::wstring(L"QEMU ") + prefix + L": " + Widen(message));
}

void QemuDirectHost::ProcessVideoUpdate(QemuDirectHost* host, const void* pixels,
	int width, int height, int stride, int format,
	int dirtyX, int dirtyY, int updateWidth, int updateHeight)
{
	if (host == nullptr || pixels == nullptr || width <= 0 || height <= 0 || stride <= 0)
	{
		return;
	}

	int64_t right = (std::min<int64_t>)(width, static_cast<int64_t>(dirtyX) + updateWidth);
	int64_t bottom = (std::min<int64_t>)(height, static_cast<int64_t>(dirtyY) + updateHeight);
	dirtyX = (std::max)(0, dirtyX);
	dirtyY = (std::max)(0, dirtyY);
	updateWidth = static_cast<int>(right) - dirtyX;
	updateHeight = static_cast<int>(bottom) - dirtyY;
	if (updateWidth <= 0 || updateHeight <= 0)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> lock(host->m_frameMutex);
		if (!host->m_frameValid || host->m_frameWidth != static_cast<unsigned>(width) ||
			host->m_frameHeight != static_cast<unsigned>(height))
		{
			dirtyX = 0;
			dirtyY = 0;
			updateWidth = width;
			updateHeight = height;
		}
	}

	std::vector<uint32_t> converted(static_cast<size_t>(updateWidth) * updateHeight);
	if (format >= 1 && format <= 4)
	{
		for (int row = 0; row < updateHeight; row++)
		{
			const uint8_t* source = reinterpret_cast<const uint8_t*>(pixels) +
				static_cast<size_t>(dirtyY + row) * stride;
			uint32_t* target = converted.data() + static_cast<size_t>(row) * updateWidth;
			for (int column = 0; column < updateWidth; column++)
			{
				const uint8_t* value = source + static_cast<size_t>(dirtyX + column) * 4;
				uint32_t r = 0;
				uint32_t g = 0;
				uint32_t b = 0;
				if (format == 1 || format == 4)
				{
					b = value[0];
					g = value[1];
					r = value[2];
				}
				else
				{
					r = value[0];
					g = value[1];
					b = value[2];
				}
				target[column] = PackBgra(r, g, b);
			}
		}
	}
	else
	{
		int bitsPerPixel = (format >> 24) & 0xff;
		int formatType = (format >> 16) & 0xff;
		int redBits = (format >> 8) & 0xf;
		int greenBits = (format >> 4) & 0xf;
		int blueBits = format & 0xf;
		bool reversedRgb = formatType == 3 || formatType == 8 || formatType == 9;
		if (bitsPerPixel == 32)
		{
		for (int row = 0; row < updateHeight; row++)
		{
			const uint32_t* source = reinterpret_cast<const uint32_t*>(
				reinterpret_cast<const uint8_t*>(pixels) + static_cast<size_t>(dirtyY + row) * stride);
			uint32_t* target = converted.data() + static_cast<size_t>(row) * updateWidth;
			for (int column = 0; column < updateWidth; column++)
			{
				uint32_t value = source[dirtyX + column];
				uint32_t r = reversedRgb ? (value & 0xffu) : ((value >> 16) & 0xffu);
				uint32_t g = (value >> 8) & 0xffu;
				uint32_t b = reversedRgb ? ((value >> 16) & 0xffu) : (value & 0xffu);
				target[column] = PackBgra(r, g, b);
			}
		}
		}
		else if (bitsPerPixel == 24)
		{
		for (int row = 0; row < updateHeight; row++)
		{
			const uint8_t* source = reinterpret_cast<const uint8_t*>(pixels) +
				static_cast<size_t>(dirtyY + row) * stride;
			uint32_t* target = converted.data() + static_cast<size_t>(row) * updateWidth;
			for (int column = 0; column < updateWidth; column++)
			{
				const uint8_t* value = source + static_cast<size_t>(dirtyX + column) * 3;
				uint32_t r = reversedRgb ? value[0] : value[2];
				uint32_t g = value[1];
				uint32_t b = reversedRgb ? value[2] : value[0];
				target[column] = PackBgra(r, g, b);
			}
		}
		}
		else if (bitsPerPixel == 16 || bitsPerPixel == 15)
		{
		for (int row = 0; row < updateHeight; row++)
		{
			const uint16_t* source = reinterpret_cast<const uint16_t*>(
				reinterpret_cast<const uint8_t*>(pixels) + static_cast<size_t>(dirtyY + row) * stride);
			uint32_t* target = converted.data() + static_cast<size_t>(row) * updateWidth;
			for (int column = 0; column < updateWidth; column++)
			{
				uint16_t value = source[dirtyX + column];
				uint32_t bMask = blueBits > 0 ? ((1u << blueBits) - 1u) : 0u;
				uint32_t gMask = greenBits > 0 ? ((1u << greenBits) - 1u) : 0u;
				uint32_t rMask = redBits > 0 ? ((1u << redBits) - 1u) : 0u;
				uint32_t bValue = value & bMask;
				uint32_t gValue = (value >> blueBits) & gMask;
				uint32_t rValue = (value >> (blueBits + greenBits)) & rMask;
				uint32_t r = ExpandColorBits(rValue, redBits);
				uint32_t g = ExpandColorBits(gValue, greenBits);
				uint32_t b = ExpandColorBits(bValue, blueBits);
				if (reversedRgb)
				{
					std::swap(r, b);
				}
				target[column] = PackBgra(r, g, b);
			}
		}
		}
		else if (bitsPerPixel == 8)
		{
		for (int row = 0; row < updateHeight; row++)
		{
			const uint8_t* source = reinterpret_cast<const uint8_t*>(pixels) +
				static_cast<size_t>(dirtyY + row) * stride;
			uint32_t* target = converted.data() + static_cast<size_t>(row) * updateWidth;
			for (int column = 0; column < updateWidth; column++)
			{
				uint32_t value = source[dirtyX + column];
				target[column] = PackBgra(value, value, value);
			}
		}
		}
		else
		{
			return;
		}
	}

	{
		std::lock_guard<std::mutex> lock(host->m_frameMutex);
		if (host->m_videoFrameCount == 0)
		{
			host->m_firstVideoFrameTick = GetTickCount64();
		}
		bool resized = !host->m_frameValid ||
			host->m_frameWidth != static_cast<unsigned>(width) ||
			host->m_frameHeight != static_cast<unsigned>(height);
		if (resized)
		{
			host->m_framePixels.assign(static_cast<size_t>(width) * height, 0xff000000u);
			host->m_frameDirty = false;
		}
		for (int row = 0; row < updateHeight; row++)
		{
			const uint32_t* source = converted.data() + static_cast<size_t>(row) * updateWidth;
			uint32_t* target = host->m_framePixels.data() +
				static_cast<size_t>(dirtyY + row) * width + dirtyX;
			std::copy_n(source, updateWidth, target);
		}
		host->m_frameWidth = static_cast<unsigned>(width);
		host->m_frameHeight = static_cast<unsigned>(height);
		host->m_videoFrameCount++;
		host->m_frameValid = true;
		if (!host->m_frameDirty)
		{
			host->m_dirtyX = static_cast<unsigned>(dirtyX);
			host->m_dirtyY = static_cast<unsigned>(dirtyY);
			host->m_dirtyWidth = static_cast<unsigned>(updateWidth);
			host->m_dirtyHeight = static_cast<unsigned>(updateHeight);
		}
		else
		{
			unsigned right = (std::max)(host->m_dirtyX + host->m_dirtyWidth,
				static_cast<unsigned>(dirtyX + updateWidth));
			unsigned bottom = (std::max)(host->m_dirtyY + host->m_dirtyHeight,
				static_cast<unsigned>(dirtyY + updateHeight));
			host->m_dirtyX = (std::min)(host->m_dirtyX, static_cast<unsigned>(dirtyX));
			host->m_dirtyY = (std::min)(host->m_dirtyY, static_cast<unsigned>(dirtyY));
			host->m_dirtyWidth = right - host->m_dirtyX;
			host->m_dirtyHeight = bottom - host->m_dirtyY;
		}
		host->m_frameDirty = true;
		host->m_pointerWidth = width;
		host->m_pointerHeight = height;
	}

	static bool logged = false;
	if (!logged)
	{
		logged = true;
		std::wstringstream formatStream;
		formatStream << std::hex << std::uppercase << format;
		ULONGLONG elapsed = host->m_loopStartTick != 0 ? GetTickCount64() - host->m_loopStartTick : 0;
		host->Trace(L"DirectHost: first QEMU video frame " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
			L", stride=" + std::to_wstring(stride) + L", format=0x" + formatStream.str() +
			L", elapsed_ms=" + std::to_wstring(elapsed));
	}
}

void QemuDirectHost::VideoCallback(const void* pixels, int width, int height,
	int stride, int format, void* opaque)
{
	ProcessVideoUpdate(reinterpret_cast<QemuDirectHost*>(opaque), pixels,
		width, height, stride, format, 0, 0, width, height);
}

void QemuDirectHost::AudioCallback(const void*, size_t size, int sampleRate, int channels, int, void* opaque)
{
	QemuDirectHost* host = reinterpret_cast<QemuDirectHost*>(opaque);
	static bool logged = false;
	if (host != nullptr && !logged)
	{
		logged = true;
		host->Trace(L"DirectHost: first QEMU audio callback, bytes=" + std::to_wstring(size) +
			L", rate=" + std::to_wstring(sampleRate) + L", channels=" + std::to_wstring(channels));
	}
}

void QemuDirectHost::LogCallbackV2(void* opaque, QemuHostLogLevel level, const char* message)
{
	LogCallback(level, message, opaque);
}

void QemuDirectHost::VideoCallbackV2(void* opaque, const void* pixels, int width, int height, int stride, int format)
{
	VideoCallback(pixels, width, height, stride, format, opaque);
}

void QemuDirectHost::VideoUpdateCallbackV3(void* opaque, const void* pixels,
	int width, int height, int stride, int format,
	int x, int y, int updateWidth, int updateHeight)
{
	ProcessVideoUpdate(reinterpret_cast<QemuDirectHost*>(opaque), pixels,
		width, height, stride, format, x, y, updateWidth, updateHeight);
}

void QemuDirectHost::AudioCallbackV2(void* opaque, const void* samples, size_t size, int sample_rate, int channels, int format)
{
	AudioCallback(samples, size, sample_rate, channels, format, opaque);
}

void QemuDirectHost::InputCallback(int, int, int, void*)
{
}
