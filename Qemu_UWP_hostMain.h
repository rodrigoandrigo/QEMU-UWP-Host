#pragma once

#include "Common\StepTimer.h"
#include "Common\DeviceResources.h"
#include "QemuDirectHost.h"
#include "Content\QemuFrameRenderer.h"

namespace Qemu_UWP_host
{
	class Qemu_UWP_hostMain : public DX::IDeviceNotify
	{
	public:
		Qemu_UWP_hostMain(const std::shared_ptr<DX::DeviceResources>& deviceResources);
		~Qemu_UWP_hostMain();

		void CreateWindowSizeDependentResources();
		void StartRenderLoop();
		void StopRenderLoop();
		bool LoadGame(Windows::Storage::StorageFile^ file, std::wstring* error);
		void RunLoadedGame();
		bool PauseCore(std::wstring* error);
		bool ResumeCore(std::wstring* error);
		bool ShutdownCore(std::wstring* error);
		void StopCore();
		void ResetCore();
		void SetProgressCallback(std::function<void(const std::wstring&)> callback);
		void SetKey(unsigned qemuKey, bool pressed);
		void SetPointer(float positionX, float positionY, float width, float height, int deltaX, int deltaY, bool left, bool right, bool middle);
		void ClearPointer();
		void ClearInput();
		std::wstring StatusText() const;
		std::wstring ApiCompatibilityText() const;
		bool HasVideoFrame() const;
		void TrackingUpdate(float positionX, float positionY);
		void StopTracking();
		Concurrency::critical_section& GetCriticalSection() { return m_criticalSection; }

		virtual void OnDeviceLost();
		virtual void OnDeviceRestored();

	private:
		void ProcessInput();
		void Update();
		bool Render();

		std::shared_ptr<DX::DeviceResources> m_deviceResources;
		QemuDirectHost m_host;
		std::unique_ptr<QemuFrameRenderer> m_frameRenderer;
		Windows::Foundation::IAsyncAction^ m_renderLoopWorker;
		Concurrency::critical_section m_criticalSection;
		DX::StepTimer m_timer;
	};
}
