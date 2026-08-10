//
// DirectXPage.xaml.h
// DirectXPage class declaration.
//

#pragma once

#include "DirectXPage.g.h"

#include "Common\DeviceResources.h"
#include "Qemu_UWP_hostMain.h"

#include <string>
#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

namespace Qemu_UWP_host
{
	/// <summary>
	/// A page that hosts a DirectX SwapChainPanel.
	/// </summary>
	public ref class DirectXPage sealed
	{
	public:
		DirectXPage();
		virtual ~DirectXPage();

		void SaveInternalState(Windows::Foundation::Collections::IPropertySet^ state);
		void LoadInternalState(Windows::Foundation::Collections::IPropertySet^ state);

	private:
		// Low-level XAML rendering event handler.
		void OnRendering(Platform::Object^ sender, Platform::Object^ args);

		// Window event handlers.
		void OnVisibilityChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::VisibilityChangedEventArgs^ args);

		// DisplayInformation event handlers.
		void OnDpiChanged(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);
		void OnOrientationChanged(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);
		void OnDisplayContentsInvalidated(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);

		// Other event handlers.
		void SelectBootButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void SelectDriveButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void SelectCdromButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RemoveDriveButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void RemoveCdromButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ClearBootMediaButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void DriveMediaHistory_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void CdromMediaHistory_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void DirectBootFile_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void DirectBootText_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::TextChangedEventArgs^ e);
		void ClearFirmwareButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ClearKernelButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ClearInitrdButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ClearDtbButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ClearKernelAppendButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void BootDevice_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void QemuSelector_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void ResetCommandsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void AlignCommandsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void CommandLineBox_TextChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::TextChangedEventArgs^ e);
		void StartButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void PauseButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ResumeButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void StopButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ShutdownButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void MetricsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ResetButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ShowTabsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void DetectTargetsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void TestPackagedDllsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ClearErrorsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void UpdateCommandLineButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ArgumentsHelpButton_PointerEntered(Platform::Object^ sender, Windows::UI::Xaml::Input::PointerRoutedEventArgs^ e);
		void ArgumentsHelpButton_PointerExited(Platform::Object^ sender, Windows::UI::Xaml::Input::PointerRoutedEventArgs^ e);
		void ArgumentsHelpHideTimer_Tick(Platform::Object^ sender, Platform::Object^ e);
		void BootMediaRefreshTimer_Tick(Platform::Object^ sender, Platform::Object^ e);
		void BootOption_Changed(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void BootOptionText_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::TextChangedEventArgs^ e);
		void SelectSharedFolderButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ClearSharedFolderButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void SharedFolderMode_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void RfbServer_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void RfbPort_TextChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::TextChangedEventArgs^ e);
		void Architecture_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void DiagnosticProfile_Changed(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void MemoryBox_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);
		void OnKeyUp(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);
		void OnBackRequested(Platform::Object^ sender, Windows::UI::Core::BackRequestedEventArgs^ args);
		void OnCompositionScaleChanged(Windows::UI::Xaml::Controls::SwapChainPanel^ sender, Object^ args);
		void OnSwapChainPanelSizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
		void SetStatus(const std::wstring& text);
		void AppendError(const std::wstring& text);
		void ToggleInputCapture();
		void UpdateCaptureIndicators();
		void ApplyInputCaptureState();
		void ApplyCorePointerCaptureState();
		void FocusEmulatorSurface();
		void SendPointerToCore(Windows::UI::Core::PointerEventArgs^ e);
		void RefreshCommandLinePreview();
		bool ValidateStartConfiguration(std::wstring& report);
		void RefreshBootMediaState();
		void RefreshBootMediaHistory();
		void RefreshDirectBootFileSelectors();
		void UpdateBootMediaSize();
		void SelectBootMediaHistoryItem(Windows::UI::Xaml::Controls::ComboBox^ comboBox, bool cdromMedia);
		void ApplyDetectedMediaSettings(Windows::Storage::StorageFile^ file, bool cdromMedia);
		void UpdateMediaDetectionText();
		void RefreshQemuTargetList();
		void SelectDirectBootFile(Windows::UI::Xaml::Controls::ComboBox^ comboBox, Windows::UI::Xaml::Controls::TextBox^ textBox);
		void ClearDirectBootField(Windows::UI::Xaml::Controls::ComboBox^ comboBox, Windows::UI::Xaml::Controls::TextBox^ textBox);
		bool IsBootMediaFile(Windows::Storage::StorageFile^ file);
		void RefreshQemuOptionSelectors(bool showProgress);
		std::wstring SelectedQemuTarget();
		std::wstring SelectedQemuDllName();
		void FillSelector(Windows::UI::Xaml::Controls::ComboBox^ comboBox, const std::vector<std::pair<std::wstring, std::wstring>>& options, Platform::String^ emptyLabel);
		void SelectComboBoxValue(Windows::UI::Xaml::Controls::ComboBox^ comboBox, const std::wstring& value);
		void ApplyProfileSelectors(int profile);
		std::wstring SelectedSelectorValue(Windows::UI::Xaml::Controls::ComboBox^ comboBox);
		std::wstring SelectedComboTag(Windows::UI::Xaml::Controls::ComboBox^ comboBox);
		std::wstring BuildAutomaticCommandLine();
		std::wstring BuildAdditionalArguments();
		void UpdateCommandPreview();
		void ResetToStartupDefaults();
		Platform::String^ BuildCommandLine();
		bool EnsureMediaNbdServer(Windows::Storage::StorageFile^ mediaFile, bool readOnly, bool cdromMedia, std::wstring& url);
		void StopMediaNbdServer(bool cdromMedia);
		void StopAllMediaNbdServers();
		void OnMediaNbdConnectionReceived(Windows::Networking::Sockets::StreamSocketListener^ sender, Windows::Networking::Sockets::StreamSocketListenerConnectionReceivedEventArgs^ args);
		void ServeMediaNbdClient(Windows::Networking::Sockets::StreamSocket^ socket, std::wstring mediaPath, uint64_t mediaSize, bool readOnly);
		bool RfbServerEnabled();
		int RfbExternalPort();
		int RfbInternalPort();
		std::wstring RfbDisplayArgument();
		std::wstring LocalRfbAddress();
		void UpdateRfbStatus(const std::wstring& status);
		void UpdateRfbRuntimeOverlay();
		bool EnsureRfbProxyServer();
		void StopRfbProxyServer();
		void OnRfbProxyConnectionReceived(Windows::Networking::Sockets::StreamSocketListener^ sender, Windows::Networking::Sockets::StreamSocketListenerConnectionReceivedEventArgs^ args);
		void ServeRfbProxyClient(Windows::Networking::Sockets::StreamSocket^ clientSocket, int internalPort);
		void RelayRfbStream(Windows::Storage::Streams::IInputStream^ input, Windows::Storage::Streams::IOutputStream^ output);
		bool IsCommandLineFile(Windows::Storage::StorageFile^ file);
		Platform::String^ QuoteForCommandLine(Platform::String^ value);
		void StageBootFileAndStart();
		void WriteCommandLineAndStart(Platform::String^ commandLine);
		void StartWithCommandFile(Windows::Storage::StorageFile^ commandFile);
		void SetStartState(bool starting, bool running);
		void UpdateVmControlButtons();
		unsigned MapVirtualKeyToQemuKey(Windows::System::VirtualKey key);
		void GamepadPollTimer_Tick(Platform::Object^ sender, Platform::Object^ e);
		void MetricsTimer_Tick(Platform::Object^ sender, Platform::Object^ e);
		void ResetPerformanceMetrics();
		void UpdatePerformanceMetrics();
		void UpdateMetricsControl();
		void PollGamepad();
		void BuildVirtualKeyboard();
		void UpdateVirtualKeyboardVisibility();
		void UpdateVirtualKeyboardSelection();
		void MoveVirtualKeyboardSelection(int dx, int dy);
		void SendVirtualKeyboardSelectedKey();
		void SetGamepadGuestKey(bool& state, unsigned key, bool down);
		void ReleaseGamepadGuestKeys();
		void ReleaseGamepadMouseButtons();

		struct VirtualKeyboardKey
		{
			std::wstring label;
			unsigned key;
			int row;
			int column;
			int columnSpan;
			Windows::UI::Xaml::Controls::Button^ button;
		};

		// Track independent input on a background worker thread.
		Windows::Foundation::IAsyncAction^ m_inputLoopWorker;
		Windows::UI::Core::CoreIndependentInputSource^ m_coreInput;
		Windows::UI::Core::SystemNavigationManager^ m_systemNavigationManager;
		Windows::Foundation::EventRegistrationToken m_backRequestedToken;

		// Independent input handling functions.
		void OnPointerPressed(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);
		void OnPointerMoved(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);
		void OnPointerReleased(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);
		void OnPointerCaptureLost(Platform::Object^ sender, Windows::UI::Core::PointerEventArgs^ e);

		// Resources used to render DirectX content behind the XAML page.
		std::shared_ptr<DX::DeviceResources> m_deviceResources;
		std::unique_ptr<Qemu_UWP_hostMain> m_main; 
		Windows::Storage::StorageFile^ m_selectedBootFile;
		Windows::Storage::StorageFile^ m_stagedBootFile;
		Windows::Storage::StorageFile^ m_selectedDriveFile;
		Windows::Storage::StorageFile^ m_selectedCdromFile;
		Windows::Storage::StorageFile^ m_selectedCommandFile;
		Windows::Storage::StorageFile^ m_stagedDriveFile;
		Windows::Storage::StorageFile^ m_stagedCdromFile;
		Windows::Storage::StorageFile^ m_stagedCommandFile;
		Windows::Storage::StorageFile^ m_generatedCommandFile;
		Windows::Storage::StorageFolder^ m_selectedSharedFolder;
		Windows::Networking::Sockets::StreamSocketListener^ m_driveNbdListener;
		Windows::Networking::Sockets::StreamSocketListener^ m_cdromNbdListener;
		Windows::Networking::Sockets::StreamSocketListener^ m_rfbProxyListener;
		std::wstring m_driveNbdPath;
		std::wstring m_cdromNbdPath;
		std::wstring m_rfbProxyAddress;
		uint64_t m_driveNbdSize;
		uint64_t m_cdromNbdSize;
		int m_driveNbdPort;
		int m_cdromNbdPort;
		int m_rfbProxyPort;
		int m_rfbInternalPort;
		int m_rfbClientCount;
		bool m_driveNbdReadOnly;
		bool m_cdromNbdReadOnly;
		bool m_rfbStatusEnabled;
		std::atomic_bool m_rfbStopping;
		Windows::UI::Xaml::DispatcherTimer^ m_argumentsHelpHideTimer;
		Windows::UI::Xaml::DispatcherTimer^ m_bootMediaRefreshTimer;
		Windows::UI::Xaml::DispatcherTimer^ m_gamepadPollTimer;
		Windows::UI::Xaml::DispatcherTimer^ m_metricsTimer;
		std::vector<VirtualKeyboardKey> m_virtualKeyboardKeys;
		unsigned int m_gamepadPreviousButtons;
		bool m_virtualKeyboardVisible;
		bool m_gamepadTriggerToggleArmed;
		bool m_gamepadBumperToggleArmed;
		bool m_gamepadGuestUp;
		bool m_gamepadGuestDown;
		bool m_gamepadGuestLeft;
		bool m_gamepadGuestRight;
		bool m_gamepadGuestEnter;
		bool m_gamepadMouseLeft;
		bool m_gamepadMouseRight;
		int m_virtualKeyboardSelectedIndex;
		bool m_bootMediaSizeRefreshActive;
		bool m_refreshingBootMediaHistory;
		bool m_refreshingDirectBootSelectors;
		bool m_refreshingQemuSelectors;
		bool m_refreshingQemuTargets;
		unsigned int m_qemuSelectorLoadGeneration;
		bool m_updatingCommandText;
		bool m_isStarting;
		bool m_isRunning;
		bool m_isPaused;
		bool m_isShutdownPending;
		bool m_isStopPending;
		bool m_expectHostVideoFrame;
		uint64_t m_metricsStartTick;
		uint64_t m_metricsLastSampleTick;
		uint64_t m_metricsLastCpuTime100ns;
		unsigned int m_metricsLastFrameCount;
		bool m_metricsBootComplete;
		bool m_metricsEnabled;
		bool m_windowVisible;
		bool m_inputCaptured;
		Windows::UI::Core::CoreCursor^ m_visiblePointerCursor;
		bool m_corePointerCaptureActive;
		bool m_ctrlDown;
		bool m_altDown;
		double m_inputSurfaceWidth;
		double m_inputSurfaceHeight;
		double m_emulatorPointerX;
		double m_emulatorPointerY;
		double m_lastPhysicalPointerX;
		double m_lastPhysicalPointerY;
		bool m_havePhysicalPointerPosition;
	};
}






