#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Qemu_UWP_host
{
	struct QemuHostFrameSnapshot
	{
		unsigned width;
		unsigned height;
		unsigned frameNumber;
		bool valid;
		bool dirty;
		std::vector<uint32_t> pixels;
	};

	class QemuDirectHost
	{
	public:
		QemuDirectHost();
		~QemuDirectHost();

		bool LoadGame(Windows::Storage::StorageFile^ commandFile, std::wstring* error);
		void RunLoadedGame();
		bool Pause(std::wstring* error);
		bool Resume(std::wstring* error);
		bool RequestShutdown(std::wstring* error);
		void Stop();
		void Reset();
		void SetProgressCallback(std::function<void(const std::wstring&)> callback);
		std::wstring StatusText() const;
		bool HasVideoFrame() const;
		QemuHostFrameSnapshot CopyFrame(bool forcePixels);
		void SetKey(unsigned key, bool down);
		void SetPointer(float x, float y, float width, float height, int deltaX, int deltaY, bool left, bool right, bool middle);
		void ClearPointer();
		void ClearInput();
		std::wstring ApiCompatibilityText() const;

	private:
		enum QemuHostLogLevel
		{
			QEMU_HOST_LOG_ERROR = 0,
			QEMU_HOST_LOG_WARNING = 1,
			QEMU_HOST_LOG_INFO = 2,
			QEMU_HOST_LOG_DEBUG = 3,
		};

		typedef void (*QemuHostLogCallback)(QemuHostLogLevel level, const char* message, void* opaque);
		typedef void (*QemuHostVideoCallback)(const void* pixels, int width, int height, int stride, int format, void* opaque);
		typedef void (*QemuHostAudioCallback)(const void* samples, size_t size, int sample_rate, int channels, int format, void* opaque);
		typedef void (*QemuHostLogCallbackV2)(void* opaque, QemuHostLogLevel level, const char* message);
		typedef void (*QemuHostVideoCallbackV2)(void* opaque, const void* pixels, int width, int height, int stride, int format);
		typedef void (*QemuHostAudioCallbackV2)(void* opaque, const void* samples, size_t size, int sample_rate, int channels, int format);
		typedef void (*QemuHostInputCallback)(int type, int code, int value, void* opaque);

		typedef int (*qemu_host_init_t)(int argc, const char* const* argv);
		typedef int (*qemu_host_start_with_args_t)(int argc, const char* const* argv);
		typedef int (*qemu_host_run_t)();
		typedef int (*qemu_host_start_t)();
		typedef int (*qemu_host_main_loop_step_t)(bool nonblocking, int* status);
		typedef int (*qemu_host_pause_t)();
		typedef int (*qemu_host_resume_t)();
		typedef int (*qemu_host_request_shutdown_t)();
		typedef int (*qemu_host_reset_t)();
		typedef int (*qemu_host_join_t)(int* status);
		typedef int (*qemu_host_cleanup_t)();
		typedef int (*qemu_host_send_key_number_t)(int keycode, bool down);
		typedef int (*qemu_host_send_key_qcode_t)(int qcode, bool down);
		typedef int (*qemu_host_send_pointer_rel_t)(int dx, int dy);
		typedef int (*qemu_host_send_pointer_abs_t)(int x, int y, int width, int height);
		typedef int (*qemu_host_send_pointer_abs_normalized_t)(int x, int y);
		typedef int (*qemu_host_send_pointer_button_t)(int button, bool down);
		typedef int (*qemu_host_capture_pointer_abs_t)();
		typedef void (*qemu_host_set_log_callback_t)(QemuHostLogCallback callback, void* opaque);
		typedef void (*qemu_host_set_video_callback_t)(QemuHostVideoCallback callback, void* opaque);
		typedef void (*qemu_host_set_audio_callback_t)(QemuHostAudioCallback callback, void* opaque);
		typedef void (*qemu_host_register_log_callback_t)(QemuHostLogCallbackV2 callback, void* opaque);
		typedef void (*qemu_host_register_video_callback_t)(QemuHostVideoCallbackV2 callback, void* opaque);
		typedef void (*qemu_host_register_audio_callback_t)(QemuHostAudioCallbackV2 callback, void* opaque);
		typedef void (*qemu_host_set_input_callback_t)(QemuHostInputCallback callback, void* opaque);
		typedef bool (*qemu_host_is_initialized_t)();
		typedef bool (*qemu_host_is_running_t)();
		typedef bool (*qemu_host_pointer_is_absolute_t)();
		// ABI: high 16 bits = major, low 16 bits = minor. New host DLLs must export this.
		typedef unsigned int (*qemu_host_get_api_version_t)();

		bool LoadQemuDll(const std::wstring& dllName, std::wstring* error);
		template <typename T>
		bool Resolve(const char* name, T& target, std::wstring* error);
		void RunQemu();
		void ProcessPendingControl();
		void ProcessPendingInput();
		void Trace(const std::wstring& text);
		void SetStatus(const std::wstring& text);
		static std::string Narrow(const std::wstring& value);
		static std::wstring Widen(const char* value);
		static std::vector<std::wstring> SplitCommandLine(const std::wstring& commandLine);
		static std::wstring QemuDllNameFromCommand(const std::vector<std::wstring>& parts);
		static bool CommandUsesHostDisplay(const std::vector<std::wstring>& parts);
		static bool PackagedDllContainsAsciiString(const std::wstring& dllName, const char* text);
		static int MapHostKeyNumber(unsigned key);
		static void LogCallback(QemuHostLogLevel level, const char* message, void* opaque);
		static void VideoCallback(const void* pixels, int width, int height, int stride, int format, void* opaque);
		static void AudioCallback(const void* samples, size_t size, int sample_rate, int channels, int format, void* opaque);
		static void LogCallbackV2(void* opaque, QemuHostLogLevel level, const char* message);
		static void VideoCallbackV2(void* opaque, const void* pixels, int width, int height, int stride, int format);
		static void AudioCallbackV2(void* opaque, const void* samples, size_t size, int sample_rate, int channels, int format);
		static void InputCallback(int type, int code, int value, void* opaque);

		enum class PendingInputType
		{
			KeyNumber,
			PointerRel,
			PointerAbs,
			PointerButton,
		};

		struct PendingInputEvent
		{
			PendingInputType type;
			int a;
			int b;
			int c;
			int d;
			bool down;
		};

		HMODULE m_module;
		qemu_host_init_t m_qemuHostInit;
		qemu_host_start_with_args_t m_qemuHostStartWithArgs;
		qemu_host_run_t m_qemuHostRun;
		qemu_host_start_t m_qemuHostStart;
		qemu_host_main_loop_step_t m_qemuHostMainLoopStep;
		qemu_host_pause_t m_qemuHostPause;
		qemu_host_resume_t m_qemuHostResume;
		qemu_host_request_shutdown_t m_qemuHostRequestShutdown;
		qemu_host_reset_t m_qemuHostReset;
		qemu_host_join_t m_qemuHostJoin;
		qemu_host_cleanup_t m_qemuHostCleanup;
		qemu_host_send_key_number_t m_qemuHostSendKeyNumber;
		qemu_host_send_key_qcode_t m_qemuHostSendKeyQcode;
		qemu_host_send_pointer_rel_t m_qemuHostSendPointerRel;
		qemu_host_send_pointer_abs_t m_qemuHostSendPointerAbs;
		qemu_host_send_pointer_abs_normalized_t m_qemuHostSendPointerAbsNormalized;
		qemu_host_send_pointer_button_t m_qemuHostSendPointerButton;
		qemu_host_capture_pointer_abs_t m_qemuHostCapturePointerAbs;
		qemu_host_set_log_callback_t m_qemuHostSetLogCallback;
		qemu_host_set_video_callback_t m_qemuHostSetVideoCallback;
		qemu_host_set_audio_callback_t m_qemuHostSetAudioCallback;
		qemu_host_register_log_callback_t m_qemuHostRegisterLogCallback;
		qemu_host_register_video_callback_t m_qemuHostRegisterVideoCallback;
		qemu_host_register_audio_callback_t m_qemuHostRegisterAudioCallback;
		qemu_host_set_input_callback_t m_qemuHostSetInputCallback;
		qemu_host_is_initialized_t m_qemuHostIsInitialized;
		qemu_host_is_running_t m_qemuHostIsRunning;
		qemu_host_pointer_is_absolute_t m_qemuHostPointerIsAbsolute;
		qemu_host_get_api_version_t m_qemuHostGetApiVersion;
		unsigned int m_hostApiVersion;
		bool m_usesLegacyHostApi;
		Windows::Foundation::IAsyncAction^ m_worker;
		mutable std::mutex m_statusMutex;
		std::mutex m_progressMutex;
		std::wstring m_status;
		std::wstring m_logPath;
		std::wstring m_stderrPath;
		std::wstring m_loadedDllName;
		std::vector<std::string> m_arguments;
		std::vector<char*> m_argv;
		std::function<void(const std::wstring&)> m_progressCallback;
		mutable std::mutex m_frameMutex;
		std::mutex m_inputMutex;
		std::mutex m_controlMutex;
		std::vector<PendingInputEvent> m_pendingInputEvents;
		std::vector<uint32_t> m_framePixels;
		unsigned m_frameWidth;
		unsigned m_frameHeight;
		unsigned m_videoFrameCount;
		bool m_frameValid;
		bool m_frameDirty;
		bool m_mouseLeft;
		bool m_mouseRight;
		bool m_mouseMiddle;
		int m_pointerWidth;
		int m_pointerHeight;
		ULONGLONG m_loopStartTick;
		bool m_initializedOnce;
		bool m_initialized;
		bool m_deferredInit;
		bool m_running;
		bool m_pendingPause;
		bool m_pendingResume;
		bool m_pendingShutdown;
		bool m_pendingStop;
	};
}
