#include "pch.h"
#include "Qemu_UWP_hostMain.h"
#include "Common\DirectXHelper.h"

#include <algorithm>

using namespace Qemu_UWP_host;
using namespace Windows::Foundation;
using namespace Windows::System::Threading;
using namespace Concurrency;

Qemu_UWP_hostMain::Qemu_UWP_hostMain(const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	m_deviceResources(deviceResources),
	m_renderLoopWorker(nullptr)
{
	m_deviceResources->RegisterDeviceNotify(this);
	m_frameRenderer = std::unique_ptr<QemuFrameRenderer>(new QemuFrameRenderer(m_deviceResources, &m_host));
	m_timer.SetFixedTimeStep(true);
	m_timer.SetTargetElapsedSeconds(1.0 / 60);
}

Qemu_UWP_hostMain::~Qemu_UWP_hostMain()
{
	StopRenderLoop();
	m_deviceResources->RegisterDeviceNotify(nullptr);
}

void Qemu_UWP_hostMain::CreateWindowSizeDependentResources()
{
	m_frameRenderer->CreateWindowSizeDependentResources();
}

void Qemu_UWP_hostMain::StartRenderLoop()
{
	if (m_renderLoopWorker != nullptr && m_renderLoopWorker->Status == AsyncStatus::Started)
	{
		return;
	}

	auto workItemHandler = ref new WorkItemHandler([this](IAsyncAction^ action)
	{
		while (action->Status == AsyncStatus::Started)
		{
			ULONGLONG frameStart = GetTickCount64();
			bool presented = false;
			{
				critical_section::scoped_lock lock(m_criticalSection);
				Update();
				if (Render())
				{
					m_deviceResources->Present();
					presented = true;
				}
			}

			ULONGLONG elapsed = GetTickCount64() - frameStart;
			ULONGLONG targetDelay = presented ? 16 : 33;
			Sleep(static_cast<DWORD>(elapsed < targetDelay ? targetDelay - elapsed : 1));
		}
	});

	m_renderLoopWorker = ThreadPool::RunAsync(workItemHandler, WorkItemPriority::Normal, WorkItemOptions::TimeSliced);
}

void Qemu_UWP_hostMain::StopRenderLoop()
{
	if (m_renderLoopWorker != nullptr)
	{
		m_renderLoopWorker->Cancel();
		m_renderLoopWorker = nullptr;
	}
}

void Qemu_UWP_hostMain::Update()
{
	ProcessInput();
	m_timer.Tick([&]()
	{
	});
}

void Qemu_UWP_hostMain::ProcessInput()
{
}

bool Qemu_UWP_hostMain::Render()
{
	if (m_timer.GetFrameCount() == 0)
	{
		return false;
	}

	return m_frameRenderer->Render();
}

bool Qemu_UWP_hostMain::LoadGame(Windows::Storage::StorageFile^ file, std::wstring* error)
{
	return m_host.LoadGame(file, error);
}

void Qemu_UWP_hostMain::RunLoadedGame()
{
	m_host.RunLoadedGame();
}

bool Qemu_UWP_hostMain::PauseCore(std::wstring* error)
{
	return m_host.Pause(error);
}

bool Qemu_UWP_hostMain::ResumeCore(std::wstring* error)
{
	return m_host.Resume(error);
}

bool Qemu_UWP_hostMain::ShutdownCore(std::wstring* error)
{
	return m_host.RequestShutdown(error);
}

void Qemu_UWP_hostMain::StopCore()
{
	m_host.Stop();
}

void Qemu_UWP_hostMain::ResetCore()
{
	m_host.Reset();
}

void Qemu_UWP_hostMain::SetProgressCallback(std::function<void(const std::wstring&)> callback)
{
	m_host.SetProgressCallback(callback);
}

void Qemu_UWP_hostMain::SetKey(unsigned key, bool pressed)
{
	m_host.SetKey(key, pressed);
}

void Qemu_UWP_hostMain::SetPointer(float positionX, float positionY, float width, float height, int deltaX, int deltaY, bool left, bool right, bool middle)
{
	m_host.SetPointer(positionX, positionY, width, height, deltaX, deltaY, left, right, middle);
}

void Qemu_UWP_hostMain::ClearPointer()
{
	m_host.ClearPointer();
}

void Qemu_UWP_hostMain::ClearInput()
{
	m_host.ClearInput();
}

std::wstring Qemu_UWP_hostMain::StatusText() const
{
	return m_host.StatusText();
}

std::wstring Qemu_UWP_hostMain::ApiCompatibilityText() const
{
	return m_host.ApiCompatibilityText();
}

bool Qemu_UWP_hostMain::HasVideoFrame() const
{
	return m_host.HasVideoFrame();
}

void Qemu_UWP_hostMain::TrackingUpdate(float positionX, float positionY)
{
	m_host.SetPointer(positionX, positionY, (std::max)(1.0f, positionX), (std::max)(1.0f, positionY), 0, 0, false, false, false);
}

void Qemu_UWP_hostMain::StopTracking()
{
	m_host.ClearPointer();
}

void Qemu_UWP_hostMain::OnDeviceLost()
{
	m_frameRenderer->ReleaseDeviceDependentResources();
}

void Qemu_UWP_hostMain::OnDeviceRestored()
{
	m_frameRenderer->CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();
}
