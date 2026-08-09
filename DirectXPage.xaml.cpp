//
// DirectXPage.xaml.cpp
// DirectXPage class implementation.
//

#include "pch.h"
#include "DirectXPage.xaml.h"
#include "QemuInputKeys.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <initializer_list>
#include <iomanip>
#include <ppltasks.h>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

using namespace Qemu_UWP_host;

using namespace Platform;
using namespace Windows::ApplicationModel;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Gaming::Input;
using namespace Windows::Graphics::Display;
using namespace Windows::Networking;
using namespace Windows::Networking::Connectivity;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;
using namespace Windows::Storage::Streams;
using namespace Windows::System;
using namespace Windows::System::Diagnostics;
using namespace Windows::System::Threading;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Documents;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace concurrency;

namespace
{
	const int ProfileNormalCommand = 0;
	const int ProfileVideoOnlyNoMedia = 1;
	const int ProfileLinuxCloudUefiQcow2 = 2;
	const int ProfileWindows98 = 3;
	const int ProfileWindowsXp = 4;
	const int ProfileWindows7 = 5;
	const int ProfileXboxTcg64 = 6;
	const int ProfileXboxTcg128 = 7;
	const int ProfileXboxTcg256 = 8;
	const int BootDeviceAuto = 0;
	const int BootDeviceDrive = 1;
	const int BootDeviceCdrom = 2;
	const wchar_t* DriveId = L"drive0";
	const wchar_t* CdromId = L"cdrom0";
	const wchar_t* SharedDriveId = L"shared0";
	const bool VvfatFeatureEnabled = false;
	const size_t MaxVisibleErrorLogChars = 65536;

	void TraceStartup(const std::wstring& text)
	{
		std::wstring line = text;
		line += L"\r\n";
		OutputDebugStringW(line.c_str());

		try
		{
			std::wstring logPath(ApplicationData::Current->LocalFolder->Path->Data());
			logPath += L"\\qemu-startup.log";
			CREATEFILE2_EXTENDED_PARAMETERS params = {};
			params.dwSize = sizeof(params);
			HANDLE logFile = CreateFile2(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, OPEN_ALWAYS, &params);
			if (logFile != INVALID_HANDLE_VALUE)
			{
				int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
				if (size > 0)
				{
					std::string utf8(size, '\0');
					WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), &utf8[0], size, nullptr, nullptr);
					DWORD written = 0;
					WriteFile(logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
				}
				CloseHandle(logFile);
			}
		}
		catch (...)
		{
		}
	}

	std::wstring TrimVisibleErrorLog(std::wstring value)
	{
		if (value.size() <= MaxVisibleErrorLogChars)
		{
			return value;
		}

		size_t start = value.size() - MaxVisibleErrorLogChars;
		size_t lineStart = value.find(L'\n', start);
		if (lineStart != std::wstring::npos && lineStart + 1 < value.size())
		{
			start = lineStart + 1;
		}

		return L"... older log lines trimmed from the visible error box; full log remains in qemu-uwp.log ...\r\n" +
			value.substr(start);
	}

	bool HasExtension(const std::wstring& name, std::initializer_list<const wchar_t*> extensions)
	{
		for (const wchar_t* extension : extensions)
		{
			size_t extensionLength = wcslen(extension);
			if (name.length() >= extensionLength &&
				_wcsicmp(name.c_str() + name.length() - extensionLength, extension) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool IsDriveMediaName(const std::wstring& name)
	{
		return HasExtension(name, {
			L".iso", L".cue", L".bin", L".img", L".raw", L".qcow", L".qcow2", L".qed", L".vdi", L".vmdk",
			L".vhd", L".vpc", L".vhdx", L".bochs", L".cloop", L".dmg", L".hds",
			L".parallels", L".vvfat", L".qemu_cmd_line"
		});
	}

	bool IsCdromMediaName(const std::wstring& name)
	{
		return IsDriveMediaName(name) && !HasExtension(name, { L".qemu_cmd_line" });
	}

	std::wstring Lowercase(std::wstring value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
		{
			return static_cast<wchar_t>(towlower(ch));
		});
		return value;
	}

	int RoundRelativePointerDelta(double value)
	{
		return value >= 0.0 ? static_cast<int>(value + 0.5) : static_cast<int>(value - 0.5);
	}

	int ScaleRelativePointerDelta(double deltaX, double deltaY, bool horizontal)
	{
		double distance = std::sqrt((deltaX * deltaX) + (deltaY * deltaY));
		double scale = 2.25;
		if (distance >= 6.0)
		{
			scale = 3.0;
		}
		if (distance >= 16.0)
		{
			scale = 4.0;
		}

		return RoundRelativePointerDelta((horizontal ? deltaX : deltaY) * scale);
	}

	bool ContainsText(const std::wstring& value, const wchar_t* text)
	{
		return Lowercase(value).find(Lowercase(text)) != std::wstring::npos;
	}

	bool IsFirmwareFileName(const std::wstring& name)
	{
		if (HasExtension(name, { L".txt" }))
		{
			return false;
		}

		return HasExtension(name, { L".fd", L".rom", L".bin", L".efi" }) ||
			ContainsText(name, L"bios") ||
			ContainsText(name, L"firmware") ||
			ContainsText(name, L"edk2") ||
			ContainsText(name, L"ovmf") ||
			ContainsText(name, L"uefi");
	}

	bool IsKernelFileName(const std::wstring& name)
	{
		return HasExtension(name, { L".kernel", L".elf" }) ||
			ContainsText(name, L"vmlinuz") ||
			ContainsText(name, L"bzimage") ||
			ContainsText(name, L"kernel");
	}

	bool IsInitrdFileName(const std::wstring& name)
	{
		return HasExtension(name, { L".initrd", L".cpio", L".gz", L".xz", L".lz", L".lzma" }) ||
			ContainsText(name, L"initrd") ||
			ContainsText(name, L"initramfs");
	}

	bool IsDtbFileName(const std::wstring& name)
	{
		return HasExtension(name, { L".dtb" });
	}

	std::wstring BootMediaFolderPath()
	{
		std::wstring path(ApplicationData::Current->LocalFolder->Path->Data());
		path += L"\\boot_media";
		return path;
	}

	uint64_t FileSizeFromFindData(const WIN32_FIND_DATAW& data)
	{
		return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
	}

	uint64_t DirectorySizeBytes(const std::wstring& path)
	{
		uint64_t total = 0;
		std::wstring search = path + L"\\*";
		WIN32_FIND_DATAW data = {};
		HANDLE find = FindFirstFileExFromAppW(search.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
		if (find == INVALID_HANDLE_VALUE)
		{
			return 0;
		}

		do
		{
			if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
			{
				continue;
			}

			std::wstring child = path + L"\\";
			child += data.cFileName;
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				total += DirectorySizeBytes(child);
			}
			else
			{
				total += FileSizeFromFindData(data);
			}
		} while (FindNextFileW(find, &data));

		FindClose(find);
		return total;
	}

	void DeleteDirectoryContents(const std::wstring& path)
	{
		std::wstring search = path + L"\\*";
		WIN32_FIND_DATAW data = {};
		HANDLE find = FindFirstFileExFromAppW(search.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
		if (find == INVALID_HANDLE_VALUE)
		{
			return;
		}

		do
		{
			if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
			{
				continue;
			}

			std::wstring child = path + L"\\";
			child += data.cFileName;
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				DeleteDirectoryContents(child);
				RemoveDirectoryFromAppW(child.c_str());
			}
			else
			{
				DeleteFileFromAppW(child.c_str());
			}
		} while (FindNextFileW(find, &data));

		FindClose(find);
	}

	std::wstring FormatBytes(uint64_t bytes)
	{
		const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
		double value = static_cast<double>(bytes);
		int unit = 0;
		while (value >= 1024.0 && unit < 4)
		{
			value /= 1024.0;
			unit++;
		}

		wchar_t buffer[64] = {};
		if (unit == 0)
		{
			swprintf_s(buffer, L"%llu %s", static_cast<unsigned long long>(bytes), units[unit]);
		}
		else
		{
			swprintf_s(buffer, L"%.1f %s", value, units[unit]);
		}
		return buffer;
	}

	enum class TargetBlockStyle
	{
		Ide,
		Scsi,
		VirtioMmio,
		VirtioPci,
		VirtioCcw
	};

	enum class TargetOptionScope
	{
		Any,
		I386Only,
		X86_64Only,
		X86Only
	};

	struct TargetProfile
	{
		int memoryMb;
		const wchar_t* machineArgs;
		TargetBlockStyle blockStyle;
	};

	bool IsAnyTarget(const std::wstring& target, std::initializer_list<const wchar_t*> names)
	{
		for (const wchar_t* name : names)
		{
			if (_wcsicmp(target.c_str(), name) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool OptionAppliesToTarget(TargetOptionScope scope, const std::wstring& target)
	{
		switch (scope)
		{
		case TargetOptionScope::I386Only:
			return IsAnyTarget(target, { L"i386" });
		case TargetOptionScope::X86_64Only:
			return IsAnyTarget(target, { L"x86_64" });
		case TargetOptionScope::X86Only:
			return IsAnyTarget(target, { L"i386", L"x86_64" });
		default:
			return true;
		}
	}

	bool IsX86Target(const std::wstring& target)
	{
		return IsAnyTarget(target, { L"i386", L"x86_64" });
	}

	bool TargetUsesPcCompatibilityDefaults(const std::wstring& target)
	{
		return IsX86Target(target);
	}

	bool TargetSupportsVgaOption(const std::wstring& target)
	{
		return IsX86Target(target);
	}

	bool ProfilePrefersAbsolutePointer(int profile, const std::wstring& target)
	{
		if (!TargetUsesPcCompatibilityDefaults(target))
		{
			return false;
		}
		return profile != ProfileWindows98;
	}

	TargetProfile GetTargetProfile(const std::wstring& target)
	{
		if (IsAnyTarget(target, { L"x86_64" }))
		{
			return { 1024, L"", TargetBlockStyle::Ide };
		}
		if (IsAnyTarget(target, { L"i386" }))
		{
			return { 512, L"", TargetBlockStyle::Ide };
		}
		if (IsAnyTarget(target, { L"arm" }))
		{
			return { 256, L" -M virt", TargetBlockStyle::VirtioMmio };
		}
		if (IsAnyTarget(target, { L"aarch64" }))
		{
			return { 1024, L" -M virt", TargetBlockStyle::VirtioMmio };
		}
		if (IsAnyTarget(target, { L"riscv32" }))
		{
			return { 512, L" -M virt", TargetBlockStyle::VirtioMmio };
		}
		if (IsAnyTarget(target, { L"riscv64" }))
		{
			return { 1024, L" -M virt", TargetBlockStyle::VirtioMmio };
		}
		if (IsAnyTarget(target, { L"mips", L"mipsel" }))
		{
			return { 256, L" -M malta", TargetBlockStyle::Ide };
		}
		if (IsAnyTarget(target, { L"mips64", L"mips64el" }))
		{
			return { 512, L" -M malta", TargetBlockStyle::Ide };
		}
		if (IsAnyTarget(target, { L"loongarch64" }))
		{
			return { 1024, L" -M virt", TargetBlockStyle::VirtioPci };
		}
		if (IsAnyTarget(target, { L"ppc" }))
		{
			return { 512, L" -M mac99", TargetBlockStyle::Ide };
		}
		if (IsAnyTarget(target, { L"ppc64" }))
		{
			return { 1024, L" -M pseries", TargetBlockStyle::VirtioPci };
		}
		if (IsAnyTarget(target, { L"s390x" }))
		{
			return { 1024, L" -M s390-ccw-virtio", TargetBlockStyle::VirtioCcw };
		}
		if (IsAnyTarget(target, { L"sparc" }))
		{
			return { 256, L"", TargetBlockStyle::Scsi };
		}
		if (IsAnyTarget(target, { L"sparc64" }))
		{
			return { 512, L"", TargetBlockStyle::Scsi };
		}
		if (IsAnyTarget(target, { L"m68k" }))
		{
			return { 128, L" -M q800", TargetBlockStyle::Scsi };
		}
		if (IsAnyTarget(target, { L"alpha" }))
		{
			return { 256, L"", TargetBlockStyle::Scsi };
		}
		return { 512, L"", TargetBlockStyle::Ide };
	}

	const wchar_t* BlockInterface(TargetBlockStyle style)
	{
		return style == TargetBlockStyle::Scsi ? L"scsi" : L"ide";
	}

	const wchar_t* VirtioDevice(TargetBlockStyle style)
	{
		switch (style)
		{
		case TargetBlockStyle::VirtioMmio:
			return L"virtio-blk-device";
		case TargetBlockStyle::VirtioPci:
			return L"virtio-blk-pci";
		case TargetBlockStyle::VirtioCcw:
			return L"virtio-blk-ccw";
		default:
			return L"";
		}
	}

	TargetBlockStyle EffectiveBlockStyle(TargetBlockStyle profileStyle, const std::wstring& diskInterface)
	{
		if (_wcsicmp(diskInterface.c_str(), L"virtio") == 0)
		{
			if (profileStyle == TargetBlockStyle::VirtioMmio ||
				profileStyle == TargetBlockStyle::VirtioPci ||
				profileStyle == TargetBlockStyle::VirtioCcw)
			{
				return profileStyle;
			}
			return TargetBlockStyle::VirtioPci;
		}
		if (_wcsicmp(diskInterface.c_str(), L"scsi") == 0)
		{
			return TargetBlockStyle::Scsi;
		}
		return profileStyle;
	}

	std::wstring TargetDefaultMachine(const TargetProfile& profile)
	{
		if (profile.machineArgs == nullptr || profile.machineArgs[0] == 0)
		{
			return std::wstring();
		}

		std::wstring args(profile.machineArgs);
		const std::wstring prefix = L" -M ";
		if (args.find(prefix) == 0)
		{
			return args.substr(prefix.size());
		}
		return std::wstring();
	}

	bool MachineNameIs(const std::wstring& machine, const wchar_t* name)
	{
		if (machine.empty())
		{
			return false;
		}

		size_t optionStart = machine.find(L',');
		std::wstring baseName = optionStart == std::wstring::npos ? machine : machine.substr(0, optionStart);
		return _wcsicmp(baseName.c_str(), name) == 0;
	}

	bool IsMicrovmMachine(const std::wstring& machine)
	{
		return MachineNameIs(machine, L"microvm");
	}

	bool IsVirtMachine(const std::wstring& machine)
	{
		return MachineNameIs(machine, L"virt");
	}

	bool MachineUsesVirtioMmio(const std::wstring& machine, const TargetProfile& profile)
	{
		return IsMicrovmMachine(machine) ||
			(IsVirtMachine(machine) && profile.blockStyle == TargetBlockStyle::VirtioMmio);
	}

	bool MachineSupportsPcPeripherals(const std::wstring& machine)
	{
		return !IsMicrovmMachine(machine) && !IsVirtMachine(machine);
	}

	bool MachineSupportsVgaOption(const std::wstring& machine, const std::wstring& target)
	{
		return TargetSupportsVgaOption(target) && MachineSupportsPcPeripherals(machine);
	}

	bool MachineSupportsUsbPeripherals(const std::wstring& machine)
	{
		return MachineSupportsPcPeripherals(machine);
	}

	bool MachineSupportsPciPeripherals(const std::wstring& machine, const TargetProfile& profile)
	{
		return !MachineUsesVirtioMmio(machine, profile) && !IsMicrovmMachine(machine);
	}

	bool DeviceRequiresUsb(const std::wstring& device)
	{
		std::wstring lowered = Lowercase(device);
		return lowered.find(L"usb-") != std::wstring::npos ||
			lowered.find(L"qemu-xhci") != std::wstring::npos ||
			lowered.find(L"xhci") != std::wstring::npos;
	}

	bool DeviceRequiresPci(const std::wstring& device)
	{
		std::wstring lowered = Lowercase(device);
		return lowered.find(L"-pci") != std::wstring::npos ||
			lowered.find(L"e1000") == 0 ||
			lowered.find(L"rtl8139") == 0 ||
			lowered.find(L"ne2k_pci") == 0 ||
			lowered.find(L"pcnet") == 0 ||
			lowered.find(L"vmxnet3") == 0 ||
			lowered.find(L"intel-hda") == 0 ||
			lowered.find(L"hda-") == 0;
	}

	bool DeviceRequiresPcMachine(const std::wstring& device)
	{
		std::wstring lowered = Lowercase(device);
		return lowered.find(L"pcspk") == 0;
	}

	std::wstring DefaultFirmwarePathForTarget(const std::wstring& qemuDir, const std::wstring& target, bool secure)
	{
		if (IsAnyTarget(target, { L"i386" }))
		{
			return qemuDir + (secure ? L"\\edk2-i386-secure-code.fd" : L"\\edk2-i386-code.fd");
		}
		if (IsAnyTarget(target, { L"x86_64" }))
		{
			return qemuDir + (secure ? L"\\edk2-x86_64-secure-code.fd" : L"\\edk2-x86_64-code.fd");
		}
		if (IsAnyTarget(target, { L"aarch64" }))
		{
			return qemuDir + L"\\edk2-aarch64-code.fd";
		}
		if (IsAnyTarget(target, { L"arm" }))
		{
			return qemuDir + L"\\edk2-arm-code.fd";
		}
		if (IsAnyTarget(target, { L"riscv32", L"riscv64" }))
		{
			return qemuDir + L"\\edk2-riscv-code.fd";
		}
		return std::wstring();
	}

	bool UsesDetachedDriveDevice(TargetBlockStyle style, const std::wstring& diskInterface)
	{
		return style == TargetBlockStyle::VirtioMmio ||
			style == TargetBlockStyle::VirtioPci ||
			style == TargetBlockStyle::VirtioCcw ||
			_wcsicmp(diskInterface.c_str(), L"sata") == 0;
	}

	std::wstring DriveInterfaceArgument(TargetBlockStyle style, const std::wstring& diskInterface)
	{
		if (_wcsicmp(diskInterface.c_str(), L"ide") == 0)
		{
			return L"ide";
		}
		if (_wcsicmp(diskInterface.c_str(), L"scsi") == 0)
		{
			return L"scsi";
		}
		return BlockInterface(style);
	}

	std::wstring DriveDeviceArgument(TargetBlockStyle style, const std::wstring& diskInterface, bool cdrom)
	{
		if (_wcsicmp(diskInterface.c_str(), L"sata") == 0)
		{
			return cdrom ? L"ide-cd,bus=ahci0.1,drive=" + std::wstring(CdromId) : L"ide-hd,bus=ahci0.0,drive=" + std::wstring(DriveId);
		}

		std::wstring device = VirtioDevice(style);
		if (!device.empty())
		{
			device += L",drive=";
			device += cdrom ? CdromId : DriveId;
		}
		return device;
	}

	std::wstring SharedDriveDeviceArgument(TargetBlockStyle style, const std::wstring& diskInterface)
	{
		if (_wcsicmp(diskInterface.c_str(), L"sata") == 0)
		{
			return L"ide-hd,bus=ahci0.2,drive=" + std::wstring(SharedDriveId);
		}

		std::wstring device = VirtioDevice(style);
		if (!device.empty())
		{
			device += L",drive=";
			device += SharedDriveId;
		}
		return device;
	}

	bool TryParsePort(const std::wstring& text, int fallback, int& port)
	{
		try
		{
			size_t consumed = 0;
			int parsed = std::stoi(text, &consumed);
			if (consumed == text.length() && parsed >= 1024 && parsed <= 65534)
			{
				port = parsed;
				return true;
			}
		}
		catch (...)
		{
		}
		port = fallback;
		return false;
	}

	bool IsTargetChar(unsigned char value)
	{
		return (value >= 'a' && value <= 'z') ||
			(value >= 'A' && value <= 'Z') ||
			(value >= '0' && value <= '9') ||
			value == '_';
	}

	bool ContainsTerminatedAscii(
		const std::vector<unsigned char>& data,
		const char* token,
		size_t begin,
		size_t end)
	{
		size_t length = strlen(token);
		if (length == 0 || begin >= data.size())
		{
			return false;
		}

		end = (std::min)(end, data.size());
		if (end < begin || end - begin < length)
		{
			return false;
		}

		for (size_t i = begin; i + length <= end; i++)
		{
			if (memcmp(data.data() + i, token, length) == 0)
			{
				bool beforeOk = i == 0 || data[i - 1] == 0;
				bool afterOk = i + length >= data.size() || data[i + length] == 0;
				if (beforeOk && afterOk)
				{
					return true;
				}
			}
		}

		return false;
	}

	size_t FindAscii(const std::vector<unsigned char>& data, const char* token)
	{
		size_t length = strlen(token);
		if (length == 0 || data.size() < length)
		{
			return std::wstring::npos;
		}

		for (size_t i = 0; i + length <= data.size(); i++)
		{
			if (memcmp(data.data() + i, token, length) == 0)
			{
				return i;
			}
		}

		return std::wstring::npos;
	}

	std::wstring WidenAscii(const std::string& value)
	{
		return std::wstring(value.begin(), value.end());
	}

	bool ReadBinaryFile(const std::wstring& path, std::vector<unsigned char>& data)
	{
		CREATEFILE2_EXTENDED_PARAMETERS params = {};
		params.dwSize = sizeof(params);
		HANDLE file = CreateFile2(path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &params);
		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 512LL * 1024LL * 1024LL)
		{
			CloseHandle(file);
			return false;
		}

		data.resize(static_cast<size_t>(size.QuadPart));
		DWORD read = 0;
		bool ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == static_cast<DWORD>(data.size());
		CloseHandle(file);
		return ok;
	}

	bool DataContainsAscii(const std::vector<unsigned char>& data, const char* token)
	{
		return FindAscii(data, token) != std::wstring::npos;
	}

	std::vector<std::wstring> TokenizeCommandLine(const std::wstring& commandLine)
	{
		std::vector<std::wstring> parts;
		std::wstring current;
		bool inQuotes = false;
		for (wchar_t ch : commandLine)
		{
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

	bool CommandHasOption(const std::vector<std::wstring>& tokens, const wchar_t* option)
	{
		size_t optionLength = wcslen(option);
		for (const std::wstring& token : tokens)
		{
			if (_wcsicmp(token.c_str(), option) == 0)
			{
				return true;
			}
			if (token.size() > optionLength &&
				_wcsnicmp(token.c_str(), option, optionLength) == 0 &&
				token[optionLength] == L'=')
			{
				return true;
			}
		}
		return false;
	}

	std::wstring QuoteCommandToken(const std::wstring& token)
	{
		bool needsQuote = token.empty();
		for (wchar_t ch : token)
		{
			if (iswspace(ch) || ch == L'"')
			{
				needsQuote = true;
				break;
			}
		}
		if (!needsQuote)
		{
			return token;
		}

		std::wstring quoted = L"\"";
		for (wchar_t ch : token)
		{
			if (ch == L'"')
			{
				quoted += L"\\\"";
			}
			else
			{
				quoted += ch;
			}
		}
		quoted += L"\"";
		return quoted;
	}

	bool HasExtension(const std::wstring& name, const wchar_t* extension)
	{
		size_t extensionLength = wcslen(extension);
		return name.length() >= extensionLength &&
			_wcsicmp(name.c_str() + name.length() - extensionLength, extension) == 0;
	}

	std::wstring DiskFormatFromFileName(const std::wstring& name)
	{
		if (HasExtension(name, L".qcow2"))
		{
			return L"qcow2";
		}
		if (HasExtension(name, L".qcow"))
		{
			return L"qcow";
		}
		if (HasExtension(name, L".qed"))
		{
			return L"qed";
		}
		if (HasExtension(name, L".vdi"))
		{
			return L"vdi";
		}
		if (HasExtension(name, L".vmdk"))
		{
			return L"vmdk";
		}
		if (HasExtension(name, L".vhd") || HasExtension(name, L".vpc"))
		{
			return L"vpc";
		}
		if (HasExtension(name, L".vhdx"))
		{
			return L"vhdx";
		}
		if (HasExtension(name, L".bochs"))
		{
			return L"bochs";
		}
		if (HasExtension(name, L".cloop"))
		{
			return L"cloop";
		}
		if (HasExtension(name, L".dmg"))
		{
			return L"dmg";
		}
		if (HasExtension(name, L".hds") || HasExtension(name, L".parallels"))
		{
			return L"parallels";
		}
		if (HasExtension(name, L".vvfat"))
		{
			return L"vvfat";
		}
		return L"raw";
	}

	bool PreferReadOnlyImageMode(const std::wstring& name, const std::wstring& format, bool cdromMedia)
	{
		if (cdromMedia)
		{
			return true;
		}
		return format == L"cloop" ||
			format == L"dmg" ||
			HasExtension(name, L".iso") ||
			HasExtension(name, L".cue") ||
			HasExtension(name, L".bin");
	}

	int ProfileDefaultMemoryMb(int profile)
	{
		switch (profile)
		{
		case ProfileLinuxCloudUefiQcow2:
		case ProfileWindows7:
			return 1024;
		case ProfileWindowsXp:
			return 512;
		case ProfileWindows98:
			return 256;
		default:
			return 0;
		}
	}

	struct QemuStartupProfile
	{
		const wchar_t* machine;
		const wchar_t* cpu;
		const wchar_t* device;
		const wchar_t* vga;
		const wchar_t* monitor;
		const wchar_t* netdev;
		const wchar_t* smp;
		const wchar_t* rtc;
		const wchar_t* rebootBehavior;
		const wchar_t* acpi;
		const wchar_t* hpet;
		const wchar_t* vmport;
		const wchar_t* usbBus;
		const wchar_t* usbController;
		const wchar_t* inputDevice;
		const wchar_t* networkDevice;
		const wchar_t* audioDevice;
		const wchar_t* extraArgs;
		bool uefi;
	};

	QemuStartupProfile GetQemuStartupProfile(int profile, const std::wstring& target)
	{
		if (!IsX86Target(target))
		{
			const wchar_t* smp = profile == ProfileLinuxCloudUefiQcow2 ? L"2" : L"1";
			const wchar_t* netdev = profile == ProfileLinuxCloudUefiQcow2 ? L"user,id=net0" : L"";
			bool uefi = profile == ProfileLinuxCloudUefiQcow2;
			return { L"", L"", L"", L"", L"none", netdev, smp, L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L" -serial none", uefi };
		}

		const bool i386Target = IsAnyTarget(target, { L"i386" });
		const wchar_t* genericCpu = i386Target ? L"qemu32" : L"qemu64";

		switch (profile)
		{
		case ProfileLinuxCloudUefiQcow2:
			return { L"q35", genericCpu, L"", L"std", L"none", L"user,id=net0", L"2", L"", L"", L"", L"", L"", L"on", L"", L"usb-tablet", L"e1000", L"", L" -serial none", true };
		case ProfileWindows98:
			return { L"pc", L"pentium2", L"", L"std", L"none", L"", L"2", L"base=localtime", L"", L"", L"", L"", L"", L"", L"", L"", L"", L" -serial none", false };
		case ProfileWindowsXp:
			return { L"pc", L"pentium3", L"", L"std", L"none", L"user,id=net0", L"2", L"base=localtime", L"", L"", L"", L"", L"on", L"", L"usb-tablet", L"rtl8139", L"", L" -serial none", false };
		case ProfileWindows7:
			return { L"pc", genericCpu, L"", L"std", L"none", L"user,id=net0", L"2", L"base=localtime", L"", L"", L"", L"", L"on", L"", L"usb-tablet", L"e1000", L"", L" -serial none", false };
		case ProfileXboxTcg64:
			return { L"", L"", L"", L"", L"", L"", L"2", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", false };
		case ProfileXboxTcg128:
			return { L"", L"", L"", L"", L"", L"", L"2", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", false };
		case ProfileXboxTcg256:
			return { L"", L"", L"", L"", L"", L"", L"4", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", false };
		default:
			return { L"", L"", L"", L"", L"", L"", L"2", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", false };
		}
	}

	std::wstring ProfileExtraCommandArguments(int profile, const std::wstring& qemuDir, const std::wstring& target, bool includeFirmware)
	{
		QemuStartupProfile settings = GetQemuStartupProfile(profile, target);
		std::wstring args = settings.extraArgs != nullptr ? settings.extraArgs : L"";
		if (profile == ProfileXboxTcg64 || profile == ProfileXboxTcg128 || profile == ProfileXboxTcg256)
		{
			// The base x86_64 command owns the single TCG accelerator option.
			args.clear();
		}
		if (settings.uefi && includeFirmware)
		{
			std::wstring biosPath = DefaultFirmwarePathForTarget(qemuDir, target, false);
			if (!biosPath.empty())
			{
				args += L" -bios \"";
				args += biosPath;
				args += L"\"";
			}
		}
		return args;
	}

	bool IsCoreKeyDown(VirtualKey key)
	{
		CoreVirtualKeyStates state = Window::Current->CoreWindow->GetKeyState(key);
		return (static_cast<unsigned>(state) & static_cast<unsigned>(CoreVirtualKeyStates::Down)) != 0;
	}

	bool IsControlDown()
	{
		return IsCoreKeyDown(VirtualKey::Control) ||
			IsCoreKeyDown(VirtualKey::LeftControl) ||
			IsCoreKeyDown(VirtualKey::RightControl);
	}

	bool IsAltDown()
	{
		return IsCoreKeyDown(VirtualKey::Menu) ||
			IsCoreKeyDown(VirtualKey::LeftMenu) ||
			IsCoreKeyDown(VirtualKey::RightMenu);
	}

	unsigned int GamepadButtonMask(GamepadButtons button)
	{
		return static_cast<unsigned int>(button);
	}

	bool GamepadButtonDown(unsigned int buttons, GamepadButtons button)
	{
		return (buttons & GamepadButtonMask(button)) != 0;
	}

	bool GamepadButtonPressed(unsigned int buttons, unsigned int previousButtons, GamepadButtons button)
	{
		unsigned int mask = GamepadButtonMask(button);
		return (buttons & mask) != 0 && (previousButtons & mask) == 0;
	}

	double ApplyAnalogDeadzone(double value)
	{
		const double deadzone = 0.8;
		if (std::abs(value) <= deadzone)
		{
			return 0.0;
		}

		double sign = value < 0.0 ? -1.0 : 1.0;
		return sign * ((std::abs(value) - deadzone) / (1.0 - deadzone));
	}

	std::wstring JoinSet(const std::set<std::string>& values)
	{
		if (values.empty())
		{
			return L"(none)";
		}

		std::wstring joined;
		for (const auto& value : values)
		{
			if (!joined.empty())
			{
				joined += L", ";
			}
			joined += WidenAscii(value);
		}
		return joined;
	}

	std::wstring Win32ErrorDescription(DWORD code)
	{
		LPWSTR message = nullptr;
		DWORD length = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			code,
			0,
			reinterpret_cast<LPWSTR>(&message),
			0,
			nullptr);

		std::wstring text = L"Win32 error " + std::to_wstring(code);
		if (length != 0 && message != nullptr)
		{
			text += L" (";
			text += message;
			while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
			{
				text.pop_back();
			}
			text += L")";
		}

		if (message != nullptr)
		{
			LocalFree(message);
		}
		return text;
	}

	std::vector<std::wstring> EnumeratePackagedRootDlls(const std::wstring& packagePath)
	{
		std::vector<std::wstring> names;
		std::wstring search = packagePath + L"\\*.dll";
		WIN32_FIND_DATAW data = {};
		HANDLE find = FindFirstFileExFromAppW(search.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
		if (find == INVALID_HANDLE_VALUE)
		{
			return names;
		}

		do
		{
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				names.push_back(data.cFileName);
			}
		} while (FindNextFileW(find, &data));

		FindClose(find);
		std::sort(names.begin(), names.end(), [](const std::wstring& left, const std::wstring& right)
		{
			return _wcsicmp(left.c_str(), right.c_str()) < 0;
		});
		return names;
	}

	std::wstring ProbePackagedDllLoads(const std::wstring& packagePath)
	{
		std::wstringstream report;
		report << L"Packaged DLL load probe: package root = " << packagePath << L"\r\n";
		report << L"Packaged DLL load probe: each root DLL is tested with LoadPackagedLibrary and immediately released.\r\n";
		report << L"Packaged DLL load probe: Win32 error 127 usually means an imported procedure is missing in that DLL or one of its dependencies.\r\n\r\n";

		std::vector<std::wstring> dlls = EnumeratePackagedRootDlls(packagePath);
		if (dlls.empty())
		{
			report << L"No packaged root DLLs were found.";
			return report.str();
		}

		size_t successCount = 0;
		size_t failureCount = 0;
		for (const std::wstring& dllName : dlls)
		{
			SetLastError(ERROR_SUCCESS);
			HMODULE module = LoadPackagedLibrary(dllName.c_str(), 0);
			if (module == nullptr)
			{
				DWORD code = GetLastError();
				failureCount++;
				report << L"[FAIL] " << dllName << L" -> " << Win32ErrorDescription(code) << L"\r\n";
				continue;
			}

			successCount++;
			report << L"[ OK ] " << dllName << L"\r\n";
			FreeLibrary(module);
		}

		report << L"\r\nPackaged DLL load probe: " << successCount << L" loaded, " << failureCount << L" failed, "
			<< dlls.size() << L" total.";
		return report.str();
	}

	std::wstring ProbeQemuDirectDll(const std::wstring& dllPath)
	{
		std::wstringstream report;
		report << L"Target probe: reading " << dllPath << L"\r\n";

		CREATEFILE2_EXTENDED_PARAMETERS params = {};
		params.dwSize = sizeof(params);
		HANDLE dll = CreateFile2(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &params);
		if (dll == INVALID_HANDLE_VALUE)
		{
			report << L"Target probe: failed to open selected QEMU target DLL.";
			return report.str();
		}

		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(dll, &size) || size.QuadPart <= 0 || size.QuadPart > 512LL * 1024LL * 1024LL)
		{
			CloseHandle(dll);
			report << L"Target probe: invalid selected QEMU target DLL size.";
			return report.str();
		}

		std::vector<unsigned char> data(static_cast<size_t>(size.QuadPart));
		DWORD totalRead = 0;
		DWORD chunkRead = 0;
		while (totalRead < data.size())
		{
			DWORD remaining = static_cast<DWORD>((std::min)(data.size() - totalRead, static_cast<size_t>(1024 * 1024)));
			if (!ReadFile(dll, data.data() + totalRead, remaining, &chunkRead, nullptr) || chunkRead == 0)
			{
				break;
			}
			totalRead += chunkRead;
		}
		CloseHandle(dll);

		if (totalRead != data.size())
		{
			report << L"Target probe: incomplete DLL read.";
			return report.str();
		}

		static const char* knownTargets[] =
		{
			"aarch64",
			"alpha",
			"arm",
			"i386",
			"m68k",
			"mips",
			"mips64",
			"mips64el",
			"mipsel",
			"ppc",
			"ppc64",
			"riscv32",
			"riscv64",
			"s390x",
			"sparc",
			"sparc64",
			"x86_64"
		};

		std::set<std::string> tableTargets;
		size_t marker = FindAscii(data, "unsupported architecture");
		size_t tableBegin = marker == std::wstring::npos || marker < 4096 ? 0 : marker - 4096;
		size_t tableEnd = marker == std::wstring::npos ? data.size() : marker;
		for (const char* target : knownTargets)
		{
			if (ContainsTerminatedAscii(data, target, tableBegin, tableEnd))
			{
				tableTargets.insert(target);
			}
		}

		std::set<std::string> commandStrings;
		const char* prefix = "qemu-system-";
		size_t prefixLength = strlen(prefix);
		for (size_t i = 0; i + prefixLength < data.size(); i++)
		{
			if (memcmp(data.data() + i, prefix, prefixLength) != 0)
			{
				continue;
			}

			size_t start = i + prefixLength;
			size_t end = start;
			while (end < data.size() && IsTargetChar(data[end]))
			{
				end++;
			}
			if (end > start)
			{
				std::string target(reinterpret_cast<const char*>(data.data() + start), end - start);
				if (target != "ARCH")
				{
					commandStrings.insert(target);
				}
			}
		}

		static const char* hostExportNames[] =
		{
			"qemu_host_init",
			"qemu_host_start_with_args",
			"qemu_host_run",
			"qemu_host_start",
			"qemu_host_main_loop_step",
			"qemu_host_pause",
			"qemu_host_resume",
			"qemu_host_wake_main_loop",
			"qemu_host_request_shutdown",
			"qemu_host_request_stop",
			"qemu_host_reset",
			"qemu_host_join",
			"qemu_host_cleanup",
			"qemu_host_set_log_callback",
			"qemu_host_set_video_callback",
			"qemu_host_register_video_update_callback",
			"qemu_host_set_audio_callback",
			"qemu_host_set_input_callback",
			"qemu_host_send_key_number",
			"qemu_host_send_key_qcode",
			"qemu_host_send_pointer_rel",
			"qemu_host_send_pointer_abs",
			"qemu_host_send_pointer_abs_normalized",
			"qemu_host_send_pointer_button",
			"qemu_host_is_initialized",
			"qemu_host_is_running",
			"qemu_host_pointer_is_absolute"
		};
		std::set<std::string> hostExports;
		for (const char* exportName : hostExportNames)
		{
			size_t found = FindAscii(data, exportName);
			if (found != std::wstring::npos)
			{
				hostExports.insert(exportName);
			}
		}

		report << L"QEMU host API probe: embedded architecture table = " << JoinSet(tableTargets) << L"\r\n";
		report << L"QEMU host API probe: embedded qemu-system strings = " << JoinSet(commandStrings) << L"\r\n";
		report << L"QEMU host API probe: total targets from arch table = " << tableTargets.size() << L"\r\n";
		report << L"QEMU host API probe: host embedding exports = " << JoinSet(hostExports) << L"\r\n";
		report << L"QEMU host API probe: this host uses the packaged qemu-system DLL that matches the selected QEMU target.";
		return report.str();
	}
}

DirectXPage::DirectXPage():
	m_windowVisible(true),
	m_coreInput(nullptr),
	m_selectedBootFile(nullptr),
	m_stagedBootFile(nullptr),
	m_selectedDriveFile(nullptr),
	m_selectedCdromFile(nullptr),
	m_selectedCommandFile(nullptr),
	m_stagedDriveFile(nullptr),
	m_stagedCdromFile(nullptr),
	m_stagedCommandFile(nullptr),
	m_generatedCommandFile(nullptr),
	m_selectedSharedFolder(nullptr),
	m_driveNbdListener(nullptr),
	m_cdromNbdListener(nullptr),
	m_rfbProxyListener(nullptr),
	m_driveNbdSize(0),
	m_cdromNbdSize(0),
	m_driveNbdPort(0),
	m_cdromNbdPort(0),
	m_rfbProxyPort(0),
	m_rfbInternalPort(0),
	m_rfbClientCount(0),
	m_driveNbdReadOnly(true),
	m_cdromNbdReadOnly(true),
	m_rfbStatusEnabled(false),
	m_rfbStopping(false),
	m_argumentsHelpHideTimer(nullptr),
	m_bootMediaRefreshTimer(nullptr),
	m_gamepadPollTimer(nullptr),
	m_metricsTimer(nullptr),
	m_gamepadPreviousButtons(0),
	m_virtualKeyboardVisible(false),
	m_gamepadTriggerToggleArmed(true),
	m_gamepadBumperToggleArmed(true),
	m_gamepadGuestUp(false),
	m_gamepadGuestDown(false),
	m_gamepadGuestLeft(false),
	m_gamepadGuestRight(false),
	m_gamepadGuestEnter(false),
	m_gamepadMouseLeft(false),
	m_gamepadMouseRight(false),
	m_virtualKeyboardSelectedIndex(0),
	m_bootMediaSizeRefreshActive(false),
	m_refreshingBootMediaHistory(false),
	m_refreshingDirectBootSelectors(false),
	m_refreshingQemuSelectors(false),
	m_refreshingQemuTargets(false),
	m_qemuSelectorLoadGeneration(0),
	m_updatingCommandText(false),
	m_isStarting(false),
	m_isRunning(false),
	m_isPaused(false),
	m_isShutdownPending(false),
	m_isStopPending(false),
	m_expectHostVideoFrame(false),
	m_metricsStartTick(0),
	m_metricsLastSampleTick(0),
	m_metricsLastCpuTime100ns(0),
	m_metricsLastFrameCount(0),
	m_metricsBootComplete(false),
	m_inputCaptured(true),
	m_visiblePointerCursor(ref new CoreCursor(CoreCursorType::Arrow, 0)),
	m_corePointerCaptureActive(false),
	m_ctrlDown(false),
	m_altDown(false),
	m_inputSurfaceWidth(1.0),
	m_inputSurfaceHeight(1.0),
	m_emulatorPointerX(0.0),
	m_emulatorPointerY(0.0),
	m_lastPhysicalPointerX(0.0),
	m_lastPhysicalPointerY(0.0),
	m_havePhysicalPointerPosition(false)
{
	TraceStartup(L"DirectXPage: constructor entered.");
	TraceStartup(L"DirectXPage: before InitializeComponent.");
	InitializeComponent();
	TraceStartup(L"DirectXPage: after InitializeComponent.");
	UpdateCaptureIndicators();
	m_inputSurfaceWidth = (std::max)(1.0, swapChainPanel->ActualWidth);
	m_inputSurfaceHeight = (std::max)(1.0, swapChainPanel->ActualHeight);
	m_emulatorPointerX = m_inputSurfaceWidth * 0.5;
	m_emulatorPointerY = m_inputSurfaceHeight * 0.5;

	m_argumentsHelpHideTimer = ref new DispatcherTimer();
	TimeSpan hideDelay;
	hideDelay.Duration = 10000000;
	m_argumentsHelpHideTimer->Interval = hideDelay;
	m_argumentsHelpHideTimer->Tick += ref new EventHandler<Object^>(this, &DirectXPage::ArgumentsHelpHideTimer_Tick);

	m_bootMediaRefreshTimer = ref new DispatcherTimer();
	TimeSpan bootMediaRefreshDelay;
	bootMediaRefreshDelay.Duration = 10000000;
	m_bootMediaRefreshTimer->Interval = bootMediaRefreshDelay;
	m_bootMediaRefreshTimer->Tick += ref new EventHandler<Object^>(this, &DirectXPage::BootMediaRefreshTimer_Tick);
	m_bootMediaRefreshTimer->Start();
	BuildVirtualKeyboard();
	UpdateVirtualKeyboardVisibility();

	m_gamepadPollTimer = ref new DispatcherTimer();
	TimeSpan gamepadPollDelay;
	gamepadPollDelay.Duration = 166667;
	m_gamepadPollTimer->Interval = gamepadPollDelay;
	m_gamepadPollTimer->Tick += ref new EventHandler<Object^>(this, &DirectXPage::GamepadPollTimer_Tick);
	m_gamepadPollTimer->Start();

	m_metricsTimer = ref new DispatcherTimer();
	TimeSpan metricsRefreshDelay;
	metricsRefreshDelay.Duration = 5000000;
	m_metricsTimer->Interval = metricsRefreshDelay;
	m_metricsTimer->Tick += ref new EventHandler<Object^>(this, &DirectXPage::MetricsTimer_Tick);

	RefreshBootMediaState();
	RefreshQemuTargetList();
	RefreshQemuOptionSelectors(false);

	if (architectureBox != nullptr && memoryBox != nullptr)
	{
		ComboBoxItem^ selectedArch = dynamic_cast<ComboBoxItem^>(architectureBox->SelectedItem);
		if (selectedArch != nullptr)
		{
			std::wstring target(selectedArch->Content->ToString()->Data());
			SelectComboBoxValue(memoryBox, std::to_wstring(GetTargetProfile(target).memoryMb));
		}
	}

	// Register event handlers for the page lifecycle.
	CoreWindow^ window = Window::Current->CoreWindow;

	window->VisibilityChanged +=
		ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>(this, &DirectXPage::OnVisibilityChanged);
	window->KeyDown +=
		ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &DirectXPage::OnKeyDown);
	window->KeyUp +=
		ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &DirectXPage::OnKeyUp);

	DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();

	currentDisplayInformation->DpiChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnDpiChanged);

	currentDisplayInformation->OrientationChanged +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnOrientationChanged);

	DisplayInformation::DisplayContentsInvalidated +=
		ref new TypedEventHandler<DisplayInformation^, Object^>(this, &DirectXPage::OnDisplayContentsInvalidated);

	swapChainPanel->CompositionScaleChanged += 
		ref new TypedEventHandler<SwapChainPanel^, Object^>(this, &DirectXPage::OnCompositionScaleChanged);

	swapChainPanel->SizeChanged +=
		ref new SizeChangedEventHandler(this, &DirectXPage::OnSwapChainPanelSizeChanged);

	// Device access is available at this point.
	// Device-dependent resources can be created here.
	TraceStartup(L"DirectXPage: creating DirectX device resources.");
	m_deviceResources = std::make_shared<DX::DeviceResources>();
	m_deviceResources->SetSwapChainPanel(swapChainPanel);

	// Register the SwapChainPanel for independent pointer input events.
	auto workItemHandler = ref new WorkItemHandler([this] (IAsyncAction ^)
	{
		// CoreIndependentInputSource raises pointer events for the specified device types on the thread where it is created.
		m_coreInput = swapChainPanel->CreateCoreIndependentInputSource(
			Windows::UI::Core::CoreInputDeviceTypes::Mouse |
			Windows::UI::Core::CoreInputDeviceTypes::Touch |
			Windows::UI::Core::CoreInputDeviceTypes::Pen
			);

		// Register pointer events raised on the background thread.
		m_coreInput->PointerPressed += ref new TypedEventHandler<Object^, PointerEventArgs^>(this, &DirectXPage::OnPointerPressed);
		m_coreInput->PointerMoved += ref new TypedEventHandler<Object^, PointerEventArgs^>(this, &DirectXPage::OnPointerMoved);
		m_coreInput->PointerReleased += ref new TypedEventHandler<Object^, PointerEventArgs^>(this, &DirectXPage::OnPointerReleased);
		m_coreInput->PointerCaptureLost += ref new TypedEventHandler<Object^, PointerEventArgs^>(this, &DirectXPage::OnPointerCaptureLost);
		ApplyCorePointerCaptureState();

		// Start processing input messages as they are delivered.
		m_coreInput->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
	});

	// Run the task on a dedicated high-priority background thread.
	m_inputLoopWorker = ThreadPool::RunAsync(workItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);

	TraceStartup(L"DirectXPage: creating QEMU host main.");
	m_main = std::unique_ptr<Qemu_UWP_hostMain>(new Qemu_UWP_hostMain(m_deviceResources));
	SetStatus(m_main->StatusText());
	m_main->StartRenderLoop();
	RefreshCommandLinePreview();
	TraceStartup(L"DirectXPage: constructor completed.");
}

DirectXPage::~DirectXPage()
{
	StopAllMediaNbdServers();
	if (m_gamepadPollTimer != nullptr)
	{
		m_gamepadPollTimer->Stop();
	}
	if (m_metricsTimer != nullptr)
	{
		m_metricsTimer->Stop();
	}
	// Stop rendering and event processing during destruction.
	if (m_main)
	{
		m_main->StopRenderLoop();
	}
	if (m_coreInput != nullptr)
	{
		m_coreInput->Dispatcher->StopProcessEvents();
	}
}

// Saves the current app state for suspend and termination events.
void DirectXPage::SaveInternalState(IPropertySet^ state)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->Trim();

	// Stop rendering when the app is suspended.
	m_main->StopRenderLoop();

	// Add app state save code here.
}

// Loads the current app state for resume events.
void DirectXPage::LoadInternalState(IPropertySet^ state)
{
	// Add app state load code here.

	// Start rendering when the app is resumed.
	m_main->StartRenderLoop();
}

// Manipuladores de eventos da janela.

void DirectXPage::OnVisibilityChanged(CoreWindow^ sender, VisibilityChangedEventArgs^ args)
{
	m_windowVisible = args->Visible;
	if (m_windowVisible)
	{
		m_main->StartRenderLoop();
	}
	else
	{
		m_main->StopRenderLoop();
	}
}

// Manipuladores de eventos DisplayInformation.

void DirectXPage::OnDpiChanged(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	// Note: the LogicalDpi value retrieved here may not match the app's effective DPI
	// if it is being scaled for high-resolution devices. After DPI is set in DeviceResources,
	// always retrieve it with GetDpi.
	// Consulte DeviceResources.cpp para obter mais detalhes.
	m_deviceResources->SetDpi(sender->LogicalDpi);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnOrientationChanged(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetCurrentOrientation(sender->CurrentOrientation);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnDisplayContentsInvalidated(DisplayInformation^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->ValidateDevice();
}

void DirectXPage::SelectBootButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	SelectDriveButton_Click(sender, e);
}

void DirectXPage::BootMediaRefreshTimer_Tick(Object^ sender, Object^ e)
{
	(void)sender;
	(void)e;
	UpdateBootMediaSize();
}

void DirectXPage::RefreshBootMediaState()
{
	RefreshBootMediaHistory();
	RefreshDirectBootFileSelectors();
	UpdateBootMediaSize();
}

void DirectXPage::RefreshBootMediaHistory()
{
	if (driveMediaHistoryBox == nullptr || cdromMediaHistoryBox == nullptr)
	{
		return;
	}

	std::wstring folderPath = BootMediaFolderPath();
	CreateDirectoryFromAppW(folderPath.c_str(), nullptr);

	m_refreshingBootMediaHistory = true;
	driveMediaHistoryBox->Items->Clear();
	cdromMediaHistoryBox->Items->Clear();

	std::wstring search = folderPath + L"\\*";
	WIN32_FIND_DATAW data = {};
	HANDLE find = FindFirstFileExFromAppW(search.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				continue;
			}

			std::wstring name(data.cFileName);
			std::wstring fullPath = folderPath + L"\\";
			fullPath += name;
			if (IsDriveMediaName(name))
			{
				ComboBoxItem^ item = ref new ComboBoxItem();
				item->Content = ref new String(name.c_str());
				item->Tag = ref new String(fullPath.c_str());
				driveMediaHistoryBox->Items->Append(item);
			}
			if (IsCdromMediaName(name))
			{
				ComboBoxItem^ item = ref new ComboBoxItem();
				item->Content = ref new String(name.c_str());
				item->Tag = ref new String(fullPath.c_str());
				cdromMediaHistoryBox->Items->Append(item);
			}
		} while (FindNextFileW(find, &data));
		FindClose(find);
	}

	driveMediaHistoryBox->SelectedIndex = -1;
	cdromMediaHistoryBox->SelectedIndex = -1;
	m_refreshingBootMediaHistory = false;
}

void DirectXPage::RefreshDirectBootFileSelectors()
{
	if (firmwareFileBox == nullptr || kernelFileBox == nullptr || initrdFileBox == nullptr || dtbFileBox == nullptr)
	{
		return;
	}

	m_refreshingDirectBootSelectors = true;
	firmwareFileBox->Items->Clear();
	kernelFileBox->Items->Clear();
	initrdFileBox->Items->Clear();
	dtbFileBox->Items->Clear();

	auto addDefaultItem = [](ComboBox^ comboBox)
	{
		ComboBoxItem^ item = ref new ComboBoxItem();
		item->Content = "Manual / none";
		item->Tag = "";
		comboBox->Items->Append(item);
	};

	addDefaultItem(firmwareFileBox);
	addDefaultItem(kernelFileBox);
	addDefaultItem(initrdFileBox);
	addDefaultItem(dtbFileBox);

	auto addFileItem = [](ComboBox^ comboBox, const std::wstring& source, const std::wstring& name, const std::wstring& fullPath)
	{
		ComboBoxItem^ item = ref new ComboBoxItem();
		std::wstring label = L"[";
		label += source;
		label += L"] ";
		label += name;
		item->Content = ref new String(label.c_str());
		item->Tag = ref new String(fullPath.c_str());
		comboBox->Items->Append(item);
	};

	auto selectCurrentPath = [](ComboBox^ comboBox, TextBox^ textBox)
	{
		if (comboBox == nullptr || textBox == nullptr || textBox->Text == nullptr || textBox->Text->Length() == 0)
		{
			if (comboBox != nullptr)
			{
				comboBox->SelectedIndex = 0;
			}
			return;
		}

		std::wstring currentPath(textBox->Text->Data());
		for (unsigned int index = 0; index < comboBox->Items->Size; index++)
		{
			ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(comboBox->Items->GetAt(index));
			if (item == nullptr || item->Tag == nullptr)
			{
				continue;
			}

			std::wstring path(item->Tag->ToString()->Data());
			if (_wcsicmp(path.c_str(), currentPath.c_str()) == 0)
			{
				comboBox->SelectedIndex = static_cast<int>(index);
				return;
			}
		}

		comboBox->SelectedIndex = 0;
	};

	auto scanFolder = [&](const std::wstring& folderPath, const std::wstring& source)
	{
		std::wstring search = folderPath + L"\\*";
		WIN32_FIND_DATAW data = {};
		HANDLE find = FindFirstFileExFromAppW(search.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
		if (find == INVALID_HANDLE_VALUE)
		{
			return;
		}

		do
		{
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				continue;
			}

			std::wstring name(data.cFileName);
			std::wstring fullPath = folderPath + L"\\";
			fullPath += name;
			if (IsFirmwareFileName(name))
			{
				addFileItem(firmwareFileBox, source, name, fullPath);
			}
			if (IsKernelFileName(name))
			{
				addFileItem(kernelFileBox, source, name, fullPath);
			}
			if (IsInitrdFileName(name))
			{
				addFileItem(initrdFileBox, source, name, fullPath);
			}
			if (IsDtbFileName(name))
			{
				addFileItem(dtbFileBox, source, name, fullPath);
			}
		} while (FindNextFileW(find, &data));

		FindClose(find);
	};

	std::wstring qemuPath(Package::Current->InstalledLocation->Path->Data());
	qemuPath += L"\\qemu";
	scanFolder(qemuPath, L"qemu");

	std::wstring bootMediaPath = BootMediaFolderPath();
	CreateDirectoryFromAppW(bootMediaPath.c_str(), nullptr);
	scanFolder(bootMediaPath, L"boot_media");

	selectCurrentPath(firmwareFileBox, firmwarePathBox);
	selectCurrentPath(kernelFileBox, kernelPathBox);
	selectCurrentPath(initrdFileBox, initrdPathBox);
	selectCurrentPath(dtbFileBox, dtbPathBox);
	m_refreshingDirectBootSelectors = false;
}

void DirectXPage::UpdateBootMediaSize()
{
	if (bootMediaSizeText == nullptr || m_bootMediaSizeRefreshActive)
	{
		return;
	}

	std::wstring folderPath = BootMediaFolderPath();
	CreateDirectoryFromAppW(folderPath.c_str(), nullptr);
	m_bootMediaSizeRefreshActive = true;
	auto dispatcher = Dispatcher;
	Concurrency::create_task([folderPath]()
	{
		return DirectorySizeBytes(folderPath);
	}).then([this, dispatcher](uint64_t bytes)
	{
		dispatcher->RunAsync(CoreDispatcherPriority::Low, ref new DispatchedHandler([this, bytes]()
		{
			std::wstring text = L"boot_media: ";
			text += FormatBytes(bytes);
			if (bootMediaSizeText != nullptr)
			{
				bootMediaSizeText->Text = ref new String(text.c_str());
			}
			m_bootMediaSizeRefreshActive = false;
		}));
	});
}

void DirectXPage::ApplyDetectedMediaSettings(StorageFile^ file, bool cdromMedia)
{
	if (file == nullptr || file->Name == nullptr)
	{
		UpdateMediaDetectionText();
		return;
	}

	std::wstring fileName(file->Name->Data());
	std::wstring format = DiskFormatFromFileName(fileName);
	if (cdromMedia)
	{
		SelectComboBoxValue(cdromFormatBox, format);
		if (bootDeviceBox != nullptr)
		{
			bootDeviceBox->SelectedIndex = BootDeviceCdrom;
		}
	}
	else
	{
		SelectComboBoxValue(diskFormatBox, format);
		SelectComboBoxValue(driveModeBox, PreferReadOnlyImageMode(fileName, format, false) ? L"readonly" : L"writable");
		if (bootDeviceBox != nullptr)
		{
			bootDeviceBox->SelectedIndex = BootDeviceDrive;
		}
	}

	UpdateMediaDetectionText();
}

void DirectXPage::UpdateMediaDetectionText()
{
	if (mediaDetectionText == nullptr)
	{
		return;
	}

	StorageFile^ driveFile = m_stagedDriveFile != nullptr ? m_stagedDriveFile : m_selectedDriveFile;
	StorageFile^ cdromFile = m_stagedCdromFile != nullptr ? m_stagedCdromFile : m_selectedCdromFile;
	std::wstring text = L"Detected media settings:";
	bool hasAny = false;

	if (driveFile != nullptr && driveFile->Name != nullptr)
	{
		std::wstring name(driveFile->Name->Data());
		std::wstring format = SelectedComboTag(diskFormatBox);
		if (format.empty())
		{
			format = DiskFormatFromFileName(name);
		}
		std::wstring mode = SelectedComboTag(driveModeBox);
		if (mode.empty())
		{
			mode = PreferReadOnlyImageMode(name, format, false) ? L"readonly" : L"writable";
		}
		text += L" drive=";
		text += format;
		text += L", ";
		text += mode;
		hasAny = true;
	}

	if (cdromFile != nullptr && cdromFile->Name != nullptr)
	{
		std::wstring name(cdromFile->Name->Data());
		std::wstring format = SelectedComboTag(cdromFormatBox);
		if (format.empty())
		{
			format = DiskFormatFromFileName(name);
		}
		if (hasAny)
		{
			text += L";";
		}
		text += L" cdrom=";
		text += format;
		text += L", readonly, media=cdrom";
		hasAny = true;
	}

	if (VvfatFeatureEnabled && m_selectedSharedFolder != nullptr && m_selectedSharedFolder->Path != nullptr)
	{
		if (hasAny)
		{
			text += L";";
		}
		text += L" shared-folder=vvfat, ";
		std::wstring mode = SelectedComboTag(sharedFolderModeBox);
		text += mode == L"ro" ? L"read-only" : L"read/write";
		hasAny = true;
	}

	if (!hasAny)
	{
		text += L" none";
	}

	mediaDetectionText->Text = ref new String(text.c_str());
}

bool DirectXPage::IsBootMediaFile(StorageFile^ file)
{
	if (file == nullptr || file->Path == nullptr)
	{
		return false;
	}

	std::wstring bootPath = BootMediaFolderPath();
	bootPath += L"\\";
	std::wstring filePath(file->Path->Data());
	return filePath.length() > bootPath.length() &&
		_wcsnicmp(filePath.c_str(), bootPath.c_str(), bootPath.length()) == 0;
}

void DirectXPage::SelectBootMediaHistoryItem(ComboBox^ comboBox, bool cdromMedia)
{
	if (m_refreshingBootMediaHistory || comboBox == nullptr)
	{
		return;
	}

	ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(comboBox->SelectedItem);
	if (item == nullptr || item->Tag == nullptr)
	{
		return;
	}

	String^ path = item->Tag->ToString();
	Concurrency::create_task(StorageFile::GetFileFromPathAsync(path)).then([this, cdromMedia](StorageFile^ file)
	{
		if (file == nullptr)
		{
			return;
		}

		m_selectedCommandFile = nullptr;
		if (cdromMedia)
		{
			m_selectedCdromFile = file;
			m_stagedCdromFile = nullptr;
			StopMediaNbdServer(true);
			selectedCdromText->Text = file->Path;
			ApplyDetectedMediaSettings(file, true);
			SetStatus(L"CD-ROM selected from boot_media.");
		}
		else if (IsCommandLineFile(file))
		{
			m_selectedCommandFile = file;
			m_selectedDriveFile = nullptr;
			m_selectedCdromFile = nullptr;
			m_stagedDriveFile = nullptr;
			m_stagedCdromFile = nullptr;
			StopAllMediaNbdServers();
			selectedDriveText->Text = file->Path;
			selectedCdromText->Text = "No CD-ROM selected";
			UpdateMediaDetectionText();
			SetStatus(L"qemu_cmd_line selected from boot_media.");
			Concurrency::create_task(FileIO::ReadTextAsync(file)).then([this](String^ text)
			{
				commandLineBox->Text = text;
			});
			return;
		}
		else
		{
			m_selectedDriveFile = file;
			m_stagedDriveFile = nullptr;
			StopMediaNbdServer(false);
			selectedDriveText->Text = file->Path;
			ApplyDetectedMediaSettings(file, false);
			SetStatus(L"Drive selected from boot_media.");
		}

		RefreshCommandLinePreview();
	});
}

void DirectXPage::DriveMediaHistory_SelectionChanged(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	SelectBootMediaHistoryItem(driveMediaHistoryBox, false);
}

void DirectXPage::CdromMediaHistory_SelectionChanged(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	SelectBootMediaHistoryItem(cdromMediaHistoryBox, true);
}

void DirectXPage::SelectDirectBootFile(ComboBox^ comboBox, TextBox^ textBox)
{
	if (m_refreshingDirectBootSelectors || comboBox == nullptr || textBox == nullptr)
	{
		return;
	}

	ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(comboBox->SelectedItem);
	if (item == nullptr || item->Tag == nullptr)
	{
		return;
	}

	String^ path = item->Tag->ToString();
	if (path != nullptr && path->Length() > 0)
	{
		textBox->Text = path;
	}
	RefreshCommandLinePreview();
}

void DirectXPage::DirectBootFile_SelectionChanged(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)e;
	if (sender == firmwareFileBox)
	{
		SelectDirectBootFile(firmwareFileBox, firmwarePathBox);
	}
	else if (sender == kernelFileBox)
	{
		SelectDirectBootFile(kernelFileBox, kernelPathBox);
	}
	else if (sender == initrdFileBox)
	{
		SelectDirectBootFile(initrdFileBox, initrdPathBox);
	}
	else if (sender == dtbFileBox)
	{
		SelectDirectBootFile(dtbFileBox, dtbPathBox);
	}
}

void DirectXPage::DirectBootText_Changed(Object^ sender, TextChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_refreshingDirectBootSelectors)
	{
		return;
	}
	RefreshCommandLinePreview();
}

void DirectXPage::ClearDirectBootField(ComboBox^ comboBox, TextBox^ textBox)
{
	if (comboBox != nullptr)
	{
		comboBox->SelectedIndex = 0;
	}
	if (textBox != nullptr)
	{
		textBox->Text = "";
	}
	RefreshCommandLinePreview();
}

void DirectXPage::ClearFirmwareButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	ClearDirectBootField(firmwareFileBox, firmwarePathBox);
}

void DirectXPage::ClearKernelButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	ClearDirectBootField(kernelFileBox, kernelPathBox);
}

void DirectXPage::ClearInitrdButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	ClearDirectBootField(initrdFileBox, initrdPathBox);
}

void DirectXPage::ClearDtbButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	ClearDirectBootField(dtbFileBox, dtbPathBox);
}

void DirectXPage::ClearKernelAppendButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (kernelAppendBox != nullptr)
	{
		kernelAppendBox->Text = "";
	}
	RefreshCommandLinePreview();
}

void DirectXPage::RemoveDriveButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	m_selectedBootFile = nullptr;
	m_stagedBootFile = nullptr;
	m_selectedCommandFile = nullptr;
	m_selectedDriveFile = nullptr;
	m_stagedDriveFile = nullptr;
	StopMediaNbdServer(false);
	selectedDriveText->Text = "No drive selected";
	if (driveMediaHistoryBox != nullptr)
	{
		driveMediaHistoryBox->SelectedIndex = -1;
	}
	SelectComboBoxValue(diskFormatBox, L"");
	SelectComboBoxValue(driveModeBox, L"writable");
	UpdateMediaDetectionText();
	SetStatus(L"Drive media removed from the current command.");
	RefreshCommandLinePreview();
}

void DirectXPage::RemoveCdromButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	m_selectedCdromFile = nullptr;
	m_stagedCdromFile = nullptr;
	StopMediaNbdServer(true);
	selectedCdromText->Text = "No CD-ROM selected";
	if (cdromMediaHistoryBox != nullptr)
	{
		cdromMediaHistoryBox->SelectedIndex = -1;
	}
	SelectComboBoxValue(cdromFormatBox, L"");
	UpdateMediaDetectionText();
	SetStatus(L"CD-ROM media removed from the current command.");
	RefreshCommandLinePreview();
}

void DirectXPage::SelectSharedFolderButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (!VvfatFeatureEnabled)
	{
		SetStatus(L"VVFAT shared folders are currently disabled.");
		return;
	}

	FolderPicker^ picker = ref new FolderPicker();
	picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
	picker->FileTypeFilter->Append("*");

	SetStatus(L"Opening shared folder picker...");
	Concurrency::create_task(picker->PickSingleFolderAsync()).then([this](StorageFolder^ folder)
	{
		if (folder == nullptr)
		{
			SetStatus(L"Selection canceled.");
			return;
		}

		m_selectedSharedFolder = folder;
		if (selectedSharedFolderText != nullptr)
		{
			selectedSharedFolderText->Text = folder->Path;
		}
		UpdateMediaDetectionText();
		SetStatus(L"Shared folder selected for VVFAT.");
		RefreshCommandLinePreview();
	});
}

void DirectXPage::ClearSharedFolderButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	m_selectedSharedFolder = nullptr;
	if (selectedSharedFolderText != nullptr)
	{
		selectedSharedFolderText->Text = "No shared folder selected";
	}
	UpdateMediaDetectionText();
	SetStatus(L"Shared folder removed from the current command.");
	RefreshCommandLinePreview();
}

void DirectXPage::SharedFolderMode_Changed(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (!VvfatFeatureEnabled)
	{
		return;
	}

	UpdateMediaDetectionText();
	RefreshCommandLinePreview();
}

void DirectXPage::RfbServer_Changed(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (!RfbServerEnabled())
	{
		m_rfbStatusEnabled = false;
		StopRfbProxyServer();
		UpdateRfbStatus(L"disabled");
		if (m_isRunning)
		{
			m_inputCaptured = true;
			UpdateCaptureIndicators();
			ApplyInputCaptureState();
		}
	}
	else
	{
		m_rfbStatusEnabled = true;
		m_rfbProxyPort = RfbExternalPort();
		m_rfbInternalPort = m_rfbProxyPort + 1;
		m_rfbProxyAddress = LocalRfbAddress();
		m_inputCaptured = false;
		m_main->ClearInput();
		m_havePhysicalPointerPosition = false;
		UpdateCaptureIndicators();
		ApplyInputCaptureState();
		UpdateRfbStatus(m_isRunning ? L"listening" : L"configured");
	}
	RefreshCommandLinePreview();
}

void DirectXPage::RfbPort_TextChanged(Object^ sender, TextChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (RfbServerEnabled())
	{
		m_rfbStatusEnabled = true;
		m_rfbProxyPort = RfbExternalPort();
		m_rfbInternalPort = m_rfbProxyPort + 1;
		m_rfbProxyAddress = LocalRfbAddress();
		UpdateRfbStatus(m_isRunning ? L"port changed; restart QEMU to apply" : L"configured");
	}
	RefreshCommandLinePreview();
}

void DirectXPage::ClearBootMediaButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	SetStatus(L"Clearing boot_media...");
	auto dispatcher = Dispatcher;
	Concurrency::create_task([this]()
	{
		DeleteDirectoryContents(BootMediaFolderPath());
	}).then([this, dispatcher]()
	{
		dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this]()
		{
			if (IsBootMediaFile(m_selectedDriveFile))
			{
				m_selectedDriveFile = nullptr;
				m_stagedDriveFile = nullptr;
				selectedDriveText->Text = "No drive selected";
			}
			if (IsBootMediaFile(m_selectedCdromFile))
			{
				m_selectedCdromFile = nullptr;
				m_stagedCdromFile = nullptr;
				selectedCdromText->Text = "No CD-ROM selected";
			}
			if (IsBootMediaFile(m_selectedCommandFile))
			{
				m_selectedCommandFile = nullptr;
			}
			std::wstring bootPath = BootMediaFolderPath();
			bootPath += L"\\";
			auto clearBootMediaText = [&bootPath](TextBox^ textBox)
			{
				if (textBox == nullptr || textBox->Text == nullptr)
				{
					return;
				}
				std::wstring path(textBox->Text->Data());
				if (path.length() > bootPath.length() &&
					_wcsnicmp(path.c_str(), bootPath.c_str(), bootPath.length()) == 0)
				{
					textBox->Text = "";
				}
			};
			clearBootMediaText(firmwarePathBox);
			clearBootMediaText(kernelPathBox);
			clearBootMediaText(initrdPathBox);
			clearBootMediaText(dtbPathBox);
			StopAllMediaNbdServers();
			RefreshBootMediaState();
			RefreshCommandLinePreview();
			SetStatus(L"boot_media cleared.");
		}));
	});
}

void DirectXPage::BootDevice_Changed(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	UpdateMediaDetectionText();
	RefreshCommandLinePreview();
}

void DirectXPage::FillSelector(ComboBox^ comboBox, const std::vector<std::pair<std::wstring, std::wstring>>& options, String^ emptyLabel)
{
	if (comboBox == nullptr)
	{
		return;
	}

	comboBox->Items->Clear();
	if (emptyLabel != nullptr)
	{
		ComboBoxItem^ autoItem = ref new ComboBoxItem();
		autoItem->Content = emptyLabel;
		autoItem->Tag = "";
		comboBox->Items->Append(autoItem);
	}
	for (const auto& option : options)
	{
		ComboBoxItem^ item = ref new ComboBoxItem();
		item->Content = ref new String(option.first.c_str());
		item->Tag = ref new String(option.second.c_str());
		comboBox->Items->Append(item);
	}
	comboBox->SelectedIndex = comboBox->Items->Size > 0 ? 0 : -1;
}

void DirectXPage::SelectComboBoxValue(ComboBox^ comboBox, const std::wstring& value)
{
	if (comboBox == nullptr)
	{
		return;
	}

	for (unsigned int index = 0; index < comboBox->Items->Size; index++)
	{
		ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(comboBox->Items->GetAt(index));
		if (item == nullptr || item->Tag == nullptr)
		{
			continue;
		}
		std::wstring tag(item->Tag->ToString()->Data());
		if (_wcsicmp(tag.c_str(), value.c_str()) == 0)
		{
			comboBox->SelectedIndex = static_cast<int>(index);
			return;
		}
	}

	comboBox->SelectedIndex = 0;
}

void DirectXPage::ApplyProfileSelectors(int profile)
{
	if (profile == ProfileNormalCommand || profile == ProfileVideoOnlyNoMedia)
	{
		std::wstring target = SelectedQemuTarget();
		bool absolutePointer = ProfilePrefersAbsolutePointer(profile, target);
		SelectComboBoxValue(machineSelectorBox, L"");
		SelectComboBoxValue(cpuSelectorBox, L"");
		SelectComboBoxValue(deviceSelectorBox, L"");
		SelectComboBoxValue(vgaSelectorBox, L"");
		SelectComboBoxValue(monitorSelectorBox, L"");
		SelectComboBoxValue(netdevSelectorBox, L"");
		SelectComboBoxValue(smpSelectorBox, L"2");
		SelectComboBoxValue(rtcSelectorBox, L"");
		SelectComboBoxValue(rebootBehaviorBox, L"");
		SelectComboBoxValue(acpiSelectorBox, L"");
		SelectComboBoxValue(hpetSelectorBox, L"");
		SelectComboBoxValue(vmportSelectorBox, L"");
		SelectComboBoxValue(usbBusSelectorBox, absolutePointer ? L"on" : L"");
		SelectComboBoxValue(usbControllerSelectorBox, L"");
		SelectComboBoxValue(inputDeviceSelectorBox, absolutePointer ? L"usb-tablet" : L"");
		SelectComboBoxValue(networkDeviceSelectorBox, L"");
		SelectComboBoxValue(audioDeviceSelectorBox, L"");
		return;
	}

	QemuStartupProfile settings = GetQemuStartupProfile(profile, SelectedQemuTarget());
	SelectComboBoxValue(machineSelectorBox, settings.machine != nullptr ? settings.machine : L"");
	SelectComboBoxValue(cpuSelectorBox, settings.cpu != nullptr ? settings.cpu : L"");
	SelectComboBoxValue(deviceSelectorBox, settings.device != nullptr ? settings.device : L"");
	SelectComboBoxValue(vgaSelectorBox, settings.vga != nullptr ? settings.vga : L"");
	SelectComboBoxValue(monitorSelectorBox, settings.monitor != nullptr ? settings.monitor : L"");
	SelectComboBoxValue(netdevSelectorBox, settings.netdev != nullptr ? settings.netdev : L"");
	SelectComboBoxValue(smpSelectorBox, settings.smp != nullptr ? settings.smp : L"2");
	SelectComboBoxValue(rtcSelectorBox, settings.rtc != nullptr ? settings.rtc : L"");
	SelectComboBoxValue(rebootBehaviorBox, settings.rebootBehavior != nullptr ? settings.rebootBehavior : L"");
	SelectComboBoxValue(acpiSelectorBox, settings.acpi != nullptr ? settings.acpi : L"");
	SelectComboBoxValue(hpetSelectorBox, settings.hpet != nullptr ? settings.hpet : L"");
	SelectComboBoxValue(vmportSelectorBox, settings.vmport != nullptr ? settings.vmport : L"");
	SelectComboBoxValue(usbBusSelectorBox, settings.usbBus != nullptr ? settings.usbBus : L"");
	SelectComboBoxValue(usbControllerSelectorBox, settings.usbController != nullptr ? settings.usbController : L"");
	SelectComboBoxValue(inputDeviceSelectorBox, settings.inputDevice != nullptr ? settings.inputDevice : L"");
	SelectComboBoxValue(networkDeviceSelectorBox, settings.networkDevice != nullptr ? settings.networkDevice : L"");
	SelectComboBoxValue(audioDeviceSelectorBox, settings.audioDevice != nullptr ? settings.audioDevice : L"");
}

void DirectXPage::RefreshQemuTargetList()
{
	if (architectureBox == nullptr)
	{
		return;
	}

	m_refreshingQemuTargets = true;
	architectureBox->Items->Clear();
	ComboBoxItem^ item = ref new ComboBoxItem();
	item->Content = ref new String(L"x86_64");
	architectureBox->Items->Append(item);
	architectureBox->SelectedIndex = 0;
	architectureBox->IsEnabled = false;
	m_refreshingQemuTargets = false;
}

std::wstring DirectXPage::SelectedQemuTarget()
{
	return L"x86_64";
}

std::wstring DirectXPage::SelectedQemuDllName()
{
	return L"qemu-system-x86_64.dll";
}

void DirectXPage::RefreshQemuOptionSelectors(bool showProgress)
{
	m_refreshingQemuSelectors = true;
	unsigned int generation = ++m_qemuSelectorLoadGeneration;
	std::wstring target = SelectedQemuTarget();
	std::wstring dllName = L"qemu-system-" + target + L".dll";

	std::wstring dllPath(Package::Current->InstalledLocation->Path->Data());
	dllPath += L"\\";
	dllPath += dllName;

	if (showProgress)
	{
		if (targetLoadingPanel != nullptr)
		{
			targetLoadingPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;
		}
		if (targetLoadingText != nullptr)
		{
			std::wstring text = L"Loading ";
			text += target;
			text += L" target options...";
			targetLoadingText->Text = ref new String(text.c_str());
		}
		SetStatus(L"Loading QEMU target options for " + target + L".");
	}
	if (architectureBox != nullptr)
	{
		architectureBox->IsEnabled = false;
	}
	if (machineSelectorBox != nullptr) machineSelectorBox->IsEnabled = false;
	if (cpuSelectorBox != nullptr) cpuSelectorBox->IsEnabled = false;
	if (deviceSelectorBox != nullptr) deviceSelectorBox->IsEnabled = false;
	if (vgaSelectorBox != nullptr) vgaSelectorBox->IsEnabled = false;
	if (monitorSelectorBox != nullptr) monitorSelectorBox->IsEnabled = false;
	if (netdevSelectorBox != nullptr) netdevSelectorBox->IsEnabled = false;
	if (usbControllerSelectorBox != nullptr) usbControllerSelectorBox->IsEnabled = false;
	if (inputDeviceSelectorBox != nullptr) inputDeviceSelectorBox->IsEnabled = false;
	if (networkDeviceSelectorBox != nullptr) networkDeviceSelectorBox->IsEnabled = false;
	if (audioDeviceSelectorBox != nullptr) audioDeviceSelectorBox->IsEnabled = false;

	auto machines = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
	auto cpus = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
	auto devices = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
	auto vgas = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
	auto monitors = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
	auto netdevs = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
	auto usbControllers = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
	auto inputDevices = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
	auto networkDevices = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
	auto audioDevices = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
	auto readOk = std::make_shared<bool>(false);
	auto dispatcher = Dispatcher;

	create_task([dllPath, target, machines, cpus, devices, vgas, monitors, netdevs, usbControllers, inputDevices, networkDevices, audioDevices, readOk]()
	{
		std::vector<unsigned char> data;
		*readOk = ReadBinaryFile(dllPath, data);
		std::string dllText;
		if (!data.empty())
		{
			dllText.assign(reinterpret_cast<const char*>(data.data()), data.size());
		}

		auto addIfFound = [&dllText, &target](std::vector<std::pair<std::wstring, std::wstring>>& options,
		const wchar_t* label,
		const wchar_t* value,
		const char* probe,
		TargetOptionScope scope = TargetOptionScope::Any)
		{
			if (!dllText.empty() && OptionAppliesToTarget(scope, target) && dllText.find(probe) != std::string::npos)
			{
				options.push_back({ label, value });
			}
		};

		addIfFound(*machines, L"PC i440FX", L"pc", "pc-i440fx");
		addIfFound(*machines, L"Q35", L"q35", "q35");
		addIfFound(*machines, L"MicroVM", L"microvm", "microvm");
		addIfFound(*machines, L"ISA PC", L"isapc", "isapc");
		addIfFound(*machines, L"virt", L"virt", "virt");
		addIfFound(*machines, L"Malta", L"malta", "malta");
		addIfFound(*machines, L"mac99", L"mac99", "mac99");
		addIfFound(*machines, L"pseries", L"pseries", "pseries");
		addIfFound(*machines, L"s390-ccw-virtio", L"s390-ccw-virtio", "s390-ccw-virtio");
		addIfFound(*machines, L"sun4m", L"sun4m", "sun4m");
		addIfFound(*machines, L"sun4u", L"sun4u", "sun4u");
		addIfFound(*machines, L"q800", L"q800", "q800");
		addIfFound(*machines, L"loongson3-virt", L"loongson3-virt", "loongson3-virt");

		addIfFound(*cpus, L"qemu64", L"qemu64", "qemu64", TargetOptionScope::X86_64Only);
		addIfFound(*cpus, L"qemu32", L"qemu32", "qemu32", TargetOptionScope::I386Only);
		addIfFound(*cpus, L"max", L"max", "max");
		addIfFound(*cpus, L"486", L"486", "486", TargetOptionScope::X86Only);
		addIfFound(*cpus, L"pentium", L"pentium", "pentium", TargetOptionScope::X86Only);
		addIfFound(*cpus, L"pentium2", L"pentium2", "pentium2", TargetOptionScope::X86Only);
		addIfFound(*cpus, L"pentium3", L"pentium3", "pentium3", TargetOptionScope::X86Only);
		addIfFound(*cpus, L"athlon", L"athlon", "athlon", TargetOptionScope::X86Only);
		addIfFound(*cpus, L"core2duo", L"core2duo", "core2duo", TargetOptionScope::X86_64Only);
		addIfFound(*cpus, L"Haswell", L"Haswell", "Haswell", TargetOptionScope::X86_64Only);
		addIfFound(*cpus, L"Broadwell", L"Broadwell", "Broadwell", TargetOptionScope::X86_64Only);
		addIfFound(*cpus, L"Skylake-Client", L"Skylake-Client", "Skylake-Client", TargetOptionScope::X86_64Only);
		addIfFound(*cpus, L"EPYC", L"EPYC", "EPYC", TargetOptionScope::X86_64Only);
		addIfFound(*cpus, L"cortex-a7", L"cortex-a7", "cortex-a7");
		addIfFound(*cpus, L"cortex-a15", L"cortex-a15", "cortex-a15");
		addIfFound(*cpus, L"cortex-a53", L"cortex-a53", "cortex-a53");
		addIfFound(*cpus, L"cortex-a57", L"cortex-a57", "cortex-a57");
		addIfFound(*cpus, L"cortex-a72", L"cortex-a72", "cortex-a72");
		addIfFound(*cpus, L"rv32", L"rv32", "rv32");
		addIfFound(*cpus, L"rv64", L"rv64", "rv64");
		addIfFound(*cpus, L"POWER8", L"POWER8", "POWER8");
		addIfFound(*cpus, L"POWER9", L"POWER9", "POWER9");
		addIfFound(*cpus, L"m68040", L"m68040", "m68040");
		addIfFound(*cpus, L"mips32r6-generic", L"mips32r6-generic", "mips32r6-generic");
		addIfFound(*cpus, L"I7200", L"I7200", "I7200");

		addIfFound(*devices, L"USB tablet", L"usb-tablet", "usb-tablet");
		addIfFound(*devices, L"USB keyboard", L"usb-kbd", "usb-kbd");
		addIfFound(*devices, L"e1000 NIC", L"e1000,netdev=net0", "e1000");
		addIfFound(*devices, L"rtl8139 NIC", L"rtl8139,netdev=net0", "rtl8139");
		addIfFound(*devices, L"virtio-net-pci NIC", L"virtio-net-pci,netdev=net0", "virtio-net-pci");
		addIfFound(*devices, L"virtio-net-device NIC", L"virtio-net-device,netdev=net0", "virtio-net-device");
		addIfFound(*devices, L"virtio-blk-pci", L"virtio-blk-pci", "virtio-blk-pci");
		addIfFound(*devices, L"virtio-blk-device", L"virtio-blk-device", "virtio-blk-device");
		addIfFound(*devices, L"IDE disk", L"ide-hd", "ide-hd");
		addIfFound(*devices, L"IDE CD-ROM", L"ide-cd", "ide-cd");
		addIfFound(*devices, L"qemu-xhci", L"qemu-xhci", "qemu-xhci");
		addIfFound(*devices, L"AC97 audio", L"AC97", "AC97");

		addIfFound(*usbControllers, L"qemu-xhci", L"qemu-xhci", "qemu-xhci");
		addIfFound(*usbControllers, L"USB EHCI", L"usb-ehci", "usb-ehci");
		addIfFound(*usbControllers, L"PIIX3 UHCI", L"piix3-usb-uhci", "piix3-usb-uhci");
		addIfFound(*usbControllers, L"PIIX4 UHCI", L"piix4-usb-uhci", "piix4-usb-uhci");

		addIfFound(*inputDevices, L"USB tablet", L"usb-tablet", "usb-tablet");
		addIfFound(*inputDevices, L"virtio tablet PCI", L"virtio-tablet-pci", "virtio-tablet-pci");
		addIfFound(*inputDevices, L"virtio tablet MMIO", L"virtio-tablet-device", "virtio-tablet-device");

		addIfFound(*networkDevices, L"e1000", L"e1000", "e1000");
		addIfFound(*networkDevices, L"e1000e", L"e1000e", "e1000e");
		addIfFound(*networkDevices, L"rtl8139", L"rtl8139", "rtl8139");
		addIfFound(*networkDevices, L"ne2k_pci", L"ne2k_pci", "ne2k_pci");
		addIfFound(*networkDevices, L"pcnet", L"pcnet", "pcnet");
		addIfFound(*networkDevices, L"virtio-net-pci", L"virtio-net-pci", "virtio-net-pci");
		addIfFound(*networkDevices, L"virtio-net-device", L"virtio-net-device", "virtio-net-device");
		addIfFound(*networkDevices, L"vmxnet3", L"vmxnet3", "vmxnet3");

		addIfFound(*audioDevices, L"AC97", L"AC97", "AC97");
		addIfFound(*audioDevices, L"Sound Blaster 16", L"sb16", "sb16");
		addIfFound(*audioDevices, L"Intel HDA duplex", L"intel-hda+hda-duplex", "hda-duplex");
		addIfFound(*audioDevices, L"ES1370", L"ES1370", "ES1370");
		addIfFound(*audioDevices, L"PC speaker", L"pcspk", "pcspk");

		addIfFound(*vgas, L"std", L"std", "stdvga");
		addIfFound(*vgas, L"cirrus", L"cirrus", "cirrus");
		addIfFound(*vgas, L"vmware", L"vmware", "vmware");
		addIfFound(*vgas, L"qxl", L"qxl", "qxl");
		addIfFound(*vgas, L"virtio", L"virtio", "virtio-vga");
		addIfFound(*vgas, L"bochs-display", L"bochs-display", "bochs-display");
		addIfFound(*vgas, L"ramfb", L"ramfb", "ramfb");
		addIfFound(*vgas, L"none", L"none", "none");

		addIfFound(*monitors, L"none", L"none", "none");
		addIfFound(*monitors, L"virtual console", L"vc", "vc");
		addIfFound(*monitors, L"stdio", L"stdio", "stdio");

		addIfFound(*netdevs, L"user,id=net0", L"user,id=net0", "user");
		addIfFound(*netdevs, L"socket,id=net0", L"socket,id=net0", "socket");
		addIfFound(*netdevs, L"hubport,id=net0", L"hubport,id=net0", "hubport");
		addIfFound(*netdevs, L"tap,id=net0", L"tap,id=net0", "tap");
	}).then([this, dispatcher, generation, target, dllName, showProgress, machines, cpus, devices, vgas, monitors, netdevs, usbControllers, inputDevices, networkDevices, audioDevices, readOk]()
	{
		dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, generation, target, dllName, showProgress, machines, cpus, devices, vgas, monitors, netdevs, usbControllers, inputDevices, networkDevices, audioDevices, readOk]()
		{
			if (generation != m_qemuSelectorLoadGeneration)
			{
				return;
			}

			FillSelector(machineSelectorBox, *machines, "Auto");
			FillSelector(cpuSelectorBox, *cpus, "Auto");
			FillSelector(deviceSelectorBox, *devices, "Auto");
			FillSelector(vgaSelectorBox, *vgas, "Auto");
			FillSelector(monitorSelectorBox, *monitors, "Auto");
			FillSelector(netdevSelectorBox, *netdevs, "Auto");
			FillSelector(usbControllerSelectorBox, *usbControllers, "Auto");
			FillSelector(inputDeviceSelectorBox, *inputDevices, "Auto");
			FillSelector(networkDeviceSelectorBox, *networkDevices, "Auto");
			FillSelector(audioDeviceSelectorBox, *audioDevices, "Auto");
			ApplyProfileSelectors(diagnosticProfileBox != nullptr ? diagnosticProfileBox->SelectedIndex : ProfileNormalCommand);

			if (architectureBox != nullptr)
			{
				architectureBox->IsEnabled = false;
			}
			if (machineSelectorBox != nullptr) machineSelectorBox->IsEnabled = true;
			if (cpuSelectorBox != nullptr) cpuSelectorBox->IsEnabled = true;
			if (deviceSelectorBox != nullptr) deviceSelectorBox->IsEnabled = true;
			if (vgaSelectorBox != nullptr) vgaSelectorBox->IsEnabled = true;
			if (monitorSelectorBox != nullptr) monitorSelectorBox->IsEnabled = true;
			if (netdevSelectorBox != nullptr) netdevSelectorBox->IsEnabled = true;
			if (usbControllerSelectorBox != nullptr) usbControllerSelectorBox->IsEnabled = true;
			if (inputDeviceSelectorBox != nullptr) inputDeviceSelectorBox->IsEnabled = true;
			if (networkDeviceSelectorBox != nullptr) networkDeviceSelectorBox->IsEnabled = true;
			if (audioDeviceSelectorBox != nullptr) audioDeviceSelectorBox->IsEnabled = true;
			if (targetLoadingPanel != nullptr)
			{
				targetLoadingPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
			}

			m_refreshingQemuSelectors = false;
			UpdateCommandPreview();
			if (showProgress)
			{
				SetStatus(*readOk ? L"QEMU target options loaded for " + target + L"." : L"Could not read " + dllName + L"; selectors were cleared.");
			}
		}));
	});
}

std::wstring DirectXPage::SelectedSelectorValue(ComboBox^ comboBox)
{
	if (comboBox == nullptr)
	{
		return std::wstring();
	}

	ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(comboBox->SelectedItem);
	if (item == nullptr || item->Tag == nullptr)
	{
		return std::wstring();
	}

	String^ value = item->Tag->ToString();
	return value != nullptr ? std::wstring(value->Data()) : std::wstring();
}

std::wstring DirectXPage::SelectedComboTag(ComboBox^ comboBox)
{
	if (comboBox == nullptr)
	{
		return std::wstring();
	}

	ComboBoxItem^ item = dynamic_cast<ComboBoxItem^>(comboBox->SelectedItem);
	if (item == nullptr || item->Tag == nullptr)
	{
		return std::wstring();
	}

	String^ value = item->Tag->ToString();
	return value != nullptr ? std::wstring(value->Data()) : std::wstring();
}

void DirectXPage::QemuSelector_Changed(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_refreshingQemuSelectors)
	{
		return;
	}
	RefreshCommandLinePreview();
}

void DirectXPage::ResetToStartupDefaults()
{
	m_selectedBootFile = nullptr;
	m_stagedBootFile = nullptr;
	m_selectedDriveFile = nullptr;
	m_selectedCdromFile = nullptr;
	m_selectedCommandFile = nullptr;
	m_stagedDriveFile = nullptr;
	m_stagedCdromFile = nullptr;
	m_stagedCommandFile = nullptr;
	m_selectedSharedFolder = nullptr;
	StopAllMediaNbdServers();
	StopRfbProxyServer();

	selectedDriveText->Text = "No drive selected";
	selectedCdromText->Text = "No CD-ROM selected";
	if (selectedSharedFolderText != nullptr)
	{
		selectedSharedFolderText->Text = "No shared folder selected";
	}
	if (driveMediaHistoryBox != nullptr)
	{
		driveMediaHistoryBox->SelectedIndex = -1;
	}
	if (cdromMediaHistoryBox != nullptr)
	{
		cdromMediaHistoryBox->SelectedIndex = -1;
	}
	if (bootDeviceBox != nullptr)
	{
		bootDeviceBox->SelectedIndex = BootDeviceAuto;
	}
	if (bootModeBox != nullptr)
	{
		bootModeBox->SelectedIndex = 0;
	}
	if (bootOrderBox != nullptr)
	{
		bootOrderBox->SelectedIndex = 0;
	}
	if (bootMenuBox != nullptr)
	{
		bootMenuBox->SelectedIndex = 0;
	}
	if (firmwareBox != nullptr)
	{
		firmwareBox->SelectedIndex = 0;
	}
	ClearDirectBootField(firmwareFileBox, firmwarePathBox);
	ClearDirectBootField(kernelFileBox, kernelPathBox);
	ClearDirectBootField(initrdFileBox, initrdPathBox);
	ClearDirectBootField(dtbFileBox, dtbPathBox);
	if (kernelAppendBox != nullptr)
	{
		kernelAppendBox->Text = "";
	}
	if (diskInterfaceBox != nullptr)
	{
		diskInterfaceBox->SelectedIndex = 0;
	}
	if (diskFormatBox != nullptr)
	{
		diskFormatBox->SelectedIndex = 0;
	}
	if (driveCacheBox != nullptr)
	{
		driveCacheBox->SelectedIndex = 0;
	}
	if (driveModeBox != nullptr)
	{
		driveModeBox->SelectedIndex = 0;
	}
	if (cdromFormatBox != nullptr)
	{
		cdromFormatBox->SelectedIndex = 0;
	}
	if (sharedFolderModeBox != nullptr)
	{
		sharedFolderModeBox->SelectedIndex = 0;
	}
	if (rfbServerBox != nullptr)
	{
		rfbServerBox->SelectedIndex = 0;
	}
	if (rfbPortBox != nullptr)
	{
		rfbPortBox->Text = "5900";
	}
	UpdateRfbStatus(L"disabled");
	UpdateMediaDetectionText();
	if (diagnosticProfileBox != nullptr)
	{
		diagnosticProfileBox->SelectedIndex = ProfileNormalCommand;
	}
	if (extraArgumentsBox != nullptr)
	{
		extraArgumentsBox->Text = "";
	}
	if (architectureBox != nullptr && memoryBox != nullptr)
	{
		ComboBoxItem^ selectedArch = dynamic_cast<ComboBoxItem^>(architectureBox->SelectedItem);
		if (selectedArch != nullptr)
		{
			std::wstring target(selectedArch->Content->ToString()->Data());
			SelectComboBoxValue(memoryBox, std::to_wstring(GetTargetProfile(target).memoryMb));
		}
	}
	RefreshQemuOptionSelectors(false);
	commandLineBox->Text = BuildCommandLine();
	SetStatus(L"qemu commands reset to startup defaults.");
}

void DirectXPage::ResetCommandsButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	ResetToStartupDefaults();
}

void DirectXPage::AlignCommandsButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	std::wstring command(commandLineBox->Text != nullptr ? commandLineBox->Text->Data() : L"");
	std::vector<std::wstring> tokens = TokenizeCommandLine(command);
	std::wstring aligned;
	for (const std::wstring& token : tokens)
	{
		if (!aligned.empty())
		{
			aligned += L"\r\n";
		}
		aligned += QuoteCommandToken(token);
	}

	m_updatingCommandText = true;
	commandLineBox->Text = ref new String(aligned.c_str());
	m_updatingCommandText = false;
	UpdateCommandPreview();
	SetStatus(L"qemu commands aligned vertically.");
}

void DirectXPage::CommandLineBox_TextChanged(Object^ sender, TextChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (!m_updatingCommandText)
	{
		UpdateCommandPreview();
	}
}

void DirectXPage::SelectDriveButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	FileOpenPicker^ picker = ref new FileOpenPicker();
	picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
	picker->FileTypeFilter->Append(".iso");
	picker->FileTypeFilter->Append(".cue");
	picker->FileTypeFilter->Append(".bin");
	picker->FileTypeFilter->Append(".img");
	picker->FileTypeFilter->Append(".raw");
	picker->FileTypeFilter->Append(".qcow");
	picker->FileTypeFilter->Append(".qcow2");
	picker->FileTypeFilter->Append(".qed");
	picker->FileTypeFilter->Append(".vdi");
	picker->FileTypeFilter->Append(".vmdk");
	picker->FileTypeFilter->Append(".vhd");
	picker->FileTypeFilter->Append(".vpc");
	picker->FileTypeFilter->Append(".vhdx");
	picker->FileTypeFilter->Append(".bochs");
	picker->FileTypeFilter->Append(".cloop");
	picker->FileTypeFilter->Append(".dmg");
	picker->FileTypeFilter->Append(".hds");
	picker->FileTypeFilter->Append(".parallels");
	picker->FileTypeFilter->Append(".qemu_cmd_line");

	SetStatus(L"Opening file picker...");
	Concurrency::create_task(picker->PickSingleFileAsync()).then([this](StorageFile^ file)
	{
		if (file == nullptr)
		{
			SetStatus(L"Selection canceled.");
			return;
		}

		m_selectedBootFile = file;
		m_stagedBootFile = nullptr;
		m_stagedDriveFile = nullptr;
		m_stagedCommandFile = nullptr;
		StopMediaNbdServer(false);
		if (IsCommandLineFile(file))
		{
			m_selectedCommandFile = file;
			m_selectedDriveFile = nullptr;
			m_selectedCdromFile = nullptr;
			selectedDriveText->Text = file->Path;
			selectedCdromText->Text = "No CD-ROM selected";
			UpdateMediaDetectionText();
		}
		else
		{
			m_selectedCommandFile = nullptr;
			m_selectedDriveFile = file;
			selectedDriveText->Text = file->Path;
			ApplyDetectedMediaSettings(file, false);
		}
		SetStatus(L"File selected. Review qemu_cmd_line and click Start.");

		if (IsCommandLineFile(file))
		{
			Concurrency::create_task(FileIO::ReadTextAsync(file)).then([this](String^ text)
			{
				commandLineBox->Text = text;
			});
		}
		else
		{
			RefreshCommandLinePreview();
		}
	});
}

void DirectXPage::SelectCdromButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	FileOpenPicker^ picker = ref new FileOpenPicker();
	picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
	picker->FileTypeFilter->Append(".iso");
	picker->FileTypeFilter->Append(".cue");
	picker->FileTypeFilter->Append(".bin");
	picker->FileTypeFilter->Append(".img");
	picker->FileTypeFilter->Append(".raw");
	picker->FileTypeFilter->Append(".qcow");
	picker->FileTypeFilter->Append(".qcow2");
	picker->FileTypeFilter->Append(".qed");
	picker->FileTypeFilter->Append(".vdi");
	picker->FileTypeFilter->Append(".vmdk");
	picker->FileTypeFilter->Append(".vhd");
	picker->FileTypeFilter->Append(".vpc");
	picker->FileTypeFilter->Append(".vhdx");
	picker->FileTypeFilter->Append(".bochs");
	picker->FileTypeFilter->Append(".cloop");
	picker->FileTypeFilter->Append(".dmg");
	picker->FileTypeFilter->Append(".hds");
	picker->FileTypeFilter->Append(".parallels");
	picker->FileTypeFilter->Append(".vvfat");

	SetStatus(L"Opening CD-ROM picker...");
	Concurrency::create_task(picker->PickSingleFileAsync()).then([this](StorageFile^ file)
	{
		if (file == nullptr)
		{
			SetStatus(L"Selection canceled.");
			return;
		}

		m_selectedCommandFile = nullptr;
		m_selectedCdromFile = file;
		m_stagedCdromFile = nullptr;
		StopMediaNbdServer(true);
		selectedCdromText->Text = file->Path;
		ApplyDetectedMediaSettings(file, true);
		SetStatus(L"CD-ROM selected. Review qemu_cmd_line and click Start.");
		RefreshCommandLinePreview();
	});
}

void DirectXPage::StartButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	if (m_isStarting)
	{
		AppendError(L"Start ignored: the emulator is still starting.");
		tabPanel->SelectedIndex = 1;
		return;
	}
	if (m_isRunning)
	{
		AppendError(L"Start ignored: the emulator is already running.");
		tabPanel->SelectedIndex = 1;
		return;
	}
	int diagnosticProfile = diagnosticProfileBox != nullptr ? diagnosticProfileBox->SelectedIndex : 0;
	bool profileAllowsNoMedia = diagnosticProfile == ProfileVideoOnlyNoMedia;
	bool hasSharedFolderMedia = VvfatFeatureEnabled && m_selectedSharedFolder != nullptr;
	if (!profileAllowsNoMedia && m_selectedCommandFile == nullptr && m_selectedDriveFile == nullptr && m_selectedCdromFile == nullptr && !hasSharedFolderMedia)
	{
		AppendError(L"No drive, CD-ROM, shared folder, or qemu_cmd_line file selected.");
		tabPanel->SelectedIndex = 1;
		return;
	}

	std::wstring preflightReport;
	if (!ValidateStartConfiguration(preflightReport))
	{
		AppendError(L"Preflight failed: " + preflightReport);
		SetStatus(L"Preflight failed. Review Errors before starting.");
		tabPanel->SelectedIndex = 1;
		return;
	}
	AppendError(L"Preflight passed: " + preflightReport);
	SetStatus(L"Preflight passed. Preparing QEMU media...");

	SetStartState(true, false);
	StageBootFileAndStart();
}

void DirectXPage::StopButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;

	if (!m_isStarting && !m_isRunning)
	{
		SetStatus(L"Stop ignored: QEMU is not running.");
		return;
	}

	SetStatus(L"Stopping QEMU...");
	AppendError(L"Stop: requesting QEMU stop.");
	m_isStopPending = true;
	UpdateVmControlButtons();
	m_main->StopCore();
	SetStatus(m_main->StatusText());
}

void DirectXPage::PauseButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;

	std::wstring error;
	if (!m_main->PauseCore(&error))
	{
		if (!error.empty())
		{
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
		}
		return;
	}

	m_isPaused = true;
	UpdateVmControlButtons();
	SetStatus(L"QEMU pause requested.");
}

void DirectXPage::ResumeButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;

	std::wstring error;
	if (!m_main->ResumeCore(&error))
	{
		if (!error.empty())
		{
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
		}
		return;
	}

	m_isPaused = false;
	UpdateVmControlButtons();
	SetStatus(L"QEMU resume requested.");
}

void DirectXPage::ShutdownButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;

	std::wstring error;
	if (!m_main->ShutdownCore(&error))
	{
		if (!error.empty())
		{
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
		}
		return;
	}

	AppendError(L"Shutdown: QEMU shutdown requested.");
	m_isShutdownPending = true;
	UpdateVmControlButtons();
	SetStatus(L"QEMU shutdown requested.");
}

void DirectXPage::WriteCommandLineAndStart(String^ commandLine)
{
	if (commandLine == nullptr || commandLine->Length() == 0)
	{
		AppendError(L"qemu_cmd_line is empty. Select media or review the boot options.");
		tabPanel->SelectedIndex = 1;
		SetStartState(false, false);
		return;
	}

	SetStatus(L"Generating current.qemu_cmd_line...");
	AppendError(L"Start: generating current.qemu_cmd_line.");
	std::wstring commandText(commandLine->Data());
	m_expectHostVideoFrame = commandText.find(L"-display host") != std::wstring::npos;
	if (RfbServerEnabled() && !EnsureRfbProxyServer())
	{
		SetStatus(L"RFB server could not be started.");
		tabPanel->SelectedIndex = 1;
		SetStartState(false, false);
		return;
	}
	std::wstring commandLog = L"Start: qemu_cmd_line: ";
	commandLog += commandText;
	AppendError(commandLog);
	auto createFileTask = ApplicationData::Current->LocalFolder->CreateFileAsync(
		"current.qemu_cmd_line",
		CreationCollisionOption::ReplaceExisting);

	Concurrency::create_task(createFileTask).then([this, commandLine](Concurrency::task<StorageFile^> createTask)
	{
		try
		{
			m_generatedCommandFile = createTask.get();
			return Concurrency::create_task(FileIO::WriteTextAsync(m_generatedCommandFile, commandLine));
		}
		catch (Exception^ ex)
		{
			std::wstring error = L"Failed to create current.qemu_cmd_line: ";
			error += ex->Message->Data();
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
			SetStartState(false, false);
			return Concurrency::task_from_result();
		}
	}).then([this](Concurrency::task<void> writeTask)
	{
		try
		{
			writeTask.get();
			if (!m_isStarting)
			{
				return;
			}
			AppendError(L"Start: current.qemu_cmd_line written. Loading QEMU DLL.");
			StartWithCommandFile(m_generatedCommandFile);
		}
		catch (Exception^ ex)
		{
			std::wstring error = L"Failed to write current.qemu_cmd_line: ";
			error += ex->Message->Data();
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
			SetStartState(false, false);
		}
	});
}

void DirectXPage::StageBootFileAndStart()
{
	SetStatus(L"Preparing media in the app local storage...");
	AppendError(L"Start: preparing media in LocalFolder\\boot_media.");
	m_stagedCommandFile = nullptr;
	m_stagedDriveFile = nullptr;
	m_stagedCdromFile = nullptr;

	Concurrency::create_task(ApplicationData::Current->LocalFolder->CreateFolderAsync(
		"boot_media",
		CreationCollisionOption::OpenIfExists)).then([this](Concurrency::task<StorageFolder^> folderTask)
	{
		try
		{
			StorageFolder^ folder = folderTask.get();
			if (m_selectedCommandFile != nullptr)
			{
				auto commandTask = IsBootMediaFile(m_selectedCommandFile)
					? Concurrency::task_from_result<StorageFile^>(m_selectedCommandFile)
					: Concurrency::create_task(m_selectedCommandFile->CopyAsync(
						folder,
						m_selectedCommandFile->Name,
						NameCollisionOption::ReplaceExisting));

				return commandTask.then([this](StorageFile^ file)
				{
					m_stagedCommandFile = file;
				});
			}

			auto driveTask = m_selectedDriveFile != nullptr
				? (IsBootMediaFile(m_selectedDriveFile)
					? Concurrency::task_from_result<StorageFile^>(m_selectedDriveFile)
					: Concurrency::create_task(m_selectedDriveFile->CopyAsync(folder, m_selectedDriveFile->Name, NameCollisionOption::ReplaceExisting)))
				: Concurrency::task_from_result<StorageFile^>(nullptr);

			return driveTask.then([this, folder](StorageFile^ driveFile)
			{
				m_stagedDriveFile = driveFile;
				auto cdromTask = m_selectedCdromFile != nullptr
					? (IsBootMediaFile(m_selectedCdromFile)
						? Concurrency::task_from_result<StorageFile^>(m_selectedCdromFile)
						: Concurrency::create_task(m_selectedCdromFile->CopyAsync(folder, m_selectedCdromFile->Name, NameCollisionOption::ReplaceExisting)))
					: Concurrency::task_from_result<StorageFile^>(nullptr);

				return cdromTask.then([this](StorageFile^ cdromFile)
				{
					m_stagedCdromFile = cdromFile;
				});
			});
		}
		catch (Exception^ ex)
		{
			std::wstring error = L"Failed to prepare boot_media folder: ";
			error += ex->Message->Data();
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
			SetStartState(false, false);
			return Concurrency::task_from_result();
		}
	}).then([this](Concurrency::task<void> stageTask)
	{
		try
		{
			stageTask.get();
			if (m_stagedCommandFile != nullptr)
			{
				std::wstring staged = L"Start: qemu_cmd_line prepared at ";
				staged += m_stagedCommandFile->Path->Data();
				AppendError(staged);
				RefreshBootMediaState();
				StartWithCommandFile(m_stagedCommandFile);
				return;
			}

			int diagnosticProfile = diagnosticProfileBox != nullptr ? diagnosticProfileBox->SelectedIndex : 0;
			bool profileAllowsNoMedia = diagnosticProfile == ProfileVideoOnlyNoMedia;
			bool hasSharedFolderMedia = VvfatFeatureEnabled && m_selectedSharedFolder != nullptr;
			if (!profileAllowsNoMedia && m_stagedDriveFile == nullptr && m_stagedCdromFile == nullptr && !hasSharedFolderMedia)
			{
				AppendError(L"Start: no media was prepared.");
				SetStartState(false, false);
				return;
			}

			if (m_stagedDriveFile != nullptr)
			{
				std::wstring staged = L"Start: drive prepared at ";
				staged += m_stagedDriveFile->Path->Data();
				AppendError(staged);
			}
			if (m_stagedCdromFile != nullptr)
			{
				std::wstring staged = L"Start: CD-ROM prepared at ";
				staged += m_stagedCdromFile->Path->Data();
				AppendError(staged);
			}
			if (VvfatFeatureEnabled && m_selectedSharedFolder != nullptr && m_selectedSharedFolder->Path != nullptr)
			{
				std::wstring selected = L"Start: VVFAT shared folder selected at ";
				selected += m_selectedSharedFolder->Path->Data();
				AppendError(selected);
			}
			RefreshBootMediaState();

			commandLineBox->Text = BuildCommandLine();
			std::wstring profile = L"Start: profile: ";
			ComboBoxItem^ selectedProfile = diagnosticProfileBox != nullptr ? dynamic_cast<ComboBoxItem^>(diagnosticProfileBox->SelectedItem) : nullptr;
			profile += selectedProfile != nullptr ? selectedProfile->Content->ToString()->Data() : L"Normal command";
			AppendError(profile);
			WriteCommandLineAndStart(commandLineBox->Text);
		}
		catch (Exception^ ex)
		{
			std::wstring error = L"Failed to copy boot file to LocalFolder: ";
			error += ex->Message->Data();
			AppendError(error);
			SetStatus(error);
			tabPanel->SelectedIndex = 1;
			SetStartState(false, false);
		}
	});
}

void DirectXPage::ResetButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	m_main->ResetCore();
	SetStatus(L"Reset sent to core.");
}

void DirectXPage::ShowTabsButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	if (topPanel->Visibility == Windows::UI::Xaml::Visibility::Visible)
	{
		topPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
		showTabsButton->Label = "Show settings";
	}
	else
	{
		topPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;
		showTabsButton->Label = "Hide settings";
	}
}

void DirectXPage::DetectTargetsButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;

	std::wstring dllName = SelectedQemuDllName();
	SetStatus(L"Inspecting packaged " + dllName + L"...");
	if (detectTargetsButton != nullptr)
	{
		detectTargetsButton->IsEnabled = false;
	}

	std::wstring dllPath(Package::Current->InstalledLocation->Path->Data());
	dllPath += L"\\";
	dllPath += dllName;
	auto result = std::make_shared<std::wstring>();
	auto dispatcher = Dispatcher;
	create_task([dllPath, result]()
	{
		*result = ProbeQemuDirectDll(dllPath);
	}).then([this, dispatcher, result]()
	{
		dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, result]()
		{
			AppendError(*result);
			SetStatus(L"QEMU DLL inspection completed.");
			tabPanel->SelectedIndex = 1;
			if (detectTargetsButton != nullptr)
			{
				detectTargetsButton->IsEnabled = true;
			}
		}));
	});
}

void DirectXPage::TestPackagedDllsButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;

	SetStatus(L"Testing packaged DLL loads...");
	if (testPackagedDllsButton != nullptr)
	{
		testPackagedDllsButton->IsEnabled = false;
	}

	std::wstring packagePath(Package::Current->InstalledLocation->Path->Data());
	auto result = std::make_shared<std::wstring>();
	auto dispatcher = Dispatcher;
	create_task([packagePath, result]()
	{
		*result = ProbePackagedDllLoads(packagePath);
	}).then([this, dispatcher, result]()
	{
		dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, result]()
		{
			AppendError(*result);
			SetStatus(L"Packaged DLL load test completed.");
			tabPanel->SelectedIndex = 1;
			if (testPackagedDllsButton != nullptr)
			{
				testPackagedDllsButton->IsEnabled = true;
			}
		}));
	});
}

void DirectXPage::ClearErrorsButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	errorLogBox->Text = "No errors recorded.";
	SetStatus(L"Error log cleared.");
}

void DirectXPage::UpdateCommandLineButton_Click(Object^ sender, RoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	commandLineBox->Text = BuildCommandLine();
	SetStatus(L"qemu commands updated with additional arguments.");
}

void DirectXPage::ArgumentsHelpButton_PointerEntered(Object^ sender, PointerRoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_argumentsHelpHideTimer != nullptr)
	{
		m_argumentsHelpHideTimer->Stop();
	}
	if (argumentsHelpPanel != nullptr)
	{
		argumentsHelpPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;
	}
}

void DirectXPage::ArgumentsHelpButton_PointerExited(Object^ sender, PointerRoutedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_argumentsHelpHideTimer != nullptr)
	{
		m_argumentsHelpHideTimer->Stop();
		m_argumentsHelpHideTimer->Start();
	}
}

void DirectXPage::ArgumentsHelpHideTimer_Tick(Object^ sender, Object^ e)
{
	(void)sender;
	(void)e;
	if (m_argumentsHelpHideTimer != nullptr)
	{
		m_argumentsHelpHideTimer->Stop();
	}
	if (argumentsHelpPanel != nullptr)
	{
		argumentsHelpPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	}
}

void DirectXPage::BootOption_Changed(Object^ sender, RoutedEventArgs^ e)
{
	RefreshCommandLinePreview();
}

void DirectXPage::BootOptionText_Changed(Object^ sender, TextChangedEventArgs^ e)
{
	RefreshCommandLinePreview();
}

void DirectXPage::Architecture_Changed(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;

	if (architectureBox == nullptr || memoryBox == nullptr)
	{
		return;
	}
	if (m_refreshingQemuTargets)
	{
		return;
	}

	ComboBoxItem^ selectedArch = dynamic_cast<ComboBoxItem^>(architectureBox->SelectedItem);
	if (selectedArch != nullptr)
	{
		std::wstring target(selectedArch->Content->ToString()->Data());
		TargetProfile profile = GetTargetProfile(target);
		SelectComboBoxValue(memoryBox, std::to_wstring(profile.memoryMb));
	}

	RefreshQemuOptionSelectors(true);
	RefreshCommandLinePreview();
}

void DirectXPage::DiagnosticProfile_Changed(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (diagnosticProfileBox != nullptr && memoryBox != nullptr)
	{
		int memoryMb = ProfileDefaultMemoryMb(diagnosticProfileBox->SelectedIndex);
		if (memoryMb > 0)
		{
			SelectComboBoxValue(memoryBox, std::to_wstring(memoryMb));
		}
	}
	m_refreshingQemuSelectors = true;
	ApplyProfileSelectors(diagnosticProfileBox != nullptr ? diagnosticProfileBox->SelectedIndex : ProfileNormalCommand);
	m_refreshingQemuSelectors = false;
	RefreshCommandLinePreview();
}

void DirectXPage::MemoryBox_SelectionChanged(Object^ sender, SelectionChangedEventArgs^ e)
{
	(void)sender;
	(void)e;
	if (m_refreshingQemuSelectors) return;
	RefreshCommandLinePreview();
}

void DirectXPage::OnKeyDown(CoreWindow^ sender, KeyEventArgs^ args)
{
	VirtualKey keyCode = args->VirtualKey;
	m_ctrlDown = IsControlDown();
	m_altDown = IsAltDown();
	if (keyCode == VirtualKey::Control || keyCode == VirtualKey::LeftControl || keyCode == VirtualKey::RightControl)
	{
		m_ctrlDown = true;
	}
	if (keyCode == VirtualKey::Menu || keyCode == VirtualKey::LeftMenu || keyCode == VirtualKey::RightMenu)
	{
		m_altDown = true;
	}
	if (keyCode == VirtualKey::M && (m_ctrlDown || IsControlDown()) && (m_altDown || IsAltDown()))
	{
		if (!RfbServerEnabled())
		{
			ToggleInputCapture();
		}
		else
		{
			SetStatus(L"RFB mode uses the VNC client for mouse and keyboard input.");
		}
		args->Handled = true;
		return;
	}
	if (!m_isRunning || !m_inputCaptured)
	{
		return;
	}

	unsigned key = MapVirtualKeyToQemuKey(args->VirtualKey);
	if (key != 0)
	{
		m_main->SetKey(key, true);
		args->Handled = true;
	}
}

void DirectXPage::OnKeyUp(CoreWindow^ sender, KeyEventArgs^ args)
{
	VirtualKey keyCode = args->VirtualKey;
	if (keyCode == VirtualKey::Control || keyCode == VirtualKey::LeftControl || keyCode == VirtualKey::RightControl)
	{
		m_ctrlDown = IsControlDown();
	}
	if (keyCode == VirtualKey::Menu || keyCode == VirtualKey::LeftMenu || keyCode == VirtualKey::RightMenu)
	{
		m_altDown = IsAltDown();
	}
	if (!m_isRunning || !m_inputCaptured)
	{
		return;
	}

	unsigned key = MapVirtualKeyToQemuKey(args->VirtualKey);
	if (key != 0)
	{
		m_main->SetKey(key, false);
		args->Handled = true;
	}
}

void DirectXPage::OnPointerPressed(Object^ sender, PointerEventArgs^ e)
{
	ApplyCorePointerCaptureState();
	if (!m_isRunning || !m_inputCaptured)
	{
		return;
	}

	SendPointerToCore(e);
}

void DirectXPage::OnPointerMoved(Object^ sender, PointerEventArgs^ e)
{
	ApplyCorePointerCaptureState();
	if (!m_isRunning || !m_inputCaptured)
	{
		return;
	}

	SendPointerToCore(e);
}

void DirectXPage::OnPointerReleased(Object^ sender, PointerEventArgs^ e)
{
	ApplyCorePointerCaptureState();
	if (!m_isRunning || !m_inputCaptured)
	{
		return;
	}

	SendPointerToCore(e);
}

void DirectXPage::OnPointerCaptureLost(Object^ sender, PointerEventArgs^ e)
{
	m_corePointerCaptureActive = false;
	m_havePhysicalPointerPosition = false;
	ApplyCorePointerCaptureState();
}

void DirectXPage::OnCompositionScaleChanged(SwapChainPanel^ sender, Object^ args)
{
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetCompositionScale(sender->CompositionScaleX, sender->CompositionScaleY);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::OnSwapChainPanelSizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
	m_inputSurfaceWidth = (std::max)(1.0, static_cast<double>(e->NewSize.Width));
	m_inputSurfaceHeight = (std::max)(1.0, static_cast<double>(e->NewSize.Height));
	m_emulatorPointerX = (std::max)(0.0, (std::min)(m_inputSurfaceWidth, m_emulatorPointerX));
	m_emulatorPointerY = (std::max)(0.0, (std::min)(m_inputSurfaceHeight, m_emulatorPointerY));

	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetLogicalSize(e->NewSize);
	m_main->CreateWindowSizeDependentResources();
}

void DirectXPage::MetricsTimer_Tick(Object^ sender, Object^ e)
{
	(void)sender;
	(void)e;
	UpdatePerformanceMetrics();
}

void DirectXPage::ResetPerformanceMetrics()
{
	m_metricsStartTick = GetTickCount64();
	m_metricsLastSampleTick = m_metricsStartTick;
	m_metricsLastCpuTime100ns = 0;
	m_metricsLastFrameCount = m_main != nullptr ? m_main->VideoFrameCount() : 0;
	m_metricsBootComplete = false;
	if (bootMetricText != nullptr) bootMetricText->Text = "Measuring...";
	if (ramMetricText != nullptr) ramMetricText->Text = "-- MB";
	if (fpsMetricText != nullptr) fpsMetricText->Text = "0.0";
	if (cpuMetricText != nullptr) cpuMetricText->Text = "0.0%";
	if (metricsOverlay != nullptr) metricsOverlay->Visibility = Windows::UI::Xaml::Visibility::Visible;
	if (m_metricsTimer != nullptr) m_metricsTimer->Start();
}

void DirectXPage::UpdatePerformanceMetrics()
{
	if ((!m_isStarting && !m_isRunning) || m_main == nullptr)
	{
		return;
	}

	uint64_t now = GetTickCount64();
	uint64_t elapsedMs = now >= m_metricsLastSampleTick ? now - m_metricsLastSampleTick : 0;
	unsigned int frameCount = m_main->VideoFrameCount();
	unsigned int frameDelta = frameCount >= m_metricsLastFrameCount
		? frameCount - m_metricsLastFrameCount
		: frameCount;
	if (elapsedMs > 0 && fpsMetricText != nullptr)
	{
		double fps = static_cast<double>(frameDelta) * 1000.0 / static_cast<double>(elapsedMs);
		std::wstringstream fpsText;
		fpsText << std::fixed << std::setprecision(1) << fps;
		fpsMetricText->Text = ref new String(fpsText.str().c_str());
	}
	m_metricsLastFrameCount = frameCount;
	m_metricsLastSampleTick = now;

	uint64_t firstFrameTick = m_main->FirstVideoFrameTick();
	if (!m_metricsBootComplete && firstFrameTick >= m_metricsStartTick && firstFrameTick != 0)
	{
		double bootSeconds = static_cast<double>(firstFrameTick - m_metricsStartTick) / 1000.0;
		std::wstringstream bootText;
		bootText << std::fixed << std::setprecision(2) << bootSeconds << L" s";
		if (bootMetricText != nullptr) bootMetricText->Text = ref new String(bootText.str().c_str());
		m_metricsBootComplete = true;
	}
	else if (!m_metricsBootComplete && bootMetricText != nullptr)
	{
		double waitingSeconds = static_cast<double>(now - m_metricsStartTick) / 1000.0;
		std::wstringstream waitingText;
		waitingText << std::fixed << std::setprecision(1) << waitingSeconds << L" s...";
		bootMetricText->Text = ref new String(waitingText.str().c_str());
	}

	try
	{
		ProcessDiagnosticInfo^ processInfo = ProcessDiagnosticInfo::GetForCurrentProcess();
		ProcessMemoryUsageReport^ memoryReport = processInfo->MemoryUsage->GetReport();
		double memoryMb = static_cast<double>(memoryReport->WorkingSetSizeInBytes) / (1024.0 * 1024.0);
		std::wstringstream memoryText;
		memoryText << std::fixed << std::setprecision(1) << memoryMb << L" MB";
		if (ramMetricText != nullptr) ramMetricText->Text = ref new String(memoryText.str().c_str());

		ProcessCpuUsageReport^ cpuReport = processInfo->CpuUsage->GetReport();
		uint64_t cpuTime100ns = static_cast<uint64_t>(cpuReport->KernelTime.Duration + cpuReport->UserTime.Duration);
		if (m_metricsLastCpuTime100ns != 0 && elapsedMs > 0 && cpuTime100ns >= m_metricsLastCpuTime100ns)
		{
			unsigned int processorCount = (std::max)(1u, std::thread::hardware_concurrency());
			double cpuPercent = static_cast<double>(cpuTime100ns - m_metricsLastCpuTime100ns) /
				(static_cast<double>(elapsedMs) * 10000.0 * static_cast<double>(processorCount)) * 100.0;
			cpuPercent = (std::max)(0.0, (std::min)(100.0, cpuPercent));
			std::wstringstream cpuText;
			cpuText << std::fixed << std::setprecision(1) << cpuPercent << L"%";
			if (cpuMetricText != nullptr) cpuMetricText->Text = ref new String(cpuText.str().c_str());
		}
		m_metricsLastCpuTime100ns = cpuTime100ns;
	}
	catch (Exception^)
	{
		if (ramMetricText != nullptr) ramMetricText->Text = "Unavailable";
		if (cpuMetricText != nullptr) cpuMetricText->Text = "Unavailable";
	}
}

void DirectXPage::SetStartState(bool starting, bool running)
{
	bool startingNewRun = starting && !m_isStarting && !m_isRunning;
	if (startingNewRun)
	{
		ResetPerformanceMetrics();
	}
	m_isStarting = starting;
	m_isRunning = running;
	if (running)
	{
		m_inputCaptured = !RfbServerEnabled();
		m_havePhysicalPointerPosition = false;
		if (!m_inputCaptured)
		{
			m_main->ClearInput();
		}
	}
	if (!running)
	{
		m_isPaused = false;
		m_isShutdownPending = false;
		m_isStopPending = false;
		m_virtualKeyboardVisible = false;
		ReleaseGamepadGuestKeys();
	}
	if (!starting && !running)
	{
		StopRfbProxyServer();
		if (m_metricsTimer != nullptr) m_metricsTimer->Stop();
		if (metricsOverlay != nullptr) metricsOverlay->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	}
	else
	{
		if (m_metricsTimer != nullptr) m_metricsTimer->Start();
		if (metricsOverlay != nullptr) metricsOverlay->Visibility = Windows::UI::Xaml::Visibility::Visible;
	}
	if (startButton != nullptr)
	{
		startButton->IsEnabled = !starting && !running;
		if (starting)
		{
			startButton->Label = "Starting";
		}
		else if (running)
		{
			startButton->Label = "Running";
		}
		else
		{
			startButton->Label = "Start";
		}
	}
	UpdateVmControlButtons();
	UpdateCaptureIndicators();
	UpdateRfbRuntimeOverlay();
	UpdateVirtualKeyboardVisibility();
	ApplyInputCaptureState();
}

void DirectXPage::UpdateVmControlButtons()
{
	const bool canControl = m_isRunning && !m_isStopPending;
	const bool canChangeRunState = canControl && !m_isShutdownPending;
	if (pauseButton != nullptr)
	{
		pauseButton->IsEnabled = canChangeRunState && !m_isPaused;
	}
	if (resumeButton != nullptr)
	{
		resumeButton->IsEnabled = canChangeRunState && m_isPaused;
	}
	if (stopButton != nullptr)
	{
		stopButton->IsEnabled = m_isRunning && !m_isStopPending;
	}
	if (shutdownButton != nullptr)
	{
		shutdownButton->IsEnabled = canControl && !m_isShutdownPending;
	}
}

void DirectXPage::SetStatus(const std::wstring& text)
{
	statusText->Text = ref new String(text.c_str());
}

void DirectXPage::ToggleInputCapture()
{
	if (RfbServerEnabled())
	{
		m_inputCaptured = false;
		m_main->ClearInput();
		ReleaseGamepadGuestKeys();
		m_virtualKeyboardVisible = false;
		m_havePhysicalPointerPosition = false;
		UpdateCaptureIndicators();
		UpdateVirtualKeyboardVisibility();
		ApplyInputCaptureState();
		SetStatus(L"RFB mode uses the VNC client for mouse and keyboard input.");
		return;
	}
	m_inputCaptured = !m_inputCaptured;
	m_main->ClearInput();
	ReleaseGamepadGuestKeys();
	if (!m_inputCaptured)
	{
		m_virtualKeyboardVisible = false;
	}
	m_havePhysicalPointerPosition = false;
	UpdateCaptureIndicators();
	UpdateVirtualKeyboardVisibility();
	ApplyInputCaptureState();
	if (m_inputCaptured)
	{
		FocusEmulatorSurface();
	}
	SetStatus(m_inputCaptured
		? L"Input captured by the emulator. Ctrl+Alt+M releases mouse and keyboard."
		: L"Input released to the interface. Ctrl+Alt+M captures mouse and keyboard.");
}

void DirectXPage::UpdateCaptureIndicators()
{
	Windows::UI::Xaml::Visibility state = (m_isRunning && m_inputCaptured)
		? Windows::UI::Xaml::Visibility::Visible
		: Windows::UI::Xaml::Visibility::Collapsed;
	bool controlsCanTakeKeyboard = !(m_isRunning && m_inputCaptured);
	if (showTabsButton != nullptr)
	{
		showTabsButton->IsTabStop = controlsCanTakeKeyboard;
	}
	if (startButton != nullptr)
	{
		startButton->IsTabStop = controlsCanTakeKeyboard;
	}
	if (pauseButton != nullptr)
	{
		pauseButton->IsTabStop = false;
	}
	if (resumeButton != nullptr)
	{
		resumeButton->IsTabStop = false;
	}
	if (stopButton != nullptr)
	{
		stopButton->IsTabStop = false;
	}
	if (shutdownButton != nullptr)
	{
		shutdownButton->IsTabStop = false;
	}
	if (keyboardCaptureIcon != nullptr)
	{
		keyboardCaptureIcon->Visibility = state;
	}
	if (mouseCaptureIcon != nullptr)
	{
		mouseCaptureIcon->Visibility = state;
	}
	if (m_isRunning && m_inputCaptured)
	{
		FocusEmulatorSurface();
	}
}

void DirectXPage::ApplyInputCaptureState()
{
	bool shouldCapture = m_isRunning && m_inputCaptured;
	try
	{
		CoreWindow^ window = Window::Current->CoreWindow;
		if (window != nullptr)
		{
			if (shouldCapture)
			{
				window->PointerCursor = nullptr;
				window->SetPointerCapture();
			}
			else
			{
				window->ReleasePointerCapture();
				window->PointerCursor = m_visiblePointerCursor;
			}
		}
	}
	catch (...)
	{
	}

	ApplyCorePointerCaptureState();
}

void DirectXPage::ApplyCorePointerCaptureState()
{
	bool shouldCapture = m_isRunning && m_inputCaptured;
	try
	{
		if (m_coreInput != nullptr)
		{
			if (shouldCapture)
			{
				m_coreInput->PointerCursor = nullptr;
				if (!m_corePointerCaptureActive)
				{
					m_coreInput->SetPointerCapture();
					m_corePointerCaptureActive = true;
				}
			}
			else
			{
				if (m_corePointerCaptureActive)
				{
					m_coreInput->ReleasePointerCapture();
					m_corePointerCaptureActive = false;
				}
				m_coreInput->PointerCursor = m_visiblePointerCursor;
			}
		}
	}
	catch (...)
	{
		m_corePointerCaptureActive = false;
	}
}

void DirectXPage::FocusEmulatorSurface()
{
	try
	{
		this->Focus(Windows::UI::Xaml::FocusState::Programmatic);
	}
	catch (...)
	{
	}
}

void DirectXPage::SendPointerToCore(PointerEventArgs^ e)
{
	auto point = e->CurrentPoint;
	auto props = point->Properties;
	double width = m_inputSurfaceWidth;
	double height = m_inputSurfaceHeight;
	if (width <= 1.0 || height <= 1.0)
	{
		return;
	}

	double physicalX = static_cast<double>(point->Position.X);
	double physicalY = static_cast<double>(point->Position.Y);
	double deltaX = 0.0;
	double deltaY = 0.0;
	if (m_havePhysicalPointerPosition)
	{
		deltaX = physicalX - m_lastPhysicalPointerX;
		deltaY = physicalY - m_lastPhysicalPointerY;
	}
	int relativeX = ScaleRelativePointerDelta(deltaX, deltaY, true);
	int relativeY = ScaleRelativePointerDelta(deltaX, deltaY, false);
	m_emulatorPointerX = (std::max)(0.0, (std::min)(width, physicalX));
	m_emulatorPointerY = (std::max)(0.0, (std::min)(height, physicalY));
	m_lastPhysicalPointerX = physicalX;
	m_lastPhysicalPointerY = physicalY;
	m_havePhysicalPointerPosition = true;
	m_main->SetPointer(
		static_cast<float>(m_emulatorPointerX),
		static_cast<float>(m_emulatorPointerY),
		static_cast<float>(width),
		static_cast<float>(height),
		relativeX,
		relativeY,
		props->IsLeftButtonPressed,
		props->IsRightButtonPressed,
		props->IsMiddleButtonPressed);
}

void DirectXPage::GamepadPollTimer_Tick(Object^ sender, Object^ e)
{
	PollGamepad();
}

void DirectXPage::PollGamepad()
{
	auto gamepads = Gamepad::Gamepads;
	if (!m_isRunning || gamepads == nullptr || gamepads->Size == 0)
	{
		ReleaseGamepadGuestKeys();
		ReleaseGamepadMouseButtons();
		m_gamepadPreviousButtons = 0;
		if (m_virtualKeyboardVisible)
		{
			m_virtualKeyboardVisible = false;
			UpdateVirtualKeyboardVisibility();
		}
		return;
	}

	Gamepad^ gamepad = gamepads->GetAt(0);
	GamepadReading reading = gamepad->GetCurrentReading();
	unsigned int buttons = static_cast<unsigned int>(reading.Buttons);

	bool bumperTogglePressed =
		GamepadButtonDown(buttons, GamepadButtons::LeftShoulder) &&
		GamepadButtonDown(buttons, GamepadButtons::RightShoulder);
	if (m_gamepadBumperToggleArmed && bumperTogglePressed)
	{
		m_gamepadBumperToggleArmed = false;
		if (!RfbServerEnabled())
		{
			ToggleInputCapture();
		}
		else
		{
			SetStatus(L"RFB mode uses the VNC client for mouse and keyboard input.");
		}
	}
	else if (!m_gamepadBumperToggleArmed && !bumperTogglePressed)
	{
		m_gamepadBumperToggleArmed = true;
	}

	bool canSendInput = m_inputCaptured && !RfbServerEnabled();
	if (!canSendInput)
	{
		ReleaseGamepadGuestKeys();
		ReleaseGamepadMouseButtons();
		m_gamepadPreviousButtons = buttons;
		if (m_virtualKeyboardVisible)
		{
			m_virtualKeyboardVisible = false;
			UpdateVirtualKeyboardVisibility();
		}
		return;
	}

	bool triggerTogglePressed = reading.LeftTrigger >= 0.75 && reading.RightTrigger >= 0.75;
	if (m_gamepadTriggerToggleArmed && triggerTogglePressed)
	{
		m_gamepadTriggerToggleArmed = false;
		m_virtualKeyboardVisible = !m_virtualKeyboardVisible;
		ReleaseGamepadGuestKeys();
		UpdateVirtualKeyboardVisibility();
		SetStatus(m_virtualKeyboardVisible
			? L"Virtual keyboard enabled. Use D-pad to select and X to press a key."
			: L"Virtual keyboard hidden.");
	}
	else if (!m_gamepadTriggerToggleArmed && reading.LeftTrigger <= 0.25 && reading.RightTrigger <= 0.25)
	{
		m_gamepadTriggerToggleArmed = true;
	}

	if (m_virtualKeyboardVisible)
	{
		ReleaseGamepadGuestKeys();
		ReleaseGamepadMouseButtons();
		if (GamepadButtonPressed(buttons, m_gamepadPreviousButtons, GamepadButtons::DPadLeft))
		{
			MoveVirtualKeyboardSelection(-1, 0);
		}
		if (GamepadButtonPressed(buttons, m_gamepadPreviousButtons, GamepadButtons::DPadRight))
		{
			MoveVirtualKeyboardSelection(1, 0);
		}
		if (GamepadButtonPressed(buttons, m_gamepadPreviousButtons, GamepadButtons::DPadUp))
		{
			MoveVirtualKeyboardSelection(0, -1);
		}
		if (GamepadButtonPressed(buttons, m_gamepadPreviousButtons, GamepadButtons::DPadDown))
		{
			MoveVirtualKeyboardSelection(0, 1);
		}
		if (GamepadButtonPressed(buttons, m_gamepadPreviousButtons, GamepadButtons::X))
		{
			SendVirtualKeyboardSelectedKey();
		}
		if (GamepadButtonPressed(buttons, m_gamepadPreviousButtons, GamepadButtons::Y))
		{
			m_virtualKeyboardVisible = false;
			UpdateVirtualKeyboardVisibility();
			SetStatus(L"Virtual keyboard hidden.");
		}

		m_gamepadPreviousButtons = buttons;
		return;
	}

	SetGamepadGuestKey(m_gamepadGuestUp, QEMU_KEY_UP, GamepadButtonDown(buttons, GamepadButtons::DPadUp));
	SetGamepadGuestKey(m_gamepadGuestDown, QEMU_KEY_DOWN, GamepadButtonDown(buttons, GamepadButtons::DPadDown));
	SetGamepadGuestKey(m_gamepadGuestLeft, QEMU_KEY_LEFT, GamepadButtonDown(buttons, GamepadButtons::DPadLeft));
	SetGamepadGuestKey(m_gamepadGuestRight, QEMU_KEY_RIGHT, GamepadButtonDown(buttons, GamepadButtons::DPadRight));
	SetGamepadGuestKey(m_gamepadGuestEnter, QEMU_KEY_RETURN, GamepadButtonDown(buttons, GamepadButtons::A));

	bool mouseLeft = GamepadButtonDown(buttons, GamepadButtons::X);
	bool mouseRight = GamepadButtonDown(buttons, GamepadButtons::Y);
	bool mouseButtonChanged = mouseLeft != m_gamepadMouseLeft || mouseRight != m_gamepadMouseRight;
	double stickX = ApplyAnalogDeadzone(reading.LeftThumbstickX);
	double stickY = ApplyAnalogDeadzone(reading.LeftThumbstickY);
	double deltaX = 0.0;
	double deltaY = 0.0;
	int relativeX = 0;
	int relativeY = 0;
	if (stickX != 0.0 || stickY != 0.0)
	{
		const double maxPixelsPerTick = 24.0;
		deltaX = stickX * std::abs(stickX) * maxPixelsPerTick;
		deltaY = -stickY * std::abs(stickY) * maxPixelsPerTick;
		relativeX = static_cast<int>(std::round(deltaX));
		relativeY = static_cast<int>(std::round(deltaY));
	}
	if (relativeX != 0 || relativeY != 0 || mouseButtonChanged)
	{
		double width = (std::max)(1.0, m_inputSurfaceWidth);
		double height = (std::max)(1.0, m_inputSurfaceHeight);
		m_emulatorPointerX = (std::max)(0.0, (std::min)(width, m_emulatorPointerX + deltaX));
		m_emulatorPointerY = (std::max)(0.0, (std::min)(height, m_emulatorPointerY + deltaY));
		m_main->SetPointer(
			static_cast<float>(m_emulatorPointerX),
			static_cast<float>(m_emulatorPointerY),
			static_cast<float>(width),
			static_cast<float>(height),
			relativeX,
			relativeY,
			mouseLeft,
			mouseRight,
			false);
		m_gamepadMouseLeft = mouseLeft;
		m_gamepadMouseRight = mouseRight;
	}

	m_gamepadPreviousButtons = buttons;
}

void DirectXPage::BuildVirtualKeyboard()
{
	m_virtualKeyboardKeys.clear();
	if (virtualKeyboardGrid == nullptr)
	{
		return;
	}

	virtualKeyboardGrid->Children->Clear();
	virtualKeyboardGrid->RowDefinitions->Clear();
	virtualKeyboardGrid->ColumnDefinitions->Clear();
	for (int row = 0; row < 6; ++row)
	{
		virtualKeyboardGrid->RowDefinitions->Append(ref new RowDefinition());
	}
	for (int column = 0; column < 18; ++column)
	{
		virtualKeyboardGrid->ColumnDefinitions->Append(ref new ColumnDefinition());
	}

	auto addKey = [this](const wchar_t* label, unsigned key, int row, int column, int columnSpan)
	{
		Button^ button = ref new Button();
		button->Content = ref new String(label);
		button->MinWidth = 34;
		button->Height = 34;
		button->Padding = Thickness(4, 0, 4, 0);
		button->FontSize = 15;
		button->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Stretch;
		button->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Stretch;
		button->Foreground = ref new SolidColorBrush(Colors::White);
		button->Background = ref new SolidColorBrush(ColorHelper::FromArgb(0x99, 0x3A, 0x3A, 0x3A));
		button->BorderBrush = ref new SolidColorBrush(ColorHelper::FromArgb(0x66, 0xFF, 0xFF, 0xFF));
		button->IsTabStop = false;
		Grid::SetRow(button, row);
		Grid::SetColumn(button, column);
		if (columnSpan > 1)
		{
			Grid::SetColumnSpan(button, columnSpan);
		}
		virtualKeyboardGrid->Children->Append(button);
		m_virtualKeyboardKeys.push_back({ label, key, row, column, columnSpan, button });
	};

	addKey(L"Esc", QEMU_KEY_ESCAPE, 0, 0, 1);
	for (int i = 0; i < 12; ++i)
	{
		addKey((L"F" + std::to_wstring(i + 1)).c_str(), QEMU_KEY_F1 + i, 0, i + 1, 1);
	}
	addKey(L"Prt", QEMU_KEY_PRINT, 0, 13, 1);
	addKey(L"Scr", QEMU_KEY_SCROLLOCK, 0, 14, 1);
	addKey(L"Pause", QEMU_KEY_PAUSE, 0, 15, 2);

	addKey(L"`", QEMU_KEY_BACKQUOTE, 1, 0, 1);
	for (int i = 0; i < 10; ++i)
	{
		wchar_t digit[] = { static_cast<wchar_t>(L'0' + ((i + 1) % 10)), 0 };
		addKey(digit, static_cast<unsigned>(digit[0]), 1, i + 1, 1);
	}
	addKey(L"-", QEMU_KEY_MINUS, 1, 11, 1);
	addKey(L"=", QEMU_KEY_EQUALS, 1, 12, 1);
	addKey(L"Back", QEMU_KEY_BACKSPACE, 1, 13, 2);
	addKey(L"Ins", QEMU_KEY_INSERT, 1, 15, 1);
	addKey(L"Home", QEMU_KEY_HOME, 1, 16, 1);
	addKey(L"PgUp", QEMU_KEY_PAGEUP, 1, 17, 1);

	addKey(L"Tab", QEMU_KEY_TAB, 2, 0, 2);
	const wchar_t* row2 = L"qwertyuiop";
	for (int i = 0; row2[i] != 0; ++i)
	{
		wchar_t label[] = { row2[i], 0 };
		addKey(label, static_cast<unsigned>(row2[i]), 2, i + 2, 1);
	}
	addKey(L"[", QEMU_KEY_LEFTBRACKET, 2, 12, 1);
	addKey(L"]", QEMU_KEY_RIGHTBRACKET, 2, 13, 1);
	addKey(L"Del", QEMU_KEY_DELETE, 2, 15, 1);
	addKey(L"End", QEMU_KEY_END, 2, 16, 1);
	addKey(L"PgDn", QEMU_KEY_PAGEDOWN, 2, 17, 1);

	addKey(L"Caps", QEMU_KEY_CAPSLOCK, 3, 0, 2);
	const wchar_t* row3 = L"asdfghjkl";
	for (int i = 0; row3[i] != 0; ++i)
	{
		wchar_t label[] = { row3[i], 0 };
		addKey(label, static_cast<unsigned>(row3[i]), 3, i + 2, 1);
	}
	addKey(L";", QEMU_KEY_SEMICOLON, 3, 11, 1);
	addKey(L"'", QEMU_KEY_QUOTE, 3, 12, 1);
	addKey(L"Enter", QEMU_KEY_RETURN, 3, 13, 2);

	addKey(L"Shift", QEMU_KEY_LSHIFT, 4, 0, 2);
	addKey(L"\\", QEMU_KEY_BACKSLASH, 4, 2, 1);
	const wchar_t* row4 = L"zxcvbnm";
	for (int i = 0; row4[i] != 0; ++i)
	{
		wchar_t label[] = { row4[i], 0 };
		addKey(label, static_cast<unsigned>(row4[i]), 4, i + 3, 1);
	}
	addKey(L",", QEMU_KEY_COMMA, 4, 10, 1);
	addKey(L".", QEMU_KEY_PERIOD, 4, 11, 1);
	addKey(L"/", QEMU_KEY_SLASH, 4, 12, 1);
	addKey(L"Shift", QEMU_KEY_RSHIFT, 4, 13, 2);
	addKey(L"Up", QEMU_KEY_UP, 4, 16, 1);

	addKey(L"Ctrl", QEMU_KEY_LCTRL, 5, 0, 2);
	addKey(L"Alt", QEMU_KEY_LALT, 5, 2, 2);
	addKey(L"Space", QEMU_KEY_SPACE, 5, 4, 7);
	addKey(L"AltGr", QEMU_KEY_RALT, 5, 11, 2);
	addKey(L"Ctrl", QEMU_KEY_RCTRL, 5, 13, 2);
	addKey(L"Left", QEMU_KEY_LEFT, 5, 15, 1);
	addKey(L"Down", QEMU_KEY_DOWN, 5, 16, 1);
	addKey(L"Right", QEMU_KEY_RIGHT, 5, 17, 1);

	m_virtualKeyboardSelectedIndex = m_virtualKeyboardKeys.empty() ? -1 : 0;
	UpdateVirtualKeyboardSelection();
}

void DirectXPage::UpdateVirtualKeyboardVisibility()
{
	bool visible = m_virtualKeyboardVisible && m_isRunning && m_inputCaptured && !RfbServerEnabled();
	if (virtualKeyboardOverlay != nullptr)
	{
		virtualKeyboardOverlay->Visibility = visible
			? Windows::UI::Xaml::Visibility::Visible
			: Windows::UI::Xaml::Visibility::Collapsed;
	}
	if (!visible)
	{
		m_virtualKeyboardVisible = false;
	}
}

void DirectXPage::UpdateVirtualKeyboardSelection()
{
	for (size_t i = 0; i < m_virtualKeyboardKeys.size(); ++i)
	{
		Button^ button = m_virtualKeyboardKeys[i].button;
		if (button == nullptr)
		{
			continue;
		}
		bool selected = static_cast<int>(i) == m_virtualKeyboardSelectedIndex;
		button->Background = ref new SolidColorBrush(selected
			? ColorHelper::FromArgb(0xDD, 0x00, 0xC8, 0x53)
			: ColorHelper::FromArgb(0x99, 0x3A, 0x3A, 0x3A));
		button->Foreground = ref new SolidColorBrush(selected ? Colors::Black : Colors::White);
	}
}

void DirectXPage::MoveVirtualKeyboardSelection(int dx, int dy)
{
	if (m_virtualKeyboardKeys.empty())
	{
		m_virtualKeyboardSelectedIndex = -1;
		return;
	}
	if (m_virtualKeyboardSelectedIndex < 0 || m_virtualKeyboardSelectedIndex >= static_cast<int>(m_virtualKeyboardKeys.size()))
	{
		m_virtualKeyboardSelectedIndex = 0;
		UpdateVirtualKeyboardSelection();
		return;
	}

	const VirtualKeyboardKey& current = m_virtualKeyboardKeys[m_virtualKeyboardSelectedIndex];
	double currentCenter = static_cast<double>(current.column) + static_cast<double>(current.columnSpan) * 0.5;
	int bestIndex = -1;
	double bestScore = 1.0e9;
	for (size_t i = 0; i < m_virtualKeyboardKeys.size(); ++i)
	{
		if (static_cast<int>(i) == m_virtualKeyboardSelectedIndex)
		{
			continue;
		}
		const VirtualKeyboardKey& candidate = m_virtualKeyboardKeys[i];
		if (dx < 0 && candidate.row == current.row && candidate.column < current.column)
		{
			double score = static_cast<double>(current.column - candidate.column);
			if (score < bestScore)
			{
				bestScore = score;
				bestIndex = static_cast<int>(i);
			}
		}
		else if (dx > 0 && candidate.row == current.row && candidate.column > current.column)
		{
			double score = static_cast<double>(candidate.column - current.column);
			if (score < bestScore)
			{
				bestScore = score;
				bestIndex = static_cast<int>(i);
			}
		}
		else if (dy < 0 && candidate.row < current.row)
		{
			double candidateCenter = static_cast<double>(candidate.column) + static_cast<double>(candidate.columnSpan) * 0.5;
			double score = static_cast<double>(current.row - candidate.row) * 100.0 + std::abs(candidateCenter - currentCenter);
			if (score < bestScore)
			{
				bestScore = score;
				bestIndex = static_cast<int>(i);
			}
		}
		else if (dy > 0 && candidate.row > current.row)
		{
			double candidateCenter = static_cast<double>(candidate.column) + static_cast<double>(candidate.columnSpan) * 0.5;
			double score = static_cast<double>(candidate.row - current.row) * 100.0 + std::abs(candidateCenter - currentCenter);
			if (score < bestScore)
			{
				bestScore = score;
				bestIndex = static_cast<int>(i);
			}
		}
	}

	if (bestIndex >= 0)
	{
		m_virtualKeyboardSelectedIndex = bestIndex;
		UpdateVirtualKeyboardSelection();
	}
}

void DirectXPage::SendVirtualKeyboardSelectedKey()
{
	if (m_virtualKeyboardSelectedIndex < 0 || m_virtualKeyboardSelectedIndex >= static_cast<int>(m_virtualKeyboardKeys.size()))
	{
		return;
	}

	unsigned key = m_virtualKeyboardKeys[m_virtualKeyboardSelectedIndex].key;
	if (key == 0 || m_main == nullptr)
	{
		return;
	}

	m_main->SetKey(key, true);
	m_main->SetKey(key, false);
}

void DirectXPage::SetGamepadGuestKey(bool& state, unsigned key, bool down)
{
	if (state == down || key == 0 || m_main == nullptr)
	{
		state = down;
		return;
	}

	m_main->SetKey(key, down);
	state = down;
}

void DirectXPage::ReleaseGamepadGuestKeys()
{
	SetGamepadGuestKey(m_gamepadGuestUp, QEMU_KEY_UP, false);
	SetGamepadGuestKey(m_gamepadGuestDown, QEMU_KEY_DOWN, false);
	SetGamepadGuestKey(m_gamepadGuestLeft, QEMU_KEY_LEFT, false);
	SetGamepadGuestKey(m_gamepadGuestRight, QEMU_KEY_RIGHT, false);
	SetGamepadGuestKey(m_gamepadGuestEnter, QEMU_KEY_RETURN, false);
}

void DirectXPage::ReleaseGamepadMouseButtons()
{
	if (!m_gamepadMouseLeft && !m_gamepadMouseRight)
	{
		return;
	}

	if (m_main != nullptr)
	{
		double width = (std::max)(1.0, m_inputSurfaceWidth);
		double height = (std::max)(1.0, m_inputSurfaceHeight);
		m_main->SetPointer(
			static_cast<float>(m_emulatorPointerX),
			static_cast<float>(m_emulatorPointerY),
			static_cast<float>(width),
			static_cast<float>(height),
			0,
			0,
			false,
			false,
			false);
	}
	m_gamepadMouseLeft = false;
	m_gamepadMouseRight = false;
}

void DirectXPage::AppendError(const std::wstring& text)
{
	std::wstring line = text;
	line += L"\r\n";
	OutputDebugStringW(line.c_str());

	std::wstring logPath(ApplicationData::Current->LocalFolder->Path->Data());
	logPath += L"\\qemu-uwp.log";
	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	HANDLE logFile = CreateFile2(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, OPEN_ALWAYS, &params);
	if (logFile != INVALID_HANDLE_VALUE)
	{
		int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
		if (size > 0)
		{
			std::string utf8(size, '\0');
			WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), &utf8[0], size, nullptr, nullptr);
			DWORD written = 0;
			WriteFile(logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
		}
		CloseHandle(logFile);
	}

	std::wstring current(errorLogBox->Text->Data());
	if (current == L"No errors recorded.")
	{
		current.clear();
	}
	if (!current.empty())
	{
		current += L"\r\n";
	}
	current += text;
	current = TrimVisibleErrorLog(current);
	errorLogBox->Text = ref new String(current.c_str());
}

void DirectXPage::RefreshCommandLinePreview()
{
	if (commandLineBox == nullptr || m_selectedCommandFile != nullptr)
	{
		return;
	}

	commandLineBox->Text = BuildCommandLine();
}

bool DirectXPage::ValidateStartConfiguration(std::wstring& report)
{
	std::wstring target = SelectedQemuTarget();
	if (target.empty())
	{
		report = L"no QEMU target is selected.";
		return false;
	}

	std::wstring dllName = SelectedQemuDllName();
	std::wstring dllPath(Package::Current->InstalledLocation->Path->Data());
	dllPath += L"\\" + dllName;
	WIN32_FILE_ATTRIBUTE_DATA attributes = {};
	if (!GetFileAttributesExW(dllPath.c_str(), GetFileExInfoStandard, &attributes) ||
		(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
	{
		report = L"the selected packaged DLL is missing: " + dllName + L".";
		return false;
	}

	if (RfbServerEnabled())
	{
		int port = RfbExternalPort();
		if (port < 5900 || port > 5998)
		{
			report = L"the RFB port must be between 5900 and 5998.";
			return false;
		}
	}

	std::vector<std::wstring> directBootPaths;
	for (TextBox^ box : { firmwarePathBox, kernelPathBox, initrdPathBox, dtbPathBox })
	{
		if (box != nullptr && box->Text != nullptr && !std::wstring(box->Text->Data()).empty())
		{
			directBootPaths.push_back(box->Text->Data());
		}
	}
	for (const std::wstring& path : directBootPaths)
	{
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) ||
			(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			report = L"a selected firmware, kernel, initrd, or DTB file is no longer accessible: " + path;
			return false;
		}
	}

	report = dllName + L" is packaged; target " + target + L" is selected";
	if (RfbServerEnabled())
	{
		report += L"; RFB will listen on port " + std::to_wstring(RfbExternalPort());
	}
	else
	{
		report += L"; host display is selected";
	}
	return true;
}

bool DirectXPage::EnsureMediaNbdServer(StorageFile^ mediaFile, bool readOnly, bool cdromMedia, std::wstring& url)
{
	if (mediaFile == nullptr)
	{
		return false;
	}

	std::wstring mediaPath(mediaFile->Path->Data());
	StreamSocketListener^ currentListener = cdromMedia ? m_cdromNbdListener : m_driveNbdListener;
	std::wstring& currentPath = cdromMedia ? m_cdromNbdPath : m_driveNbdPath;
	uint64_t& currentSize = cdromMedia ? m_cdromNbdSize : m_driveNbdSize;
	int& currentPort = cdromMedia ? m_cdromNbdPort : m_driveNbdPort;
	bool& currentReadOnly = cdromMedia ? m_cdromNbdReadOnly : m_driveNbdReadOnly;

	if (currentListener != nullptr && currentPath == mediaPath && currentPort > 0 && currentReadOnly == readOnly)
	{
		url = L"nbd://127.0.0.1:" + std::to_wstring(currentPort);
		return true;
	}

	StopMediaNbdServer(cdromMedia);

	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	DWORD desiredAccess = readOnly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
	HANDLE media = CreateFile2(mediaPath.c_str(), desiredAccess, FILE_SHARE_READ, OPEN_EXISTING, &params);
	if (media == INVALID_HANDLE_VALUE)
	{
		if (!readOnly)
		{
			AppendError(L"Start: media could not be opened for writing; using read-only NBD.");
			readOnly = true;
			desiredAccess = GENERIC_READ;
			media = CreateFile2(mediaPath.c_str(), desiredAccess, FILE_SHARE_READ, OPEN_EXISTING, &params);
		}
		if (media == INVALID_HANDLE_VALUE)
		{
			AppendError(L"Start: could not open media for the local NBD server.");
			return false;
		}
	}

	LARGE_INTEGER size = {};
	bool sizeOk = GetFileSizeEx(media, &size) != 0;
	CloseHandle(media);
	if (!sizeOk || size.QuadPart <= 0)
	{
		AppendError(L"Start: invalid media size for the local NBD server.");
		return false;
	}

	for (int port = 10809; port <= 10839; port++)
	{
		auto listener = ref new StreamSocketListener();
		listener->ConnectionReceived += ref new TypedEventHandler<StreamSocketListener^, StreamSocketListenerConnectionReceivedEventArgs^>(
			this, &DirectXPage::OnMediaNbdConnectionReceived);

		try
		{
			create_task(listener->BindServiceNameAsync(ref new String(std::to_wstring(port).c_str()))).wait();
			if (cdromMedia)
			{
				m_cdromNbdListener = listener;
			}
			else
			{
				m_driveNbdListener = listener;
			}
			currentPath = mediaPath;
			currentSize = static_cast<uint64_t>(size.QuadPart);
			currentPort = port;
			currentReadOnly = readOnly;
			url = L"nbd://127.0.0.1:" + std::to_wstring(port);
			AppendError((readOnly ? L"Start: local read-only NBD server started at " : L"Start: local read-write NBD server started at ") + url +
				(cdromMedia ? L" for CD-ROM." : L" for Drive."));
			return true;
		}
		catch (Exception^ ex)
		{
			std::wstring bindError = L"Start: porta NBD local indisponivel ";
			bindError += std::to_wstring(port);
			if (ex != nullptr && ex->Message != nullptr)
			{
				bindError += L": ";
				bindError += ex->Message->Data();
			}
			AppendError(bindError);
			delete listener;
		}
	}

	AppendError(L"Start: could not start local media NBD server.");
	return false;
}

void DirectXPage::StopMediaNbdServer(bool cdromMedia)
{
	StreamSocketListener^& listener = cdromMedia ? m_cdromNbdListener : m_driveNbdListener;
	std::wstring& path = cdromMedia ? m_cdromNbdPath : m_driveNbdPath;
	uint64_t& size = cdromMedia ? m_cdromNbdSize : m_driveNbdSize;
	int& port = cdromMedia ? m_cdromNbdPort : m_driveNbdPort;
	bool& readOnly = cdromMedia ? m_cdromNbdReadOnly : m_driveNbdReadOnly;

	if (listener != nullptr)
	{
		delete listener;
		listener = nullptr;
	}
	path.clear();
	size = 0;
	port = 0;
	readOnly = true;
}

void DirectXPage::StopAllMediaNbdServers()
{
	StopMediaNbdServer(false);
	StopMediaNbdServer(true);
}

bool DirectXPage::RfbServerEnabled()
{
	/* Xbox/UWP uses the local in-process Direct3D host display exclusively. */
	return false;
}

int DirectXPage::RfbExternalPort()
{
	int port = 5900;
	std::wstring text = rfbPortBox != nullptr && rfbPortBox->Text != nullptr ? std::wstring(rfbPortBox->Text->Data()) : std::wstring();
	TryParsePort(text, 5900, port);
	if (port < 5900 || port > 5998)
	{
		port = 5900;
	}
	return port;
}

int DirectXPage::RfbInternalPort()
{
	return RfbExternalPort() + 1;
}

std::wstring DirectXPage::RfbDisplayArgument()
{
	int displayNumber = RfbInternalPort() - 5900;
	return L"127.0.0.1:" + std::to_wstring(displayNumber);
}

std::wstring DirectXPage::LocalRfbAddress()
{
	try
	{
		IVectorView<HostName^>^ hostNames = NetworkInformation::GetHostNames();
		for (HostName^ hostName : hostNames)
		{
			if (hostName != nullptr &&
				hostName->Type == HostNameType::Ipv4 &&
				hostName->DisplayName != nullptr)
			{
				std::wstring value(hostName->DisplayName->Data());
				if (value.find(L"127.") != 0)
				{
					return value;
				}
			}
		}
	}
	catch (...)
	{
	}
	return L"127.0.0.1";
}

void DirectXPage::UpdateRfbStatus(const std::wstring& status)
{
	std::wstring text = L"RFB server: ";
	text += status;
	if (m_rfbStatusEnabled && status != L"disabled")
	{
		text += L" | IP ";
		text += m_rfbProxyAddress.empty() ? L"127.0.0.1" : m_rfbProxyAddress;
		text += L" | port ";
		text += std::to_wstring(m_rfbProxyPort > 0 ? m_rfbProxyPort : 5900);
		if (m_rfbClientCount > 0)
		{
			text += L" | clients ";
			text += std::to_wstring(m_rfbClientCount);
		}
	}

	auto dispatcher = Dispatcher;
	auto message = std::make_shared<std::wstring>(text);
	dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, message]()
	{
		if (rfbStatusText != nullptr)
		{
			rfbStatusText->Text = ref new String(message->c_str());
		}
		if (rfbRuntimeStatusText != nullptr)
		{
			rfbRuntimeStatusText->Text = ref new String(message->c_str());
		}
		UpdateRfbRuntimeOverlay();
	}));
}

void DirectXPage::UpdateRfbRuntimeOverlay()
{
	if (rfbRuntimeOverlay == nullptr)
	{
		return;
	}
	rfbRuntimeOverlay->Visibility = (m_isRunning && RfbServerEnabled())
		? Windows::UI::Xaml::Visibility::Visible
		: Windows::UI::Xaml::Visibility::Collapsed;
}

bool DirectXPage::EnsureRfbProxyServer()
{
	if (!RfbServerEnabled())
	{
		m_rfbStatusEnabled = false;
		StopRfbProxyServer();
		return true;
	}

	int port = RfbExternalPort();
	int internalPort = port + 1;
	if (m_rfbProxyListener != nullptr && m_rfbProxyPort == port)
	{
		UpdateRfbStatus(m_rfbClientCount > 0 ? L"client connected" : L"listening");
		return true;
	}

	StopRfbProxyServer();
	m_rfbStopping = false;
	auto listener = ref new StreamSocketListener();
	listener->ConnectionReceived += ref new TypedEventHandler<StreamSocketListener^, StreamSocketListenerConnectionReceivedEventArgs^>(
		this, &DirectXPage::OnRfbProxyConnectionReceived);

	try
	{
		create_task(listener->BindServiceNameAsync(ref new String(std::to_wstring(port).c_str()))).wait();
		m_rfbProxyListener = listener;
		m_rfbProxyPort = port;
		m_rfbInternalPort = internalPort;
		m_rfbProxyAddress = LocalRfbAddress();
		m_rfbStatusEnabled = true;
		m_rfbClientCount = 0;
		UpdateRfbStatus(L"listening");
		AppendError(L"RFB proxy listening at " + m_rfbProxyAddress + L":" + std::to_wstring(port) +
			L" and forwarding to QEMU VNC on 127.0.0.1:" + std::to_wstring(internalPort) + L".");
		return true;
	}
	catch (Exception^ ex)
	{
		std::wstring error = L"RFB proxy failed to bind port ";
		error += std::to_wstring(port);
		if (ex != nullptr && ex->Message != nullptr)
		{
			error += L": ";
			error += ex->Message->Data();
		}
		AppendError(error);
		UpdateRfbStatus(L"bind failed");
		delete listener;
		return false;
	}
}

void DirectXPage::StopRfbProxyServer()
{
	m_rfbStopping = true;
	if (m_rfbProxyListener != nullptr)
	{
		delete m_rfbProxyListener;
		m_rfbProxyListener = nullptr;
	}
	m_rfbProxyPort = 0;
	m_rfbInternalPort = 0;
	m_rfbClientCount = 0;
	if (!m_rfbStatusEnabled)
	{
		m_rfbProxyAddress.clear();
	}
	UpdateRfbStatus(m_rfbStatusEnabled ? L"configured" : L"disabled");
}

void DirectXPage::OnRfbProxyConnectionReceived(StreamSocketListener^ sender, StreamSocketListenerConnectionReceivedEventArgs^ args)
{
	(void)sender;
	StreamSocket^ clientSocket = args->Socket;
	int internalPort = m_rfbInternalPort;
	m_rfbClientCount++;
	UpdateRfbStatus(L"client connected");
	create_task([this, clientSocket, internalPort]()
	{
		ServeRfbProxyClient(clientSocket, internalPort);
	});
}

void DirectXPage::RelayRfbStream(IInputStream^ input, IOutputStream^ output)
{
	try
	{
		while (!m_rfbStopping)
		{
			IBuffer^ buffer = ref new Buffer(8192);
			buffer = create_task(input->ReadAsync(buffer, 8192, InputStreamOptions::Partial)).get();
			if (buffer == nullptr || buffer->Length == 0)
			{
				break;
			}
			create_task(output->WriteAsync(buffer)).get();
		}
	}
	catch (...)
	{
	}
}

void DirectXPage::ServeRfbProxyClient(StreamSocket^ clientSocket, int internalPort)
{
	StreamSocket^ qemuSocket = ref new StreamSocket();
	bool connected = false;
	for (int attempt = 0; attempt < 100 && !connected && !m_rfbStopping; attempt++)
	{
		try
		{
			create_task(qemuSocket->ConnectAsync(ref new HostName("127.0.0.1"), ref new String(std::to_wstring(internalPort).c_str()))).get();
			connected = true;
		}
		catch (...)
		{
			Sleep(100);
		}
	}

	if (!connected)
	{
		auto dispatcher = Dispatcher;
		dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, internalPort]()
		{
			AppendError(L"RFB proxy accepted a client, but QEMU VNC was not reachable on 127.0.0.1:" + std::to_wstring(internalPort) + L".");
		}));
		delete clientSocket;
		delete qemuSocket;
		m_rfbClientCount = (std::max)(0, m_rfbClientCount - 1);
		UpdateRfbStatus(L"client disconnected");
		return;
	}

	auto clientToQemu = create_task([this, clientSocket, qemuSocket]()
	{
		RelayRfbStream(clientSocket->InputStream, qemuSocket->OutputStream);
	});
	auto qemuToClient = create_task([this, clientSocket, qemuSocket]()
	{
		RelayRfbStream(qemuSocket->InputStream, clientSocket->OutputStream);
	});

	while (!m_rfbStopping && !clientToQemu.is_done() && !qemuToClient.is_done())
	{
		Sleep(50);
	}

	delete clientSocket;
	delete qemuSocket;
	m_rfbClientCount = (std::max)(0, m_rfbClientCount - 1);
	UpdateRfbStatus(m_rfbClientCount > 0 ? L"client connected" : L"listening");
}

void DirectXPage::OnMediaNbdConnectionReceived(StreamSocketListener^ sender, StreamSocketListenerConnectionReceivedEventArgs^ args)
{
	auto socket = args->Socket;
	bool cdromMedia = sender == m_cdromNbdListener;
	std::wstring mediaPath = cdromMedia ? m_cdromNbdPath : m_driveNbdPath;
	uint64_t mediaSize = cdromMedia ? m_cdromNbdSize : m_driveNbdSize;
	bool readOnly = cdromMedia ? m_cdromNbdReadOnly : m_driveNbdReadOnly;
	create_task([this, socket, mediaPath, mediaSize, readOnly]()
	{
		ServeMediaNbdClient(socket, mediaPath, mediaSize, readOnly);
	});
}

void DirectXPage::ServeMediaNbdClient(StreamSocket^ socket, std::wstring mediaPath, uint64_t mediaSize, bool readOnly)
{
	try
	{
		auto reader = ref new DataReader(socket->InputStream);
		reader->InputStreamOptions = InputStreamOptions::None;
		auto writer = ref new DataWriter(socket->OutputStream);

		auto writeBe16 = [writer](uint16_t value)
		{
			unsigned char bytes[2] =
			{
				static_cast<unsigned char>((value >> 8) & 0xff),
				static_cast<unsigned char>(value & 0xff)
			};
			writer->WriteBytes(ref new Platform::Array<unsigned char>(bytes, 2));
		};
		auto writeBe32 = [writer](uint32_t value)
		{
			unsigned char bytes[4] =
			{
				static_cast<unsigned char>((value >> 24) & 0xff),
				static_cast<unsigned char>((value >> 16) & 0xff),
				static_cast<unsigned char>((value >> 8) & 0xff),
				static_cast<unsigned char>(value & 0xff)
			};
			writer->WriteBytes(ref new Platform::Array<unsigned char>(bytes, 4));
		};
		auto writeBe64 = [writer](uint64_t value)
		{
			unsigned char bytes[8] =
			{
				static_cast<unsigned char>((value >> 56) & 0xff),
				static_cast<unsigned char>((value >> 48) & 0xff),
				static_cast<unsigned char>((value >> 40) & 0xff),
				static_cast<unsigned char>((value >> 32) & 0xff),
				static_cast<unsigned char>((value >> 24) & 0xff),
				static_cast<unsigned char>((value >> 16) & 0xff),
				static_cast<unsigned char>((value >> 8) & 0xff),
				static_cast<unsigned char>(value & 0xff)
			};
			writer->WriteBytes(ref new Platform::Array<unsigned char>(bytes, 8));
		};
		auto readBe16 = [reader]() -> uint16_t
		{
			unsigned char b0 = reader->ReadByte();
			unsigned char b1 = reader->ReadByte();
			return (static_cast<uint16_t>(b0) << 8) | b1;
		};
		auto readBe32 = [reader]() -> uint32_t
		{
			uint32_t value = 0;
			for (int i = 0; i < 4; i++)
			{
				value = (value << 8) | reader->ReadByte();
			}
			return value;
		};
		auto readBe64 = [reader]() -> uint64_t
		{
			uint64_t value = 0;
			for (int i = 0; i < 8; i++)
			{
				value = (value << 8) | reader->ReadByte();
			}
			return value;
		};

		writeBe64(0x4e42444d41474943ULL);
		writeBe64(0x0000420281861253ULL);
		writeBe64(mediaSize);
		writeBe32(readOnly ? 0x00000003 : 0x00000001);
		auto zeroes = ref new Platform::Array<unsigned char>(124);
		writer->WriteBytes(zeroes);
		create_task(writer->StoreAsync()).wait();

		CREATEFILE2_EXTENDED_PARAMETERS params = {};
		params.dwSize = sizeof(params);
		DWORD access = readOnly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
		HANDLE media = CreateFile2(mediaPath.c_str(), access, FILE_SHARE_READ, OPEN_EXISTING, &params);
		if (media == INVALID_HANDLE_VALUE)
		{
			delete writer;
			delete reader;
			delete socket;
			return;
		}

		while (true)
		{
			unsigned int loaded = create_task(reader->LoadAsync(28)).get();
			if (loaded < 28)
			{
				break;
			}

			uint32_t magic = readBe32();
			uint16_t flags = readBe16();
			(void)flags;
			uint16_t type = readBe16();
			uint64_t handle = readBe64();
			uint64_t offset = readBe64();
			uint32_t length = readBe32();
			if (magic != 0x25609513)
			{
				break;
			}
			if (type == 2)
			{
				break;
			}

			uint32_t error = 0;
			std::vector<unsigned char> payload;
			if (type == 0)
			{
				if (offset + length > mediaSize)
				{
					error = 22;
				}
				else
				{
					payload.resize(length);
					LARGE_INTEGER position = {};
					position.QuadPart = static_cast<LONGLONG>(offset);
					SetFilePointerEx(media, position, nullptr, FILE_BEGIN);
					DWORD read = 0;
					if (!ReadFile(media, payload.data(), length, &read, nullptr) || read != length)
					{
						error = 5;
					}
				}
			}
			else if (type == 1)
			{
				payload.resize(length);
				if (length > 0)
				{
					unsigned int writePayloadLoaded = create_task(reader->LoadAsync(length)).get();
					if (writePayloadLoaded < length)
					{
						break;
					}
					auto writePayload = ref new Platform::Array<unsigned char>(length);
					reader->ReadBytes(writePayload);
					memcpy(payload.data(), writePayload->Data, length);
				}

				if (readOnly)
				{
					error = 1;
				}
				else if (offset + length > mediaSize)
				{
					error = 22;
				}
				else
				{
					LARGE_INTEGER position = {};
					position.QuadPart = static_cast<LONGLONG>(offset);
					SetFilePointerEx(media, position, nullptr, FILE_BEGIN);
					DWORD written = 0;
					if (!WriteFile(media, payload.data(), length, &written, nullptr) || written != length)
					{
						error = 5;
					}
				}
			}
			else if (type != 3)
			{
				error = 95;
			}

			writeBe32(0x67446698);
			writeBe32(error);
			writeBe64(handle);
			if (error == 0 && type == 0 && !payload.empty())
			{
				writer->WriteBytes(ref new Platform::Array<unsigned char>(payload.data(), static_cast<unsigned int>(payload.size())));
			}
			create_task(writer->StoreAsync()).wait();
		}

		CloseHandle(media);
		delete writer;
		delete reader;
		delete socket;
	}
	catch (...)
	{
		try
		{
			delete socket;
		}
		catch (...)
		{
		}
	}
}

std::wstring DirectXPage::BuildAutomaticCommandLine()
{
	std::wstring target = SelectedQemuTarget();
	TargetProfile targetProfile = GetTargetProfile(target);
	std::wstring diskInterface = SelectedComboTag(diskInterfaceBox);
	TargetBlockStyle blockStyle = EffectiveBlockStyle(targetProfile.blockStyle, diskInterface);
	std::wstring diskFormatOverride = SelectedComboTag(diskFormatBox);
	std::wstring cdromFormatOverride = SelectedComboTag(cdromFormatBox);
	std::wstring driveCache = SelectedComboTag(driveCacheBox);
	std::wstring driveMode = SelectedComboTag(driveModeBox);
	std::wstring firmware = SelectedComboTag(firmwareBox);
	std::wstring firmwarePath = firmwarePathBox != nullptr && firmwarePathBox->Text != nullptr ? std::wstring(firmwarePathBox->Text->Data()) : std::wstring();
	std::wstring kernelPath = kernelPathBox != nullptr && kernelPathBox->Text != nullptr ? std::wstring(kernelPathBox->Text->Data()) : std::wstring();
	std::wstring initrdPath = initrdPathBox != nullptr && initrdPathBox->Text != nullptr ? std::wstring(initrdPathBox->Text->Data()) : std::wstring();
	std::wstring dtbPath = dtbPathBox != nullptr && dtbPathBox->Text != nullptr ? std::wstring(dtbPathBox->Text->Data()) : std::wstring();
	std::wstring kernelAppend = kernelAppendBox != nullptr && kernelAppendBox->Text != nullptr ? std::wstring(kernelAppendBox->Text->Data()) : std::wstring();

	std::wstring memoryValue = SelectedComboTag(memoryBox);
	int memoryMb = memoryValue.empty() ? 512 : static_cast<int>(std::wcstol(memoryValue.c_str(), nullptr, 10));

	StorageFile^ driveFile = m_stagedDriveFile != nullptr ? m_stagedDriveFile : m_selectedDriveFile;
	StorageFile^ cdromFile = m_stagedCdromFile != nullptr ? m_stagedCdromFile : m_selectedCdromFile;
	std::wstring command = L"qemu-system-x86_64";
	int diagnosticProfile = diagnosticProfileBox != nullptr ? diagnosticProfileBox->SelectedIndex : 0;
	int tcgTbSizeMb = 128;
	if (diagnosticProfile == ProfileXboxTcg64)
	{
		tcgTbSizeMb = 64;
	}
	else if (diagnosticProfile == ProfileXboxTcg256)
	{
		tcgTbSizeMb = 256;
	}
	command += L" -accel tcg,thread=multi,tb-size=";
	command += std::to_wstring(tcgTbSizeMb);
	std::wstring qemuDir(Package::Current->InstalledLocation->Path->Data());
	qemuDir += L"\\qemu";
	command += L" -L ";
	command += QuoteForCommandLine(ref new String(qemuDir.c_str()))->Data();
	if (RfbServerEnabled())
	{
		command += L" -display vnc=";
		command += RfbDisplayArgument();
	}
	else
	{
		command += L" -display host";
	}
	command += L" -audiodev xaudio2,id=audio0";

	if (memoryMb > 0)
	{
		command += L" -m ";
		command += std::to_wstring(memoryMb);
		command += L"M";
	}

	std::wstring machineValue = SelectedSelectorValue(machineSelectorBox);
	std::wstring defaultMachineValue = TargetDefaultMachine(targetProfile);
	std::wstring selectedMachine = machineValue.empty() ? defaultMachineValue : machineValue;
	bool machineUsesVirtioMmio = MachineUsesVirtioMmio(selectedMachine, targetProfile);
	bool machineSupportsUsb = MachineSupportsUsbPeripherals(selectedMachine);
	bool machineSupportsPci = MachineSupportsPciPeripherals(selectedMachine, targetProfile);
	bool machineSupportsPcDevices = MachineSupportsPcPeripherals(selectedMachine);
	if (machineUsesVirtioMmio)
	{
		blockStyle = TargetBlockStyle::VirtioMmio;
		diskInterface = L"virtio";
	}

	auto appendDevice = [&command, machineSupportsUsb, machineSupportsPci, machineSupportsPcDevices](const std::wstring& device)
	{
		if (!device.empty())
		{
			if ((!machineSupportsUsb && DeviceRequiresUsb(device)) ||
				(!machineSupportsPci && DeviceRequiresPci(device)) ||
				(!machineSupportsPcDevices && DeviceRequiresPcMachine(device)))
			{
				return;
			}
			command += L" -device ";
			command += device;
		}
	};

	std::vector<std::wstring> machineOptions;
	if (machineSupportsPcDevices)
	{
		for (ComboBox^ optionBox : { acpiSelectorBox, hpetSelectorBox, vmportSelectorBox })
		{
			std::wstring option = SelectedComboTag(optionBox);
			if (!option.empty())
			{
				machineOptions.push_back(option);
			}
		}
	}

	std::wstring usbBus = SelectedComboTag(usbBusSelectorBox);
	if (!machineSupportsUsb)
	{
		usbBus.clear();
	}
	else if (usbBus == L"usb=off")
	{
		machineOptions.push_back(usbBus);
	}
	if (!machineValue.empty() || !machineOptions.empty() || !defaultMachineValue.empty())
	{
		command += L" -M ";
		command += selectedMachine.empty() ? L"pc" : selectedMachine;
		for (const std::wstring& option : machineOptions)
		{
			command += L",";
			command += option;
		}
	}

	std::wstring selectorValue = SelectedComboTag(smpSelectorBox);
	if (selectorValue.empty())
	{
		selectorValue = L"2";
	}
	command += L" -smp ";
	command += selectorValue;

	selectorValue = SelectedSelectorValue(cpuSelectorBox);
	if (!selectorValue.empty())
	{
		command += L" -cpu ";
		command += selectorValue;
	}

	selectorValue = SelectedComboTag(rtcSelectorBox);
	if (!selectorValue.empty())
	{
		command += L" -rtc ";
		command += selectorValue;
	}

	selectorValue = SelectedComboTag(rebootBehaviorBox);
	if (!selectorValue.empty())
	{
		command += L" ";
		command += selectorValue;
	}

	if (usbBus == L"on")
	{
		command += L" -usb";
	}

	selectorValue = SelectedSelectorValue(netdevSelectorBox);
	if (!selectorValue.empty())
	{
		command += L" -netdev ";
		command += selectorValue;
	}

	std::wstring networkDevice = SelectedSelectorValue(networkDeviceSelectorBox);
	if (!networkDevice.empty())
	{
		if (SelectedSelectorValue(netdevSelectorBox).empty())
		{
			command += L" -netdev user,id=net0";
		}
		appendDevice(networkDevice + L",netdev=net0");
	}

	std::wstring usbController = SelectedSelectorValue(usbControllerSelectorBox);
	std::wstring inputDevice = SelectedSelectorValue(inputDeviceSelectorBox);
	if (!machineSupportsUsb)
	{
		usbController.clear();
		inputDevice.clear();
	}
	if (machineSupportsUsb && inputDevice.empty() && usbBus != L"usb=off" && ProfilePrefersAbsolutePointer(diagnosticProfile, target))
	{
		usbBus = L"on";
		inputDevice = L"usb-tablet";
	}
	if (machineSupportsUsb && usbBus.empty() &&
		(inputDevice.find(L"usb-") != std::wstring::npos ||
		 usbController.find(L"usb") != std::wstring::npos ||
		 usbController.find(L"xHCI") != std::wstring::npos ||
		 usbController.find(L"xhci") != std::wstring::npos))
	{
		usbBus = L"on";
	}
	if (usbBus == L"on" && command.find(L" -usb") == std::wstring::npos)
	{
		command += L" -usb";
	}
	appendDevice(usbController);
	appendDevice(inputDevice);

	selectorValue = SelectedSelectorValue(audioDeviceSelectorBox);
	if (!selectorValue.empty())
	{
		if (selectorValue == L"intel-hda+hda-duplex")
		{
			appendDevice(L"intel-hda");
			appendDevice(L"hda-duplex,audiodev=audio0");
		}
		else if (selectorValue == L"pcspk")
		{
			appendDevice(L"pcspk,audiodev=audio0");
		}
		else
		{
			appendDevice(selectorValue + L",audiodev=audio0");
		}
	}

	appendDevice(SelectedSelectorValue(deviceSelectorBox));

	selectorValue = SelectedSelectorValue(vgaSelectorBox);
	if (!selectorValue.empty() && MachineSupportsVgaOption(selectedMachine, target))
	{
		command += L" -vga ";
		command += selectorValue;
	}
	selectorValue = SelectedSelectorValue(monitorSelectorBox);
	if (!selectorValue.empty())
	{
		command += L" -monitor ";
		command += selectorValue;
	}
	if (diagnosticProfile != ProfileNormalCommand && diagnosticProfile != ProfileVideoOnlyNoMedia)
	{
		command += ProfileExtraCommandArguments(diagnosticProfile, qemuDir, target, firmwarePath.empty() && (firmware.empty() || firmware == L"default"));
	}
	if (!firmwarePath.empty())
	{
		command += L" -bios ";
		command += QuoteForCommandLine(ref new String(firmwarePath.c_str()))->Data();
	}
	else if (firmware == L"uefi" || firmware == L"secure-uefi")
	{
		bool secure = firmware == L"secure-uefi";
		std::wstring biosPath = DefaultFirmwarePathForTarget(qemuDir, target, secure);
		if (!biosPath.empty())
		{
			command += L" -bios ";
			command += QuoteForCommandLine(ref new String(biosPath.c_str()))->Data();
		}
	}
	if (!kernelPath.empty())
	{
		command += L" -kernel ";
		command += QuoteForCommandLine(ref new String(kernelPath.c_str()))->Data();
	}
	if (!initrdPath.empty())
	{
		command += L" -initrd ";
		command += QuoteForCommandLine(ref new String(initrdPath.c_str()))->Data();
	}
	if (!dtbPath.empty())
	{
		command += L" -dtb ";
		command += QuoteForCommandLine(ref new String(dtbPath.c_str()))->Data();
	}
	if (!kernelAppend.empty())
	{
		command += L" -append ";
		command += QuoteForCommandLine(ref new String(kernelAppend.c_str()))->Data();
	}

	if (diagnosticProfile == ProfileVideoOnlyNoMedia)
	{
		if (MachineSupportsVgaOption(selectedMachine, target) && command.find(L" -vga ") == std::wstring::npos)
		{
			command += L" -vga std";
		}
		command += L" -monitor none -serial none -boot c";
	}
	else
	{
		bool hasBootDevice = false;
		bool useSata = diskInterface == L"sata";
		bool hasSharedFolderMedia = VvfatFeatureEnabled && m_selectedSharedFolder != nullptr;
		if (useSata && (driveFile != nullptr || cdromFile != nullptr || hasSharedFolderMedia))
		{
			command += L" -device ahci,id=ahci0";
		}

		if (driveFile != nullptr)
		{
			std::wstring fileName(driveFile->Name->Data());
			std::wstring diskFormat = diskFormatOverride.empty() ? DiskFormatFromFileName(fileName) : diskFormatOverride;
			std::wstring mediaUrl;
			bool readOnlyDrive = driveMode == L"readonly";
			bool snapshotDrive = driveMode == L"snapshot";
			bool useNbd = driveFile == m_stagedDriveFile && EnsureMediaNbdServer(driveFile, readOnlyDrive, false, mediaUrl);
			if (driveFile == m_stagedDriveFile && !useNbd)
			{
				AppendError(L"Start: drive NBD setup failed; falling back to direct file path.");
			}
			bool detachedDrive = UsesDetachedDriveDevice(blockStyle, diskInterface);
			command += L" -drive file=";
			command += useNbd ? mediaUrl : QuoteForCommandLine(driveFile->Path)->Data();
			command += L",format=";
			command += diskFormat;
			if (!driveCache.empty())
			{
				command += L",cache=";
				command += driveCache;
			}
			if (readOnlyDrive)
			{
				command += L",readonly=on";
			}
			if (snapshotDrive)
			{
				command += L",snapshot=on";
			}
			if (detachedDrive)
			{
				command += L",if=none,id=";
				command += DriveId;
				command += L",media=disk";
				std::wstring device = DriveDeviceArgument(blockStyle, diskInterface, false);
				if (!device.empty())
				{
					command += L" -device ";
					command += device;
				}
			}
			else
			{
				command += L",if=";
				command += DriveInterfaceArgument(blockStyle, diskInterface);
				command += L",media=disk";
			}
			hasBootDevice = true;
		}

		if (cdromFile != nullptr)
		{
			std::wstring fileName(cdromFile->Name->Data());
			std::wstring cdromFormat = cdromFormatOverride.empty() ? DiskFormatFromFileName(fileName) : cdromFormatOverride;
			std::wstring mediaUrl;
			bool useNbd = cdromFile == m_stagedCdromFile && EnsureMediaNbdServer(cdromFile, true, true, mediaUrl);
			if (cdromFile == m_stagedCdromFile && !useNbd)
			{
				AppendError(L"Start: CD-ROM NBD setup failed; falling back to direct file path.");
			}
			bool detachedDrive = UsesDetachedDriveDevice(blockStyle, diskInterface);
			command += L" -drive file=";
			command += useNbd ? mediaUrl : QuoteForCommandLine(cdromFile->Path)->Data();
			command += L",format=";
			command += cdromFormat;
			if (!driveCache.empty())
			{
				command += L",cache=";
				command += driveCache;
			}
			if (detachedDrive)
			{
				command += L",if=none,id=";
				command += CdromId;
				command += L",media=cdrom,readonly=on";
				std::wstring device = DriveDeviceArgument(blockStyle, diskInterface, true);
				if (!device.empty())
				{
					command += L" -device ";
					command += device;
				}
			}
			else
			{
				command += L",if=";
				command += DriveInterfaceArgument(blockStyle, diskInterface);
				command += L",media=cdrom,readonly=on";
			}
			hasBootDevice = true;
		}

		if (VvfatFeatureEnabled && m_selectedSharedFolder != nullptr && m_selectedSharedFolder->Path != nullptr)
		{
			std::wstring folderPath(m_selectedSharedFolder->Path->Data());
			std::wstring mode = SelectedComboTag(sharedFolderModeBox);
			std::wstring vvfatPath = L"fat:";
			vvfatPath += mode == L"ro" ? L"ro:" : L"rw:";
			vvfatPath += folderPath;
			bool detachedDrive = UsesDetachedDriveDevice(blockStyle, diskInterface);
			command += L" -drive file=";
			command += QuoteForCommandLine(ref new String(vvfatPath.c_str()))->Data();
			command += L",format=raw";
			if (detachedDrive)
			{
				command += L",if=none,id=";
				command += SharedDriveId;
				command += L",media=disk";
				std::wstring device = SharedDriveDeviceArgument(blockStyle, diskInterface);
				if (!device.empty())
				{
					command += L" -device ";
					command += device;
				}
			}
			else
			{
				command += L",if=";
				command += DriveInterfaceArgument(blockStyle, diskInterface);
				command += L",media=disk";
			}
		}

		std::wstring bootMode = SelectedComboTag(bootModeBox);
		std::wstring bootOrder = SelectedComboTag(bootOrderBox);
		std::wstring bootMenu = SelectedComboTag(bootMenuBox);
		int bootDevice = bootDeviceBox != nullptr ? bootDeviceBox->SelectedIndex : BootDeviceAuto;
		if (bootMode.empty() || bootMode == L"simple")
		{
			bootOrder.clear();
		}
		if (bootOrder.empty())
		{
			if (bootDevice == BootDeviceDrive)
			{
				bootOrder = L"c";
			}
			else if (bootDevice == BootDeviceCdrom)
			{
				bootOrder = L"d";
			}
			else
			{
				bootOrder = cdromFile != nullptr ? L"d" : L"c";
			}
		}

		if (bootMode == L"order" || bootMode == L"once" || !bootMenu.empty())
		{
			command += L" -boot ";
			bool hasBootPart = false;
			if (bootMode == L"once")
			{
				command += L"once=";
				command += bootOrder.substr(0, 1);
				hasBootPart = true;
			}
			else
			{
				command += L"order=";
				command += bootOrder;
				hasBootPart = true;
			}
			if (!bootMenu.empty())
			{
				if (hasBootPart)
				{
					command += L",";
				}
				command += L"menu=";
				command += bootMenu;
			}
		}
		else if (hasBootDevice || bootDevice != BootDeviceAuto)
		{
			command += L" -boot ";
			command += bootOrder.substr(0, 1);
		}
	}

	return command;
}

std::wstring DirectXPage::BuildAdditionalArguments()
{
	if (extraArgumentsBox != nullptr && extraArgumentsBox->Text != nullptr && extraArgumentsBox->Text->Length() > 0)
	{
		return std::wstring(extraArgumentsBox->Text->Data());
	}
	return std::wstring();
}

String^ DirectXPage::BuildCommandLine()
{
	std::wstring command = BuildAutomaticCommandLine();
	std::wstring additional = BuildAdditionalArguments();
	std::vector<std::wstring> automaticTokens = TokenizeCommandLine(command);
	std::vector<std::wstring> additionalTokens = TokenizeCommandLine(additional);
	if (!CommandHasOption(automaticTokens, L"-monitor") && !CommandHasOption(additionalTokens, L"-monitor"))
	{
		command += L" -monitor none";
	}
	if (!CommandHasOption(automaticTokens, L"-serial") && !CommandHasOption(additionalTokens, L"-serial"))
	{
		command += L" -serial none";
	}
	if (!CommandHasOption(automaticTokens, L"-parallel") && !CommandHasOption(additionalTokens, L"-parallel"))
	{
		command += L" -parallel none";
	}
	if (!additional.empty())
	{
		command += L" ";
		command += additional;
	}
	return ref new String(command.c_str());
}

void DirectXPage::UpdateCommandPreview()
{
	if (commandPreviewBlock == nullptr || commandLineBox == nullptr)
	{
		return;
	}

	commandPreviewBlock->Blocks->Clear();
	Paragraph^ paragraph = ref new Paragraph();
	Windows::UI::Color automaticColor = {};
	automaticColor.A = 255;
	automaticColor.R = 102;
	automaticColor.G = 217;
	automaticColor.B = 239;
	Windows::UI::Color additionalColor = {};
	additionalColor.A = 255;
	additionalColor.R = 255;
	additionalColor.G = 204;
	additionalColor.B = 102;

	std::wstring command(commandLineBox->Text != nullptr ? commandLineBox->Text->Data() : L"");
	std::vector<std::wstring> tokens = TokenizeCommandLine(command);
	std::vector<std::wstring> additionalTokens = TokenizeCommandLine(BuildAdditionalArguments());
	size_t additionalStart = tokens.size();
	if (!additionalTokens.empty() && tokens.size() >= additionalTokens.size())
	{
		additionalStart = tokens.size() - additionalTokens.size();
	}

	for (size_t index = 0; index < tokens.size(); index++)
	{
		Run^ run = ref new Run();
		std::wstring text = QuoteCommandToken(tokens[index]);
		text += L" ";
		run->Text = ref new String(text.c_str());
		run->Foreground = ref new SolidColorBrush(index >= additionalStart ? additionalColor : automaticColor);
		paragraph->Inlines->Append(run);
	}

	commandPreviewBlock->Blocks->Append(paragraph);
}

bool DirectXPage::IsCommandLineFile(StorageFile^ file)
{
	if (file == nullptr)
	{
		return false;
	}

	std::wstring name(file->Name->Data());
	return name.length() >= 14 && _wcsicmp(name.c_str() + name.length() - 14, L".qemu_cmd_line") == 0;
}

String^ DirectXPage::QuoteForCommandLine(String^ value)
{
	std::wstring quoted = L"\"";
	if (value != nullptr)
	{
		for (const wchar_t* ch = value->Data(); *ch != L'\0'; ch++)
		{
			if (*ch == L'"')
			{
				quoted += L"\\\"";
			}
			else if (*ch == L'\\')
			{
				quoted += L"/";
			}
			else
			{
				quoted += *ch;
			}
		}
	}
	quoted += L"\"";
	return ref new String(quoted.c_str());
}

void DirectXPage::StartWithCommandFile(StorageFile^ commandFile)
{
	if (commandFile == nullptr)
	{
		AppendError(L"Could not create current.qemu_cmd_line.");
		SetStartState(false, false);
		return;
	}

	SetStatus(L"Loading " + SelectedQemuDllName() + L"...");
	auto dispatcher = Dispatcher;
	m_main->SetProgressCallback(std::function<void(const std::wstring&)>());

	Concurrency::create_task([this, commandFile, dispatcher]()
	{
		bool loaded = false;
		std::wstring message;
		try
		{
			loaded = m_main->LoadGame(commandFile, &message);
		}
		catch (Exception^ ex)
		{
			message = L"UWP exception while loading the QEMU DLL: ";
			if (ex != nullptr && ex->Message != nullptr)
			{
				message += ex->Message->Data();
			}
		}
		catch (const std::exception&)
		{
			message = L"C++ exception while loading the QEMU DLL.";
		}
		catch (...)
		{
			message = L"Unknown exception while loading the QEMU DLL.";
		}

		if (loaded)
		{
			dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this]()
			{
				AppendError(L"QEMU DLL loaded. Starting execution.");
				AppendError(m_main->ApiCompatibilityText());
				SetStatus(m_main->StatusText());
				topPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
				showTabsButton->Label = "Show settings";
				bottomAppBar->IsOpen = false;
				SetStartState(false, true);
			}));

			create_task([this, dispatcher]()
			{
				Sleep(15000);
				dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this]()
				{
					if (m_isRunning && m_expectHostVideoFrame && m_main->HasVideoFrame())
					{
						SetStatus(L"QEMU running with host video frames.");
					}
					else if (m_isRunning && m_expectHostVideoFrame)
					{
						AppendError(L"Video diagnostic: QEMU is running, but -display host has not emitted any framebuffer after 15 seconds.");
						AppendError(L"Video diagnostic: the current " + SelectedQemuDllName() + L" needs a host-display backend fix; the UWP renderer has not received a frame to draw.");
						SetStatus(L"QEMU running, but no host video frame was received.");
						tabPanel->SelectedIndex = 1;
					}
				}));
			});

			m_main->RunLoadedGame();
			dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this]()
			{
				SetStatus(m_main->StatusText());
				SetStartState(false, false);
			}));
			return;
		}

		auto error = std::make_shared<std::wstring>(message.empty()
			? L"Failed to load the QEMU DLL. No additional details were returned."
			: message);
		dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this, error]()
		{
			AppendError(*error);
			SetStatus(*error);
			tabPanel->SelectedIndex = 1;
			SetStartState(false, false);
		}));
	});
}
unsigned DirectXPage::MapVirtualKeyToQemuKey(VirtualKey key)
{
	unsigned keyValue = static_cast<unsigned>(key);
	if (key >= VirtualKey::Number0 && key <= VirtualKey::Number9)
	{
		return static_cast<unsigned>('0') + (keyValue - static_cast<unsigned>(VirtualKey::Number0));
	}
	if (key >= VirtualKey::A && key <= VirtualKey::Z)
	{
		return static_cast<unsigned>('a') + (keyValue - static_cast<unsigned>(VirtualKey::A));
	}
	if (key >= VirtualKey::NumberPad0 && key <= VirtualKey::NumberPad9)
	{
		return QEMU_KEY_KP0 + (keyValue - static_cast<unsigned>(VirtualKey::NumberPad0));
	}
	if (key >= VirtualKey::F1 && key <= VirtualKey::F15)
	{
		return QEMU_KEY_F1 + (keyValue - static_cast<unsigned>(VirtualKey::F1));
	}

	switch (key)
	{
	case VirtualKey::Back:
		return QEMU_KEY_BACKSPACE;
	case VirtualKey::Tab:
		return QEMU_KEY_TAB;
	case VirtualKey::Enter:
		return QEMU_KEY_RETURN;
	case VirtualKey::Escape:
		return QEMU_KEY_ESCAPE;
	case VirtualKey::Space:
		return QEMU_KEY_SPACE;
	case VirtualKey::Pause:
		return QEMU_KEY_PAUSE;
	case VirtualKey::CapitalLock:
		return QEMU_KEY_CAPSLOCK;
	case VirtualKey::Scroll:
		return QEMU_KEY_SCROLLOCK;
	case VirtualKey::NumberKeyLock:
		return QEMU_KEY_NUMLOCK;
	case VirtualKey::Snapshot:
		return QEMU_KEY_PRINT;
	case VirtualKey::Delete:
		return QEMU_KEY_DELETE;
	case VirtualKey::Left:
		return QEMU_KEY_LEFT;
	case VirtualKey::Right:
		return QEMU_KEY_RIGHT;
	case VirtualKey::Up:
		return QEMU_KEY_UP;
	case VirtualKey::Down:
		return QEMU_KEY_DOWN;
	case VirtualKey::Home:
		return QEMU_KEY_HOME;
	case VirtualKey::End:
		return QEMU_KEY_END;
	case VirtualKey::PageUp:
		return QEMU_KEY_PAGEUP;
	case VirtualKey::PageDown:
		return QEMU_KEY_PAGEDOWN;
	case VirtualKey::Insert:
		return QEMU_KEY_INSERT;
	case VirtualKey::Shift:
	case VirtualKey::LeftShift:
		return QEMU_KEY_LSHIFT;
	case VirtualKey::RightShift:
		return QEMU_KEY_RSHIFT;
	case VirtualKey::Control:
	case VirtualKey::LeftControl:
		return QEMU_KEY_LCTRL;
	case VirtualKey::RightControl:
		return QEMU_KEY_RCTRL;
	case VirtualKey::Menu:
	case VirtualKey::LeftMenu:
		return QEMU_KEY_LALT;
	case VirtualKey::RightMenu:
		return QEMU_KEY_RALT;
	case VirtualKey::Multiply:
		return QEMU_KEY_KP_MULTIPLY;
	case VirtualKey::Add:
		return QEMU_KEY_KP_PLUS;
	case VirtualKey::Separator:
		return QEMU_KEY_COMMA;
	case VirtualKey::Subtract:
		return QEMU_KEY_KP_MINUS;
	case VirtualKey::Decimal:
		return QEMU_KEY_KP_PERIOD;
	case VirtualKey::Divide:
		return QEMU_KEY_KP_DIVIDE;
	default:
		break;
	}

	switch (keyValue)
	{
	case 0xBA:
		return QEMU_KEY_SEMICOLON;
	case 0xBB:
		return QEMU_KEY_EQUALS;
	case 0xBC:
		return QEMU_KEY_COMMA;
	case 0xBD:
		return QEMU_KEY_MINUS;
	case 0xBE:
		return QEMU_KEY_PERIOD;
	case 0xBF:
		return QEMU_KEY_SLASH;
	case 0xC0:
		return QEMU_KEY_BACKQUOTE;
	case 0xDB:
		return QEMU_KEY_LEFTBRACKET;
	case 0xDC:
		return QEMU_KEY_BACKSLASH;
	case 0xDD:
		return QEMU_KEY_RIGHTBRACKET;
	case 0xDE:
		return QEMU_KEY_QUOTE;
	case 0xE2:
		return QEMU_KEY_LESS;
	default:
		return 0;
	}
}










