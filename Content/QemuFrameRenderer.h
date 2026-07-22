#pragma once

#include "..\Common\DeviceResources.h"
#include "..\QemuDirectHost.h"

namespace Qemu_UWP_host
{
	struct QemuFrameConstants
	{
		DirectX::XMFLOAT4 scaleOffset;
	};

	class QemuFrameRenderer
	{
	public:
		QemuFrameRenderer(
			const std::shared_ptr<DX::DeviceResources>& deviceResources,
			QemuDirectHost* host);

		void CreateWindowSizeDependentResources();
		void CreateDeviceDependentResources();
		void ReleaseDeviceDependentResources();
		bool Render();

	private:
		void EnsureFrameTexture(const QemuHostFrameSnapshot& frame);
		void UploadFrameTexture(const QemuHostFrameSnapshot& frame);
		QemuFrameConstants BuildConstants(const QemuHostFrameSnapshot& frame) const;

		std::shared_ptr<DX::DeviceResources> m_deviceResources;
		QemuDirectHost* m_host;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> m_frameTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_frameTextureView;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
		unsigned m_textureWidth;
		unsigned m_textureHeight;
		unsigned m_lastRenderedFrameNumber;
		bool m_loadingComplete;
		bool m_redrawRequired;
	};
}
