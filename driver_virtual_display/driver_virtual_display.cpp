//===================== Copyright (c) Valve Corporation. All Rights Reserved. ======================
//
// Example OpenVR driver for demonstrating IVRVirtualDisplay interface.
//
//==================================================================================================
#define _WINSOCKAPI_
#include "openvr_driver.h"
#include "sharedstate.h"
#include "threadtools.h"
#include "systemtime.h"
#include "d3drender.h"

#include <winsock2.h>
#include <D3dx9core.h>
#include <d3d11.h>
#include <wrl.h>
#include <map>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <directxcolors.h>
#include <d3d11_1.h>

#include "NvEncoderD3D11.h"
#include "Logger.h"
#include "NvCodecUtils.h"
#include "SpriteFont.h"
#include "UdpSender.h"
#include "nvencoderclioptions.h"
#include "Listener.h"
#include "Utils.h"

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();

namespace
{

	using Microsoft::WRL::ComPtr;


	const char *VERTEX_SHADER = 
		"Texture2D txLeft : register(t0);\n"
		"Texture2D txRight : register(t1);\n"
		"SamplerState samLinear : register(s0);\n"
		"\n"
		"struct VS_INPUT\n"
		"{\n"
		"	float4 Pos : POSITION;\n"
		"	float2 Tex : TEXCOORD0;\n"
		"};\n"
		"\n"
		"struct PS_INPUT\n"
		"{\n"
		"	float4 Pos : SV_POSITION;\n"
		"	float2 Tex : TEXCOORD0;\n"
		"};\n"
		"PS_INPUT VS(VS_INPUT input)\n"
		"{\n"
		"	PS_INPUT output = (PS_INPUT)0;\n"
		"	output.Pos = input.Pos;\n"
		"	output.Tex = input.Tex;\n"
		"\n"
		"	return output;\n"
		"}\n"
		"float4 PS(PS_INPUT input) : SV_Target\n"
		"{\n"
		//"float offset = (1448.0 - 1024.0) / 2 / 1448.0;\n"
		"float offset = 0.0;\n"
		"float shrink_to = 1.0 - offset * 2;\n"
		"float x = input.Tex.x;\n"
		"float y = input.Tex.y;\n"
		"	if (input.Tex.x < 0.5){\n"
		"		x = x * 2;\n"
		"		x = x * shrink_to + offset;\n"
		"		y = y * shrink_to + offset;\n"
		"		return txLeft.Sample(samLinear, float2(1.0 - x, 1.0 - y)); // We need this hack, because We cloud not resolve upside down issue by changing texcoord in buffer.\n"
		"	}else{\n"
		"		x = x * 2 - 1.0;\n"
		"		x = x * shrink_to + offset;\n"
		"		y = y * shrink_to + offset;\n"
		"		return txLeft.Sample(samLinear, float2(1.0 - x, 1.0 - y)); // We need this hack, because We cloud not resolve upside down issue by changing texcoord in buffer.\n"
		"	}\n"
		"}\n";
	const char *PIXEL_SHADER = VERTEX_SHADER;

	void Test(CD3DRender *m_pD3DRender, ID3D11Texture2D *pTexture, int nHeight) {

		D3D11_MAPPED_SUBRESOURCE mapped = { 0 };
		if (SUCCEEDED(m_pD3DRender->GetContext()->Map(pTexture, 0, D3D11_MAP_READ, 0, &mapped)))
		{
			Log("[VDispDvr] Test Mapped Texture");
			FILE *fp;
			fopen_s(&fp, "C:\\src\\virtual_display\\test.bmp", "w");
			fwrite(mapped.pData, mapped.RowPitch * nHeight, 1, fp);
			fclose(fp);

			m_pD3DRender->GetContext()->Unmap(pTexture, 0);
		}
	}

	void DrawDigitPixels(D3D11_MAPPED_SUBRESOURCE &mapped, int x, int y, int digit) {
		static const char map[][15] = {
		{ 1, 1, 1,
		 1, 0, 1,
		 1, 0, 1,
		 1, 0, 1,
		 1, 1, 1},
		{ 0, 1, 0,
		1, 1, 0,
		0, 1, 0,
		0, 1, 0,
		1, 1, 1},
		{ 1, 1, 0,
		1, 0, 1,
		0, 1, 0,
		1, 0, 0,
		1, 1, 1},
		{ 1, 1, 1,
		0, 0, 1,
		0, 1, 1,
		0, 0, 1,
		1, 1, 1},
		{ 1, 0, 1,
		1, 0, 1,
		1, 1, 1,
		0, 0, 1,
		0, 0, 1},
		{ 1, 1, 1,
		1, 0, 0,
		1, 1, 1,
		0, 0, 1,
		1, 1, 1},
		{ 1, 1, 0,
		1, 0, 0,
		1, 1, 1,
		1, 0, 1,
		1, 1, 1},
		{ 1, 1, 1,
		0, 0, 1,
		0, 1, 0,
		0, 1, 0,
		0, 1, 0},
		{ 1, 1, 1,
		1, 0, 1,
		1, 1, 1,
		1, 0, 1,
		1, 1, 1 },
		{ 1, 1, 1,
		1, 0, 1,
		1, 1, 1,
		0, 0, 1,
		0, 0, 1 }
		};
		if (digit < 0 || 9 < digit) {
			digit = 0;
		}
		uint8_t *p = (uint8_t *)mapped.pData;

		for (int i = 0; i < 5 * 2; i++) {
			for (int j = 0; j < 3 * 2; j++) {
				if (map[digit][i / 2 * 3 + j / 2]) {
					p[(y + i) * mapped.RowPitch + (x + j) * 4 + 0] = 0xff;
					p[(y + i) * mapped.RowPitch + (x + j) * 4 + 1] = 0xff;
					p[(y + i) * mapped.RowPitch + (x + j) * 4 + 2] = 0xff;
					p[(y + i) * mapped.RowPitch + (x + j) * 4 + 3] = 0xff;
				}

			}
		}
			
	}


	void DrawDebugTimestamp(CD3DRender *m_pD3DRender, ID3D11Texture2D *pTexture)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = { 0 };
		if (SUCCEEDED(m_pD3DRender->GetContext()->Map(pTexture, 0, D3D11_MAP_READ, 0, &mapped)))
		{
			int x = 10;
			int y = 10;

			FILETIME ft;
			SYSTEMTIME st2, st;

			GetSystemTimeAsFileTime(&ft);
			FileTimeToSystemTime(&ft, &st2);
			SystemTimeToTzSpecificLocalTime(NULL, &st2, &st);

			uint64_t q = (((uint64_t)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
			q /= 10;
			char buf[100];
			snprintf(buf, sizeof(buf),
				"%02d %02d %02d %03lld %03lld",
				st.wHour, st.wMinute, st.wSecond, q / 1000 % 1000, q % 1000);

			for (int i = 0; buf[i]; i++) {
				if (buf[i] != ' ') {
					DrawDigitPixels(mapped, x, y, buf[i] - '0');
				}
				x += 10;
			}

			m_pD3DRender->GetContext()->Unmap(pTexture, 0);
		}
	}


	inline vr::HmdQuaternion_t HmdQuaternion_Init(double w, double x, double y, double z)
	{
		vr::HmdQuaternion_t quat;
		quat.w = w;
		quat.x = x;
		quat.y = y;
		quat.z = z;
		return quat;
	}

	inline void HmdMatrix_SetIdentity(vr::HmdMatrix34_t *pMatrix)
	{
		pMatrix->m[0][0] = 1.f;
		pMatrix->m[0][1] = 0.f;
		pMatrix->m[0][2] = 0.f;
		pMatrix->m[0][3] = 0.f;
		pMatrix->m[1][0] = 0.f;
		pMatrix->m[1][1] = 1.f;
		pMatrix->m[1][2] = 0.f;
		pMatrix->m[1][3] = 0.f;
		pMatrix->m[2][0] = 0.f;
		pMatrix->m[2][1] = 0.f;
		pMatrix->m[2][2] = 1.f;
		pMatrix->m[2][3] = 0.f;
	}

		
	//-----------------------------------------------------------------------------
	// Settings
	//-----------------------------------------------------------------------------
	static const char * const k_pch_Settings_Section = "driver_remote_glass";
	static const char * const k_pch_Settings_SerialNumber_String = "serialNumber";
	static const char * const k_pch_Settings_ModelNumber_String = "modelNumber";
	static const char * const k_pch_Settings_WindowX_Int32 = "windowX";
	static const char * const k_pch_Settings_WindowY_Int32 = "windowY";
	static const char * const k_pch_Settings_WindowWidth_Int32 = "windowWidth";
	static const char * const k_pch_Settings_WindowHeight_Int32 = "windowHeight";
	static const char * const k_pch_Settings_RenderWidth_Int32 = "renderWidth";
	static const char * const k_pch_Settings_RenderHeight_Int32 = "renderHeight";
	static const char * const k_pch_Settings_IPD_Float = "IPD";
	static const char * const k_pch_Settings_SecondsFromVsyncToPhotons_Float = "secondsFromVsyncToPhotons";
	static const char * const k_pch_Settings_DisplayFrequency_Float = "displayFrequency";
	static const char * const k_pch_Settings_EncoderOptions_String = "nvencOptions";
	static const char * const k_pch_Settings_OutputFile_String = "outputFile";
	static const char * const k_pch_Settings_ReplayFile_String = "replayFile";
	static const char * const k_pch_Settings_LogFile_String = "logFile";
	static const char * const k_pch_Settings_DebugTimestamp_Bool = "debugTimestamp";
	static const char * const k_pch_Settings_ListenHost_String = "listenHost";
	static const char * const k_pch_Settings_ListenPort_Int32 = "listenPort";
	static const char * const k_pch_Settings_ControlListenHost_String = "controlListenHost";
	static const char * const k_pch_Settings_ControlListenPort_Int32 = "controlListenPort";

	static const char * const k_pch_Settings_AdditionalLatencyInSeconds_Float = "additionalLatencyInSeconds";
	static const char * const k_pch_Settings_DisplayWidth_Int32 = "displayWidth";
	static const char * const k_pch_Settings_DisplayHeight_Int32 = "displayHeight";
	static const char * const k_pch_Settings_DisplayRefreshRateNumerator_Int32 = "displayRefreshRateNumerator";
	static const char * const k_pch_Settings_DisplayRefreshRateDenominator_Int32 = "displayRefreshRateDenominator";
	static const char * const k_pch_Settings_AdapterIndex_Int32 = "adapterIndex";

	static const char * const k_pch_Settings_SrtOptions_String = "srtOptions";

	//-----------------------------------------------------------------------------

	class RGBToNV12ConverterD3D11 {
	public:
		RGBToNV12ConverterD3D11(ID3D11Device *pDevice, ID3D11DeviceContext *pContext, int nWidth, int nHeight)
			: pD3D11Device(pDevice), pD3D11Context(pContext)
		{
			pD3D11Device->AddRef();
			pD3D11Context->AddRef();

			pTexBgra = NULL;
			D3D11_TEXTURE2D_DESC desc;
			ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
			desc.Width = nWidth;
			desc.Height = nHeight;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			desc.CPUAccessFlags = 0;
			ck(pDevice->CreateTexture2D(&desc, NULL, &pTexBgra));

			ck(pDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void **)&pVideoDevice));
			ck(pContext->QueryInterface(__uuidof(ID3D11VideoContext), (void **)&pVideoContext));

			D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc =
			{
				D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
			{ 1, 1 }, desc.Width, desc.Height,
			{ 1, 1 }, desc.Width, desc.Height,
			D3D11_VIDEO_USAGE_PLAYBACK_NORMAL
			};
			ck(pVideoDevice->CreateVideoProcessorEnumerator(&contentDesc, &pVideoProcessorEnumerator));

			ck(pVideoDevice->CreateVideoProcessor(pVideoProcessorEnumerator, 0, &pVideoProcessor));
			D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc = { 0, D3D11_VPIV_DIMENSION_TEXTURE2D,{ 0, 0 } };
			ck(pVideoDevice->CreateVideoProcessorInputView(pTexBgra, pVideoProcessorEnumerator, &inputViewDesc, &pInputView));
		}

		~RGBToNV12ConverterD3D11()
		{
			for (auto& it : outputViewMap)
			{
				ID3D11VideoProcessorOutputView* pOutputView = it.second;
				pOutputView->Release();
			}

			pInputView->Release();
			pVideoProcessorEnumerator->Release();
			pVideoProcessor->Release();
			pVideoContext->Release();
			pVideoDevice->Release();
			pTexBgra->Release();
			pD3D11Context->Release();
			pD3D11Device->Release();
		}
		void ConvertRGBToNV12(ID3D11Texture2D*pRGBSrcTexture, ID3D11Texture2D* pDestTexture)
		{
			pD3D11Context->CopyResource(pTexBgra, pRGBSrcTexture);
			ID3D11VideoProcessorOutputView* pOutputView = nullptr;
			auto it = outputViewMap.find(pDestTexture);
			if (it == outputViewMap.end())
			{
				D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc = { D3D11_VPOV_DIMENSION_TEXTURE2D };
				ck(pVideoDevice->CreateVideoProcessorOutputView(pDestTexture, pVideoProcessorEnumerator, &outputViewDesc, &pOutputView));
				outputViewMap.insert({ pDestTexture, pOutputView });
			}
			else
			{
				pOutputView = it->second;
			}

			D3D11_VIDEO_PROCESSOR_STREAM stream = { TRUE, 0, 0, 0, 0, NULL, pInputView, NULL };
			ck(pVideoContext->VideoProcessorBlt(pVideoProcessor, pOutputView, 0, 1, &stream));
			return;
		}

	private:
		ID3D11Device * pD3D11Device = NULL;
		ID3D11DeviceContext *pD3D11Context = NULL;
		ID3D11VideoDevice *pVideoDevice = NULL;
		ID3D11VideoContext *pVideoContext = NULL;
		ID3D11VideoProcessor *pVideoProcessor = NULL;
		ID3D11VideoProcessorInputView *pInputView = NULL;
		ID3D11VideoProcessorOutputView *pOutputView = NULL;
		ID3D11Texture2D *pTexBgra = NULL;
		ID3D11VideoProcessorEnumerator *pVideoProcessorEnumerator = nullptr;
		std::unordered_map<ID3D11Texture2D*, ID3D11VideoProcessorOutputView*> outputViewMap;
	};


	//-----------------------------------------------------------------------------
	// Interface to separate process standing in for an actual remote device.
	// This needs to be a separate process because D3D blocks gpu work within
	// a process on Present.
	//-----------------------------------------------------------------------------
	class CNvEncoder
	{
	public:
		CNvEncoder(CD3DRender *pD3DRender)
			: m_flFrameIntervalInSeconds( 0.0f )
			, enc(NULL)
			, m_pD3DRender(pD3DRender)
			, m_bForceNv12(false)
			, m_nFrame(0)
			, m_Listener(NULL)
			, m_DebugTimestamp(false)
		{
		}

		~CNvEncoder()
		{}

		bool Initialize(
			std::string encoderOptions, std::string outputFile, std::string replayFile, Listener *listener,
			uint32_t nWindowX, uint32_t nWindowY, uint32_t nWindowWidth, uint32_t nWindowHeight,
			uint32_t nRefreshRateNumerator, uint32_t nRefreshRateDenominator,
			bool DebugTimestamp)
		{
			int nWidth = nWindowWidth;
			int nHeight = nWindowHeight;
			NvEncoderInitParam EncodeCLIOptions(encoderOptions.c_str());
			m_DebugTimestamp = DebugTimestamp;

			if (nWindowWidth == 0 || nWindowHeight == 0 ||
				nRefreshRateNumerator == 0 || nRefreshRateDenominator == 0)
			{
				Log("RemoteDevice: Invalid parameters. w=%d h=%d refresh=%d/%d",
					nWindowWidth, nWindowHeight, nRefreshRateNumerator, nRefreshRateDenominator);
				return false;
			}

			m_flFrameIntervalInSeconds = float(nRefreshRateDenominator) / nRefreshRateNumerator;

			if (m_bForceNv12)
			{
				pConverter.reset(new RGBToNV12ConverterD3D11(m_pD3DRender->GetDevice(), m_pD3DRender->GetContext(), nWidth, nHeight));
			}

			/// Initialize Encoder ///

			Log("CNvEncoder Initialize %dx%d %dx%d %p", nWindowX, nWindowY, nWindowWidth, nWindowHeight, m_pD3DRender->GetDevice());

			NV_ENC_BUFFER_FORMAT format = m_bForceNv12 ? NV_ENC_BUFFER_FORMAT_NV12 : NV_ENC_BUFFER_FORMAT_ARGB;
			format = NV_ENC_BUFFER_FORMAT_ABGR;
			enc = new NvEncoderD3D11(m_pD3DRender->GetDevice(), nWidth, nHeight, format);

			NV_ENC_INITIALIZE_PARAMS initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
			NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };
			initializeParams.encodeConfig = &encodeConfig;
			enc->CreateDefaultEncoderParams(&initializeParams, EncodeCLIOptions.GetEncodeGUID(), EncodeCLIOptions.GetPresetGUID());

			initializeParams.encodeConfig->encodeCodecConfig.h264Config.repeatSPSPPS = 1;

			EncodeCLIOptions.SetInitParams(&initializeParams, format);

			std::string parameterDesc = EncodeCLIOptions.FullParamToString(&initializeParams);
			Log("NvEnc Encoder Parameters:\n%s", parameterDesc.c_str());

			enc->CreateEncoder(&initializeParams);

			/// Initialize debug video output ///

			if (outputFile != "") {
				fpOut = std::ofstream(outputFile, std::ios::out | std::ios::binary);
				if (!fpOut)
				{
					Log("unable to open output file %s", outputFile.c_str());
				}
			}

			m_Listener = listener;

			return true;
		}

		void Shutdown()
		{
			std::vector<std::vector<uint8_t>> vPacket;
			enc->EndEncode(vPacket);
			for (std::vector<uint8_t> &packet : vPacket)
			{
				if (fpOut) {
					fpOut.write(reinterpret_cast<char*>(packet.data()), packet.size());
				}
				m_Listener->Send(packet.data(), (int)packet.size(), GetTimestampUs(), 0);
			}

			enc->DestroyEncoder();
			delete enc;

			Log("CNvEncoder::Shutdown");

			if (fpOut) {
				fpOut.close();
			}
		}

		float GetFrameIntervalInSeconds() const
		{
			return m_flFrameIntervalInSeconds;
		}

		void Transmit(ID3D11Texture2D *pTexture, uint64_t presentationTime, uint64_t frameIndex, uint64_t clientTime)
		{
			uint32_t nWidth;
			uint32_t nHeight;
			std::vector<std::vector<uint8_t>> vPacket;
			D3D11_TEXTURE2D_DESC desc;

			pTexture->GetDesc(&desc);

			Log("[VDispDvr] Transmit(begin)");

			nWidth = min(desc.Width, SharedState_t::MAX_TEXTURE_WIDTH);
			nHeight = min(desc.Height, SharedState_t::MAX_TEXTURE_HEIGHT);

			Log("Transmit %dx%d %d", nWidth, nHeight, desc.Format);

			const NvEncInputFrame* encoderInputFrame = enc->GetNextInputFrame();

			if (m_DebugTimestamp) {
				DrawDebugTimestamp(m_pD3DRender, pTexture);
			}

			if (m_bForceNv12)
			{
				ID3D11Texture2D *pNV12Textyure = reinterpret_cast<ID3D11Texture2D*>(encoderInputFrame->inputPtr);
				pConverter->ConvertRGBToNV12(pTexture, pNV12Textyure);
			}
			else
			{
				ID3D11Texture2D *pTexBgra = reinterpret_cast<ID3D11Texture2D*>(encoderInputFrame->inputPtr);
				Log("CopyResource start");
				uint64_t start = GetTimestampUs();

				D3D11_TEXTURE2D_DESC desc2;
				pTexBgra->GetDesc(&desc2);
				Log("%dx%d %d %d -> %dx%d %d %d",
					desc.Width, desc.Height, desc.Format, desc.BindFlags,
					desc2.Width, desc2.Height, desc2.Format, desc2.BindFlags);
				m_pD3DRender->GetContext()->CopyResource(pTexBgra, pTexture);
				uint64_t end = GetTimestampUs();
				Log("CopyResource end %lld us", end - start);
			}

			uint64_t start = GetTimestampUs();
			enc->EncodeFrame(vPacket);
			uint64_t end = GetTimestampUs();
			Log("EncodeFrame %lld us", end - start);

			Log("Tracking info delay: %lld us", GetTimestampUs() - m_Listener->clientToServerTime(clientTime));
			Log("Encoding delay: %lld us", GetTimestampUs() - presentationTime);

			m_nFrame += (int)vPacket.size();
			for (std::vector<uint8_t> &packet : vPacket)
			{
				fpOut.write(reinterpret_cast<char*>(packet.data()), packet.size());
				Log("Sending packet %d", (int)packet.size());
				if (m_Listener) {
					m_Listener->Send(packet.data(), (int)packet.size(), presentationTime, frameIndex);
				}
			}

			{
				CSharedState::Ptr data(&m_sharedState);
				data->m_flLastVsyncTimeInSeconds = SystemTime::GetInSeconds();
				data->m_nVsyncCounter++;
			}

			Log("[VDispDvr] Transmit(end) (frame %d %d)", vPacket.size(), m_nFrame);
		}

		void GetTimingInfo(double *pflLastVsyncTimeInSeconds, uint32_t *pnVsyncCounter)
		{
			CSharedState::Ptr data(&m_sharedState);
			*pflLastVsyncTimeInSeconds = data->m_flLastVsyncTimeInSeconds;
			*pnVsyncCounter = data->m_nVsyncCounter;
		}

	private:
		CSharedState m_sharedState;
		float m_flFrameIntervalInSeconds;
		std::ofstream fpOut;
		NvEncoderD3D11 *enc;

		CD3DRender *m_pD3DRender;
		bool m_bForceNv12;
		int m_nFrame;
		std::unique_ptr<RGBToNV12ConverterD3D11> pConverter;

		Listener *m_Listener;
		bool m_DebugTimestamp;
	};

	//----------------------------------------------------------------------------
	// Blocks on reading backbuffer from gpu, so WaitForPresent can return
	// as soon as we know rendering made it this frame.  This step of the pipeline
	// should run about 3ms per frame.
	//----------------------------------------------------------------------------
	class CEncoder : public CThread
	{
	public:
		CEncoder( CD3DRender *pD3DRender, CNvEncoder *pRemoteDevice, int renderWidth, int renderHeight )
			: m_pRemoteDevice( pRemoteDevice )
			, m_pD3DRender( pD3DRender )
			, m_pStagingTexture( NULL )
			, m_bExiting( false )
			, m_frameIndex(0)
			, m_renderWidth(renderWidth)
			, m_renderHeight(renderHeight)
		{
			m_encodeFinished.Set();
		}

		~CEncoder()
		{
		}

		bool CopyToStaging( ID3D11Texture2D *pTexture[], int textureNum, uint64_t presentationTime, uint64_t frameIndex, uint64_t clientTime )
		{
			// Create a staging texture to copy frame data into that can in turn
			// be read back (for blocking until rendering is finished).
			if ( !m_pStagingTexture )
			{
				D3D11_TEXTURE2D_DESC srcDesc;
				pTexture[0]->GetDesc( &srcDesc );

				D3D11_TEXTURE2D_DESC stagingTextureDesc;
				ZeroMemory( &stagingTextureDesc, sizeof( stagingTextureDesc ) );
				stagingTextureDesc.Width = m_renderWidth * 2;
				stagingTextureDesc.Height = m_renderHeight;
				stagingTextureDesc.Format = srcDesc.Format;
				stagingTextureDesc.MipLevels = 1;
				stagingTextureDesc.ArraySize = 1;
				stagingTextureDesc.SampleDesc.Count = 1;
				stagingTextureDesc.Usage = D3D11_USAGE_DEFAULT;
				//stagingTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				stagingTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

				if ( FAILED( m_pD3DRender->GetDevice()->CreateTexture2D( &stagingTextureDesc, NULL, &m_pStagingTexture ) ) )
				{
					Log( "Failed to create staging texture!" );
					return false;
				}

				HRESULT hr = m_pD3DRender->GetDevice()->CreateRenderTargetView(m_pStagingTexture.Get(), NULL, &m_pRenderTargetView);
				if (FAILED(hr)) {
					Log("CreateRenderTargetView %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}

				// Create depth stencil texture
				D3D11_TEXTURE2D_DESC descDepth;
				ZeroMemory(&descDepth, sizeof(descDepth));
				descDepth.Width = stagingTextureDesc.Width;
				descDepth.Height = stagingTextureDesc.Height;
				descDepth.MipLevels = 1;
				descDepth.ArraySize = 1;
				descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
				descDepth.SampleDesc.Count = 1;
				descDepth.SampleDesc.Quality = 0;
				descDepth.Usage = D3D11_USAGE_DEFAULT;
				descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
				descDepth.CPUAccessFlags = 0;
				descDepth.MiscFlags = 0;
				hr = m_pD3DRender->GetDevice()->CreateTexture2D(&descDepth, nullptr, &m_pDepthStencil);
				if (FAILED(hr)) {
					Log("CreateTexture2D %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}


				// Create the depth stencil view
				D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
				ZeroMemory(&descDSV, sizeof(descDSV));
				descDSV.Format = descDepth.Format;
				descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
				descDSV.Texture2D.MipSlice = 0;
				hr = m_pD3DRender->GetDevice()->CreateDepthStencilView(m_pDepthStencil.Get(), &descDSV, &m_pDepthStencilView);
				if (FAILED(hr)) {
					Log("CreateDepthStencilView %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}

				m_pD3DRender->GetContext()->OMSetRenderTargets(1, m_pRenderTargetView.GetAddressOf(), m_pDepthStencilView.Get());

				D3D11_VIEWPORT viewport;
				viewport.Width = (float)m_renderWidth * 2;
				viewport.Height = (float)m_renderHeight;
				viewport.MinDepth = 0.0f;
				viewport.MaxDepth = 1.0f;
				viewport.TopLeftX = 0;
				viewport.TopLeftY = 0;
				m_pD3DRender->GetContext()->RSSetViewports(1, &viewport);


				ID3DBlob *vshader, *pshader, *error;

				hr = D3DCompile(VERTEX_SHADER, strlen(VERTEX_SHADER), "vs", NULL, NULL, "VS", "vs_4_0", 0, 0, &vshader, &error);
				Log("D3DCompile vs %p %s", hr, GetDxErrorStr(hr).c_str());
				if (FAILED(hr)) {
					Log("%s", error->GetBufferPointer());
					return false;
				}
				if (error != NULL) {
					error->Release();
					error = NULL;
				}

				hr = m_pD3DRender->GetDevice()->CreateVertexShader((const DWORD*)vshader->GetBufferPointer(), vshader->GetBufferSize(), NULL, &m_pVertexShader);
				if (FAILED(hr)) {
					Log("CreateVertexShader %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}
				hr = D3DCompile(VERTEX_SHADER, strlen(VERTEX_SHADER), "ps", NULL, NULL, "PS", "ps_4_0", 0, 0, &pshader, &error);
				Log("D3DCompile ps %p %s", hr, GetDxErrorStr(hr).c_str());
				if (FAILED(hr)) {
					Log("%s", error->GetBufferPointer());
					return false;
				}
				if (error != NULL) {
					error->Release();
				}

				hr = m_pD3DRender->GetDevice()->CreatePixelShader((const DWORD*)pshader->GetBufferPointer(), pshader->GetBufferSize(), NULL, &m_pPixelShader);
				if (FAILED(hr)) {
					Log("CreatePixelShader %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}

				// Define the input layout
				D3D11_INPUT_ELEMENT_DESC layout[] =
				{
					{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				};
				UINT numElements = ARRAYSIZE(layout);


				// Create the input layout
				hr = m_pD3DRender->GetDevice()->CreateInputLayout(layout, numElements, vshader->GetBufferPointer(),
					vshader->GetBufferSize(), &m_pVertexLayout);
				if (FAILED(hr)) {
					Log("CreateInputLayout %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}
				vshader->Release();

				// Set the input layout
				m_pD3DRender->GetContext()->IASetInputLayout(m_pVertexLayout.Get());

				// src textures has 1448x1448 pixels but dest texture(remote display) has 1024x1024 pixels.
				// Apply offset to crop center of src textures.
				float tex_offset = (1448 - 1024) / 2 / 1448.0;
				tex_offset = 0;

				// Create vertex buffer
				SimpleVertex vertices[] =
				{
					{ DirectX::XMFLOAT3(-1.0f, -1.0f, 0.5f), DirectX::XMFLOAT2(1.0f - tex_offset, 0.0f + tex_offset) },
				    { DirectX::XMFLOAT3( 1.0f,  1.0f, 0.5f), DirectX::XMFLOAT2(0.0f + tex_offset, 1.0f - tex_offset) },
				    { DirectX::XMFLOAT3( 1.0f, -1.0f, 0.5f), DirectX::XMFLOAT2(0.0f + tex_offset, 0.0f + tex_offset) },
				    { DirectX::XMFLOAT3(-1.0f,  1.0f, 0.5f), DirectX::XMFLOAT2(1.0f - tex_offset, 1.0f - tex_offset) },
				};

				D3D11_BUFFER_DESC bd;
				ZeroMemory(&bd, sizeof(bd));
				bd.Usage = D3D11_USAGE_DEFAULT;
				bd.ByteWidth = sizeof(SimpleVertex) * 4;
				bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				bd.CPUAccessFlags = 0;
				D3D11_SUBRESOURCE_DATA InitData;
				ZeroMemory(&InitData, sizeof(InitData));
				InitData.pSysMem = vertices;
				hr = m_pD3DRender->GetDevice()->CreateBuffer(&bd, &InitData, &m_pVertexBuffer);
				if (FAILED(hr)) {
					Log("CreateBuffer 1 %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}

				// Set vertex buffer
				UINT stride = sizeof(SimpleVertex);
				UINT offset = 0;
				m_pD3DRender->GetContext()->IASetVertexBuffers(0, 1, m_pVertexBuffer.GetAddressOf(), &stride, &offset);
				
				// Create index buffer
				// Create vertex buffer
				WORD indices[] =
				{
					0,1,2,
					0,3,1
				};

				bd.Usage = D3D11_USAGE_DEFAULT;
				bd.ByteWidth = sizeof(WORD) * 6;
				bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
				bd.CPUAccessFlags = 0;
				InitData.pSysMem = indices;
				hr = m_pD3DRender->GetDevice()->CreateBuffer(&bd, &InitData, &m_pIndexBuffer);
				if (FAILED(hr)) {
					Log("CreateBuffer 2 %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}

				// Set index buffer
				m_pD3DRender->GetContext()->IASetIndexBuffer(m_pIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);

				// Set primitive topology
				m_pD3DRender->GetContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				// Create the sample state
				D3D11_SAMPLER_DESC sampDesc;
				ZeroMemory(&sampDesc, sizeof(sampDesc));
				sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
				sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
				sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
				sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
				sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
				sampDesc.MinLOD = 0;
				sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
				hr = m_pD3DRender->GetDevice()->CreateSamplerState(&sampDesc, &m_pSamplerLinear);
				if (FAILED(hr)) {
					Log("CreateSamplerState 5 %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}

				Log("Staging Texture created");
			}

			D3D11_TEXTURE2D_DESC srcDesc;
			pTexture[0]->GetDesc(&srcDesc);

			Log("CopyToStaging 0 %dx%d %d", srcDesc.Width, srcDesc.Height, srcDesc.Format);
			pTexture[1]->GetDesc(&srcDesc);

			Log("CopyToStaging 1 %dx%d %d", srcDesc.Width, srcDesc.Height, srcDesc.Format);

			m_presentationTime = presentationTime;
			m_frameIndex = frameIndex;
			m_clientTime = clientTime;
			
			if (textureNum == 1) {
				m_pD3DRender->GetContext()->CopyResource( m_pStagingTexture.Get(), pTexture[0] );
			}
			else {
				D3D11_BOX box = { 0 };
				box.right = srcDesc.Width;
				box.bottom = srcDesc.Height;
				box.back = 1;
				//m_pD3DRender->GetContext()->CopyResource(m_pStagingTexture, pTexture[1]);
				//m_pD3DRender->GetContext()->CopySubresourceRegion(m_pStagingTexture, 0, 0, 0, 0, pTexture[0], 0, 0);
				//m_pD3DRender->GetContext()->CopySubresourceRegion(m_pStagingTexture.Get(), 0, 0, 0, 0, pTexture[0], 0, &box);
				//m_pD3DRender->GetContext()->CopySubresourceRegion(m_pStagingTexture.Get(), 0, srcDesc.Width, 0, 0, pTexture[1], 0, &box);

				m_pD3DRender->GetContext()->Flush();

				//m_pD3DRender->GetContext()->Begin(NULL);
				// Update our time
				static float t = 0.0f;
				
				
				static ULONGLONG timeStart = 0;
				ULONGLONG timeCur = GetTickCount64();
				if (timeStart == 0)
					timeStart = timeCur;
				
				t = (timeCur - timeStart) / 1000.0f;
				float col = (GetTimestampUs() / 1000) / 10 % 256 / 256.0;

				D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
				SRVDesc.Format = srcDesc.Format;
				SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				SRVDesc.Texture2D.MostDetailedMip = 0;
				SRVDesc.Texture2D.MipLevels = 1;

				HRESULT hr = m_pD3DRender->GetDevice()->CreateShaderResourceView(pTexture[0], &SRVDesc, m_pShaderResourceView[0].ReleaseAndGetAddressOf());
				if (FAILED(hr)) {
					Log("CreateShaderResourceView %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}
				hr = m_pD3DRender->GetDevice()->CreateShaderResourceView(pTexture[1], &SRVDesc, m_pShaderResourceView[1].ReleaseAndGetAddressOf());
				if (FAILED(hr)) {
					Log("CreateShaderResourceView %p %s", hr, GetDxErrorStr(hr).c_str());
					return false;
				}

				//
				// Clear the back buffer
				//
				float color[4] = { 1.0, col, 1.0, 1.0 };//DirectX::Colors::MidnightBlue
				m_pD3DRender->GetContext()->ClearRenderTargetView(m_pRenderTargetView.Get(), color);

				//
				// Clear the depth buffer to 1.0 (max depth)
				//
				m_pD3DRender->GetContext()->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

				//
				// Render the cube
				//
				m_pD3DRender->GetContext()->VSSetShader(m_pVertexShader.Get(), nullptr, 0);
				m_pD3DRender->GetContext()->PSSetShader(m_pPixelShader.Get(), nullptr, 0);

				ID3D11ShaderResourceView *shaderResourceView[2] = { m_pShaderResourceView[0].Get(), m_pShaderResourceView[1].Get() };
				m_pD3DRender->GetContext()->PSSetShaderResources(0, 2, shaderResourceView);
				//m_pD3DRender->GetContext()->PSSetShaderResources(0, 1, shaderResourceView);

				m_pD3DRender->GetContext()->PSSetSamplers(0, 1, m_pSamplerLinear.GetAddressOf());
				m_pD3DRender->GetContext()->DrawIndexed(6, 0, 0);
				m_pD3DRender->GetContext()->Flush();
			}

			return true;
		}

		void Run() override
		{
			SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_MOST_URGENT );

			while ( !m_bExiting )
			{
				Log( "[VDispDvr] Encoder waiting for new frame..." );

				m_newFrameReady.Wait();
				if ( m_bExiting )
					break;

				if ( m_pStagingTexture )
				{
					m_pRemoteDevice->Transmit( m_pStagingTexture.Get(), m_presentationTime, m_frameIndex, m_clientTime);
				}

				m_encodeFinished.Set();
			}
		}

		void Stop()
		{
			m_bExiting = true;
			m_newFrameReady.Set();
			Join();
		}

		void NewFrameReady( double flVsyncTimeInSeconds )
		{
			Log("New Frame Ready");
			m_flVsyncTimeInSeconds = flVsyncTimeInSeconds;
			m_encodeFinished.Reset();
			m_newFrameReady.Set();
		}

		void WaitForEncode()
		{
			m_encodeFinished.Wait();
		}

	private:
		CThreadEvent m_newFrameReady, m_encodeFinished;
		CNvEncoder *m_pRemoteDevice;
		CD3DRender *m_pD3DRender;
		double m_flVsyncTimeInSeconds;
		bool m_bExiting;
		uint64_t m_presentationTime;
		uint64_t m_frameIndex;
		uint64_t m_clientTime;

		int m_renderWidth;
		int m_renderHeight;
		ComPtr<ID3D11Texture2D> m_pStagingTexture;

		ComPtr<ID3D11VertexShader> m_pVertexShader;
		ComPtr<ID3D11PixelShader> m_pPixelShader;

		ComPtr<ID3D11InputLayout> m_pVertexLayout;
		ComPtr<ID3D11Buffer> m_pVertexBuffer;
		ComPtr<ID3D11Buffer> m_pIndexBuffer;

		ComPtr<ID3D11SamplerState> m_pSamplerLinear;

		ComPtr<ID3D11Texture2D> m_pDepthStencil;
		ComPtr<ID3D11ShaderResourceView> m_pShaderResourceView[2];
		ComPtr<ID3D11RenderTargetView> m_pRenderTargetView;
		ComPtr<ID3D11DepthStencilView> m_pDepthStencilView;

		struct SimpleVertex
		{
			DirectX::XMFLOAT3 Pos;
			DirectX::XMFLOAT2 Tex;
		};
	};
}

// VSync Event Thread

class VSyncThread : public CThread
{
public:
	VSyncThread() : m_bExit(false) {}

	void Run()override {
		while (!m_bExit) {
			uint64_t prev = GetTimestampUs();
			Log("Generate VSync Event");
			vr::VRServerDriverHost()->VsyncEvent(0);
			Sleep(((prev + 16666) - GetTimestampUs()) / 1000);
		}
	}

	void Shutdown() {
		m_bExit = true;
	}
private:
	bool m_bExit;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CRemoteHmd : public vr::ITrackedDeviceServerDriver, public vr::IVRDisplayComponent, public vr::IVRVirtualDisplay, public vr::IVRDriverDirectModeComponent
{
public:
	CRemoteHmd()
		: m_unObjectId(vr::k_unTrackedDeviceIndexInvalid)
		, m_nGraphicsAdapterLuid(0)
		, m_flLastVsyncTimeInSeconds(0.0)
		, m_nVsyncCounter(0)
		, m_pD3DRender(NULL)
		, m_pFlushTexture(NULL)
		, m_pRemoteDevice(NULL)
		, m_pEncoder(NULL)
		, m_EncoderOptions("")
		, m_DebugTimestamp(false)
		, m_Listener(NULL)
		, m_VSyncThread(NULL)
		, m_poseMutex(NULL)
	{
		std::string logFile;
		std::string host, control_host;
		int port, control_port;

		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
		m_ulPropertyContainer = vr::k_ulInvalidPropertyContainer;

		Log("Using settings values");

		char buf[10240];
		vr::VRSettings()->GetString(k_pch_Settings_Section, k_pch_Settings_SerialNumber_String, buf, sizeof(buf));
		m_sSerialNumber = buf;

		vr::VRSettings()->GetString(k_pch_Settings_Section, k_pch_Settings_ModelNumber_String, buf, sizeof(buf));
		m_sModelNumber = buf;

		m_nWindowX = vr::VRSettings()->GetInt32(k_pch_Settings_Section, k_pch_Settings_WindowX_Int32);
		m_nWindowY = vr::VRSettings()->GetInt32(k_pch_Settings_Section, k_pch_Settings_WindowY_Int32);
		m_nWindowWidth = vr::VRSettings()->GetInt32(k_pch_Settings_Section, k_pch_Settings_WindowWidth_Int32);
		m_nWindowHeight = vr::VRSettings()->GetInt32(k_pch_Settings_Section, k_pch_Settings_WindowHeight_Int32);
		m_nRenderWidth = vr::VRSettings()->GetInt32(k_pch_Settings_Section, k_pch_Settings_RenderWidth_Int32);
		m_nRenderHeight = vr::VRSettings()->GetInt32(k_pch_Settings_Section, k_pch_Settings_RenderHeight_Int32);
		m_flSecondsFromVsyncToPhotons = vr::VRSettings()->GetFloat(k_pch_Settings_Section, k_pch_Settings_SecondsFromVsyncToPhotons_Float);
		m_flDisplayFrequency = vr::VRSettings()->GetFloat(k_pch_Settings_Section, k_pch_Settings_DisplayFrequency_Float);

		vr::VRSettings()->GetString(k_pch_Settings_Section, k_pch_Settings_EncoderOptions_String, buf, sizeof(buf));
		m_EncoderOptions = buf;
		vr::VRSettings()->GetString(k_pch_Settings_Section, k_pch_Settings_OutputFile_String, buf, sizeof(buf));
		m_OutputFile = buf;
		vr::VRSettings()->GetString(k_pch_Settings_Section, k_pch_Settings_LogFile_String, buf, sizeof(buf));
		logFile = buf;
		vr::VRSettings()->GetString(k_pch_Settings_Section, k_pch_Settings_ReplayFile_String, buf, sizeof(buf));
		m_ReplayFile = buf;
		vr::VRSettings()->GetString(k_pch_Settings_Section, k_pch_Settings_SrtOptions_String, buf, sizeof(buf));
		std::string SrtOptions = buf;

		// Listener Parameters
		vr::VRSettings()->GetString(k_pch_Settings_Section, k_pch_Settings_ListenHost_String, buf, sizeof(buf));
		host = buf;
		port = vr::VRSettings()->GetInt32(k_pch_Settings_Section, k_pch_Settings_ListenPort_Int32);
		
		vr::VRSettings()->GetString(k_pch_Settings_Section, k_pch_Settings_ControlListenHost_String, buf, sizeof(buf));
		control_host = buf;
		control_port = vr::VRSettings()->GetInt32(k_pch_Settings_Section, k_pch_Settings_ControlListenPort_Int32);

		m_DebugTimestamp = vr::VRSettings()->GetBool(k_pch_Settings_Section, k_pch_Settings_DebugTimestamp_Bool);
		

		logger = simplelogger::LoggerFactory::CreateFileLogger(logFile);


		float originalIPD = vr::VRSettings()->GetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_IPD_Float);

		m_flIPD = vr::VRSettings()->GetFloat(k_pch_Settings_Section, k_pch_Settings_IPD_Float);
		vr::VRSettings()->SetFloat(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_IPD_Float, m_flIPD);

		Log("driver_null: Serial Number: %s", m_sSerialNumber.c_str());
		Log("driver_null: Model Number: %s", m_sModelNumber.c_str());
		Log("driver_null: Window: %d %d %d %d", m_nWindowX, m_nWindowY, m_nWindowWidth, m_nWindowHeight);
		Log("driver_null: Render Target: %d %d", m_nRenderWidth, m_nRenderHeight);
		Log("driver_null: Seconds from Vsync to Photons: %f", m_flSecondsFromVsyncToPhotons);
		Log("driver_null: Display Frequency: %f", m_flDisplayFrequency);
		Log("driver_null: IPD: %f", m_flIPD);

		Log("driver_null: EncoderOptions: %s%s", m_EncoderOptions.c_str(), m_EncoderOptions.size() == sizeof(buf) - 1 ? " (Maybe truncated)" : "");
		Log("driver_null: OutputFile: %s%s", m_OutputFile.c_str(), m_OutputFile.size() == sizeof(buf) - 1 ? " (Maybe truncated)" : "");
		Log("driver_null: ReplayFile: %s%s", m_ReplayFile.c_str(), m_ReplayFile.size() == sizeof(buf) - 1 ? " (Maybe truncated)" : "");


		//CDisplayRedirectLatest()

		m_flAdditionalLatencyInSeconds = max(0.0f,
			vr::VRSettings()->GetFloat(k_pch_Settings_Section,
				k_pch_Settings_AdditionalLatencyInSeconds_Float));

		uint32_t nDisplayWidth = vr::VRSettings()->GetInt32(
			k_pch_Settings_Section,
			k_pch_Settings_DisplayWidth_Int32);
		uint32_t nDisplayHeight = vr::VRSettings()->GetInt32(
			k_pch_Settings_Section,
			k_pch_Settings_DisplayHeight_Int32);

		int32_t nDisplayRefreshRateNumerator = vr::VRSettings()->GetInt32(
			k_pch_Settings_Section,
			k_pch_Settings_DisplayRefreshRateNumerator_Int32);
		int32_t nDisplayRefreshRateDenominator = vr::VRSettings()->GetInt32(
			k_pch_Settings_Section,
			k_pch_Settings_DisplayRefreshRateDenominator_Int32);

		int32_t nAdapterIndex = vr::VRSettings()->GetInt32(
			k_pch_Settings_Section,
			k_pch_Settings_AdapterIndex_Int32);

		m_pD3DRender = new CD3DRender();

		// First initialize using the specified display dimensions to determine
		// which graphics adapter the headset is attached to (if any).
		if (!m_pD3DRender->Initialize(nDisplayWidth, nDisplayHeight))
		{
			Log("Could not find headset with display size %dx%d.", nDisplayWidth, nDisplayHeight);
			return;
		}

		int32_t nDisplayX, nDisplayY;
		m_pD3DRender->GetDisplayPos(&nDisplayX, &nDisplayY);
		//m_pD3DRender->GetDisplaySize(&nDisplayWidth, &nDisplayHeight);

		int32_t nDisplayAdapterIndex;
		const int32_t nBufferSize = 128;
		wchar_t wchAdapterDescription[nBufferSize];
		if (!m_pD3DRender->GetAdapterInfo(&nDisplayAdapterIndex, wchAdapterDescription, nBufferSize))
		{
			Log("Failed to get headset adapter info!");
			return;
		}

		char chAdapterDescription[nBufferSize];
		wcstombs_s(0, chAdapterDescription, nBufferSize, wchAdapterDescription, nBufferSize);
		Log("Headset connected to %s.", chAdapterDescription);

		Log("Adapter Index: %d %d", nAdapterIndex, nDisplayAdapterIndex);

		// If no adapter specified, choose the first one the headset *isn't* plugged into.
		if (nAdapterIndex < 0)
		{
			nAdapterIndex = (nDisplayAdapterIndex == 0) ? 1 : 0;
		}
		else if (nDisplayAdapterIndex == nAdapterIndex)
		{
			Log("Headset needs to be plugged into a separate graphics card.");
			return;
		}

		nAdapterIndex = 0;

		// Store off the LUID of the primary gpu we want to use.
		if (!m_pD3DRender->GetAdapterLuid(nAdapterIndex, &m_nGraphicsAdapterLuid))
		{
			Log("Failed to get adapter index for graphics adapter!");
			return;
		}

		// Now reinitialize using the other graphics card.
		if (!m_pD3DRender->Initialize(nAdapterIndex))
		{
			Log("Could not create graphics device for adapter %d.  Requires a minimum of two graphics cards.", nAdapterIndex);
			return;
		}

		if (!m_pD3DRender->GetAdapterInfo(&nDisplayAdapterIndex, wchAdapterDescription, nBufferSize))
		{
			Log("Failed to get primary adapter info!");
			return;
		}

		wcstombs_s(0, chAdapterDescription, nBufferSize, wchAdapterDescription, nBufferSize);
		Log("Using %s as primary graphics adapter.", chAdapterDescription);

		std::function<void(sockaddr_in *)> Callback = [&](sockaddr_in *a) { ListenerCallback(a); };
		std::function<void()> poseCallback = [&]() { OnPoseUpdated(); };
		m_Listener = new Listener(host, port, control_host, control_port, SrtOptions, Callback, poseCallback);
		m_Listener->Start();

		// Spawn our separate process to manage headset presentation.
		m_pRemoteDevice = new CNvEncoder(m_pD3DRender);
		if (!m_pRemoteDevice->Initialize(
			m_EncoderOptions, m_OutputFile, m_ReplayFile, m_Listener,
			nDisplayX, nDisplayY, nDisplayWidth, nDisplayHeight,
			nDisplayRefreshRateNumerator, nDisplayRefreshRateDenominator,
			m_DebugTimestamp))
		{
			return;
		}

		// Spin up a separate thread to handle the overlapped encoding/transmit step.
		m_pEncoder = new CEncoder(m_pD3DRender, m_pRemoteDevice, m_nRenderWidth, m_nRenderHeight);
		m_pEncoder->Start();

		m_VSyncThread = new VSyncThread();
		m_VSyncThread->Start();
	}

	virtual ~CRemoteHmd()
	{
		if (m_pEncoder)
		{
			m_pEncoder->Stop();
			delete m_pEncoder;
		}

		if (m_pRemoteDevice)
		{
			m_pRemoteDevice->Shutdown();
			delete m_pRemoteDevice;
		}

		if (m_Listener)
		{
			m_Listener->Stop();
			delete m_Listener;
		}

		if (m_VSyncThread)
		{
			m_VSyncThread->Shutdown();
			delete m_VSyncThread;
		}

		if (m_pFlushTexture)
		{
			m_pFlushTexture->Release();
		}

		if (m_pD3DRender)
		{
			m_pD3DRender->Shutdown();
			delete m_pD3DRender;
		}
	}


	virtual vr::EVRInitError Activate(vr::TrackedDeviceIndex_t unObjectId)
	{
		Log("CRemoteHmd Activate %d", unObjectId);

		m_unObjectId = unObjectId;
		m_ulPropertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_unObjectId);


		vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_ModelNumber_String, m_sModelNumber.c_str());
		vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_RenderModelName_String, m_sModelNumber.c_str());
		vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, vr::Prop_UserIpdMeters_Float, m_flIPD);
		vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.f);
		vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, vr::Prop_DisplayFrequency_Float, m_flDisplayFrequency);
		vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer, vr::Prop_SecondsFromVsyncToPhotons_Float, m_flSecondsFromVsyncToPhotons);

		// return a constant that's not 0 (invalid) or 1 (reserved for Oculus)
		vr::VRProperties()->SetUint64Property(m_ulPropertyContainer, vr::Prop_CurrentUniverseId_Uint64, 2);

		// avoid "not fullscreen" warnings from vrmonitor
		vr::VRProperties()->SetBoolProperty(m_ulPropertyContainer, vr::Prop_IsOnDesktop_Bool, false);

		// Manually send VSync events on direct mode. ref:https://github.com/ValveSoftware/virtual_display/issues/1
		vr::VRProperties()->SetBoolProperty(m_ulPropertyContainer, vr::Prop_DriverDirectModeSendsVsyncEvents_Bool, true);

		// Icons can be configured in code or automatically configured by an external file "drivername\resources\driver.vrresources".
		// Icon properties NOT configured in code (post Activate) are then auto-configured by the optional presence of a driver's "drivername\resources\driver.vrresources".
		// In this manner a driver can configure their icons in a flexible data driven fashion by using an external file.
		//
		// The structure of the driver.vrresources file allows a driver to specialize their icons based on their HW.
		// Keys matching the value in "Prop_ModelNumber_String" are considered first, since the driver may have model specific icons.
		// An absence of a matching "Prop_ModelNumber_String" then considers the ETrackedDeviceClass ("HMD", "Controller", "GenericTracker", "TrackingReference")
		// since the driver may have specialized icons based on those device class names.
		//
		// An absence of either then falls back to the "system.vrresources" where generic device class icons are then supplied.
		//
		// Please refer to "bin\drivers\sample\resources\driver.vrresources" which contains this sample configuration.
		//
		// "Alias" is a reserved key and specifies chaining to another json block.
		//
		// In this sample configuration file (overly complex FOR EXAMPLE PURPOSES ONLY)....
		//
		// "Model-v2.0" chains through the alias to "Model-v1.0" which chains through the alias to "Model-v Defaults".
		//
		// Keys NOT found in "Model-v2.0" would then chase through the "Alias" to be resolved in "Model-v1.0" and either resolve their or continue through the alias.
		// Thus "Prop_NamedIconPathDeviceAlertLow_String" in each model's block represent a specialization specific for that "model".
		// Keys in "Model-v Defaults" are an example of mapping to the same states, and here all map to "Prop_NamedIconPathDeviceOff_String".
		//
		bool bSetupIconUsingExternalResourceFile = true;
		if (!bSetupIconUsingExternalResourceFile)
		{
			// Setup properties directly in code.
			// Path values are of the form {drivername}\icons\some_icon_filename.png
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceOff_String, "{virtual_display}/icons/headset_sample_status_off.png");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceSearching_String, "{virtual_display}/icons/headset_sample_status_searching.gif");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{virtual_display}/icons/headset_sample_status_searching_alert.gif");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceReady_String, "{virtual_display}/icons/headset_sample_status_ready.png");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{virtual_display}/icons/headset_sample_status_ready_alert.png");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceNotReady_String, "{virtual_display}/icons/headset_sample_status_error.png");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceStandby_String, "{virtual_display}/icons/headset_sample_status_standby.png");
			vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_NamedIconPathDeviceAlertLow_String, "{virtual_display}/icons/headset_sample_status_ready_low.png");
		}

		vr::VRProperties()->SetFloatProperty(m_ulPropertyContainer,
			vr::Prop_SecondsFromVsyncToPhotons_Float, m_flAdditionalLatencyInSeconds);
		vr::VRProperties()->SetUint64Property(m_ulPropertyContainer,
			vr::Prop_GraphicsAdapterLuid_Uint64, m_nGraphicsAdapterLuid);

		return vr::VRInitError_None;
	}

	virtual void Deactivate()
	{
		Log("CRemoteHmd Deactivate");
		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
	}

	virtual void EnterStandby()
	{
	}

	void *GetComponent(const char *pchComponentNameAndVersion)
	{
		Log("GetComponent %s", pchComponentNameAndVersion);
		if (!_stricmp(pchComponentNameAndVersion, vr::IVRDisplayComponent_Version))
		{
			return (vr::IVRDisplayComponent*)this;
		}
		if (!_stricmp(pchComponentNameAndVersion, vr::IVRVirtualDisplay_Version))
		{
			//return static_cast< vr::IVRVirtualDisplay * >(this);
		}
		if (!_stricmp(pchComponentNameAndVersion, vr::IVRDriverDirectModeComponent_Version))
		{
			return static_cast< vr::IVRDriverDirectModeComponent * >(this);
		}

		// override this to add a component to a driver
		return NULL;
	}

	virtual void PowerOff()
	{
	}

	/** debug request from a client */
	virtual void DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize)
	{
		if (unResponseBufferSize >= 1)
			pchResponseBuffer[0] = 0;
	}

	virtual void GetWindowBounds(int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight)
	{
		Log("GetWindowBounds %dx%x %dx%d", m_nWindowX, m_nWindowY, m_nWindowWidth, m_nWindowHeight);
		*pnX = m_nWindowX;
		*pnY = m_nWindowY;
		*pnWidth = m_nWindowWidth;
		*pnHeight = m_nWindowHeight;
	}

	virtual bool IsDisplayOnDesktop()
	{
		return false;
	}

	virtual bool IsDisplayRealDisplay()
	{
		return false;
	}

	virtual void GetRecommendedRenderTargetSize(uint32_t *pnWidth, uint32_t *pnHeight)
	{
		*pnWidth = m_nRenderWidth;
		*pnHeight = m_nRenderHeight;
		Log("GetRecommendedRenderTargetSize %dx%d", *pnWidth, *pnHeight);
	}

	virtual void GetEyeOutputViewport(vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight)
	{
		*pnY = 0;
		*pnWidth = m_nWindowWidth / 2;
		*pnHeight = m_nWindowHeight;

		if (eEye == vr::Eye_Left)
		{
			*pnX = 0;
		}
		else
		{
			*pnX = m_nWindowWidth / 2;
		}
		Log("GetEyeOutputViewport %d %dx%d %dx%d", eEye, *pnX, *pnY, *pnWidth, *pnHeight);
	}

	virtual void GetProjectionRaw(vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom)
	{
		if (eEye == vr::Eye_Left)
		{
			*pfLeft = -1.0;
			*pfRight = 1.0;
			*pfTop = -1.0;
			*pfBottom = 1.0;
		}
		else {
			*pfLeft = -1.0;
			*pfRight = 1.0;
			*pfTop = -1.0;
			*pfBottom = 1.0;
		}
		Log("GetProjectionRaw %d", eEye);
	}

	virtual vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye eEye, float fU, float fV)
	{
		vr::DistortionCoordinates_t coordinates;
		coordinates.rfBlue[0] = fU;
		coordinates.rfBlue[1] = fV;
		coordinates.rfGreen[0] = fU;
		coordinates.rfGreen[1] = fV;
		coordinates.rfRed[0] = fU;
		coordinates.rfRed[1] = fV;
		return coordinates;
	}

	// ITrackedDeviceServerDriver


	virtual vr::DriverPose_t GetPose()
	{
		vr::DriverPose_t pose = { 0 };
		pose.poseIsValid = true;
		pose.result = vr::TrackingResult_Running_OK;
		pose.deviceIsConnected = true;
		//pose.shouldApplyHeadModel = true;
		//pose.willDriftInYaw = true;

		pose.qWorldFromDriverRotation = HmdQuaternion_Init(1, 0, 0, 0);
		pose.qDriverFromHeadRotation = HmdQuaternion_Init(1, 0, 0, 0);
		pose.qRotation = HmdQuaternion_Init(1, 0, 0, 0);

		if (m_Listener->HasValidTrackingInfo()) {
			auto& info = m_Listener->GetTrackingInfo();
			uint64_t trackingDelay = GetTimestampUs() - m_Listener->clientToServerTime(info.clientTime);

			Log("Tracking elapsed:%lld us %lld quot:%f,%f,%f,%f\nposition:%f,%f,%f\nView[0]:\n%sProj[0]:\n%sView[1]:\n%sProj[1]:\n%s",
				trackingDelay,
				info.FrameIndex,
				info.HeadPose_Pose_Orientation.x,
				info.HeadPose_Pose_Orientation.y,
				info.HeadPose_Pose_Orientation.z,
				info.HeadPose_Pose_Orientation.w,
				info.HeadPose_Pose_Position.x,
				info.HeadPose_Pose_Position.y,
				info.HeadPose_Pose_Position.z,
				DumpMatrix(info.Eye[0].ViewMatrix.M).c_str(),
				DumpMatrix(info.Eye[0].ProjectionMatrix.M).c_str(),
				DumpMatrix(info.Eye[1].ViewMatrix.M).c_str(),
				DumpMatrix(info.Eye[1].ProjectionMatrix.M).c_str()
			);

			pose.qRotation.x = info.HeadPose_Pose_Orientation.x;
			pose.qRotation.y = info.HeadPose_Pose_Orientation.y;
			pose.qRotation.z = info.HeadPose_Pose_Orientation.z;
			pose.qRotation.w = info.HeadPose_Pose_Orientation.w;

			pose.vecPosition[0] = info.HeadPose_Pose_Position.x;
			pose.vecPosition[1] = info.HeadPose_Pose_Position.y;
			pose.vecPosition[2] = info.HeadPose_Pose_Position.z;

			/*
			pose.vecVelocity[0] = info.HeadPose_LinearVelocity.x;
			pose.vecVelocity[1] = info.HeadPose_LinearVelocity.y;
			pose.vecVelocity[2] = info.HeadPose_LinearVelocity.z;

			pose.vecAcceleration[0] = info.HeadPose_LinearAcceleration.x;
			pose.vecAcceleration[1] = info.HeadPose_LinearAcceleration.y;
			pose.vecAcceleration[2] = info.HeadPose_LinearAcceleration.z;

			pose.vecAngularVelocity[0] = info.HeadPose_AngularVelocity.x;
			pose.vecAngularVelocity[1] = info.HeadPose_AngularVelocity.y;
			pose.vecAngularVelocity[2] = info.HeadPose_AngularVelocity.z;

			pose.vecAngularAcceleration[0] = info.HeadPose_AngularAcceleration.x;
			pose.vecAngularAcceleration[1] = info.HeadPose_AngularAcceleration.y;
			pose.vecAngularAcceleration[2] = info.HeadPose_AngularAcceleration.z;*/

			//pose.poseTimeOffset = -(trackingDelay * 1.0) / 1000.0 / 1000.0;
			pose.poseTimeOffset = 0;

			m_LastReferencedFrameIndex = info.FrameIndex;
			m_LastReferencedClientTime = info.clientTime;

			m_poseMutex.Wait(INFINITE);
			if (m_poseBuffer.size() != 0) {
				m_poseBuffer.push_back(info);
			}
			else {
				if (m_poseBuffer.back().FrameIndex != info.FrameIndex) {
					// New track info
					m_poseBuffer.push_back(info);
				}
			}
			if (m_poseBuffer.size() > 10) {
				m_poseBuffer.pop_front();
			}
			m_poseMutex.Release();
		}

		return pose;
	}


	void RunFrame()
	{
		// In a real driver, this should happen from some pose tracking thread.
		// The RunFrame interval is unspecified and can be very irregular if some other
		// driver blocks it for some periodic task.
		if (m_unObjectId != vr::k_unTrackedDeviceIndexInvalid)
		{
			Log("RunFrame");
			vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_unObjectId, GetPose(), sizeof(vr::DriverPose_t));
		}
	}

	std::string GetSerialNumber() const { return m_sSerialNumber; }

private:
	vr::TrackedDeviceIndex_t m_unObjectId;
	vr::PropertyContainerHandle_t m_ulPropertyContainer;

	std::string m_sSerialNumber;
	std::string m_sModelNumber;

	int32_t m_nWindowX;
	int32_t m_nWindowY;
	int32_t m_nWindowWidth;
	int32_t m_nWindowHeight;
	int32_t m_nRenderWidth;
	int32_t m_nRenderHeight;
	float m_flSecondsFromVsyncToPhotons;
	float m_flDisplayFrequency;
	float m_flIPD;

	std::string m_EncoderOptions;
	std::string m_OutputFile;
	std::string m_ReplayFile;
	bool m_DebugTimestamp;

	uint64_t m_LastReferencedFrameIndex;
	uint64_t m_LastReferencedClientTime;

	IPCMutex m_poseMutex;
	std::list<Listener::TrackingInfo> m_poseBuffer;

public:
	bool IsValid() const
	{
		return m_pEncoder != NULL;
	}


	// IVRVirtualDisplay

	virtual void Present2(vr::SharedTextureHandle_t backbufferTextureHandle) 
	{
		Log("Present %p", backbufferTextureHandle);
		// Open and cache our shared textures to avoid re-opening every frame.
		ID3D11Texture2D *pTexture = m_pD3DRender->GetSharedTexture((HANDLE)backbufferTextureHandle);
		if (pTexture == NULL)
		{
			Log("[VDispDvr] Texture is NULL!");
		}
		else
		{
			Log("[VDispDvr] Waiting for previous encode to finish...");

			// Wait for the encoder to be ready.  This is important because the encoder thread
			// blocks on transmit which uses our shared d3d context (which is not thread safe).
			m_pEncoder->WaitForEncode();

			Log("[VDispDvr] Done");

			// Access to shared texture must be wrapped in AcquireSync/ReleaseSync
			// to ensure the compositor has finished rendering to it before it gets used.
			// This enforces scheduling of work on the gpu between processes.
			IDXGIKeyedMutex *pKeyedMutex = NULL;
			if (SUCCEEDED(pTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **)&pKeyedMutex)))
			{
				if (pKeyedMutex->AcquireSync(0, 10) != S_OK)
				{
					pKeyedMutex->Release();
					Log("[VDispDvr] ACQUIRESYNC FAILED!!!");
					return;
				}
			}

			//Log("[VDispDvr] AcquiredSync");

			if (m_pFlushTexture == NULL)
			{
				D3D11_TEXTURE2D_DESC srcDesc;
				pTexture->GetDesc(&srcDesc);

				// Create a second small texture for copying and reading a single pixel from
				// in order to block on the cpu until rendering is finished.
				D3D11_TEXTURE2D_DESC flushTextureDesc;
				ZeroMemory(&flushTextureDesc, sizeof(flushTextureDesc));
				flushTextureDesc.Width = 32;
				flushTextureDesc.Height = 32;
				flushTextureDesc.MipLevels = 1;
				flushTextureDesc.ArraySize = 1;
				flushTextureDesc.Format = srcDesc.Format;
				flushTextureDesc.SampleDesc.Count = 1;
				flushTextureDesc.Usage = D3D11_USAGE_STAGING;
				flushTextureDesc.BindFlags = 0;
				flushTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

				if (FAILED(m_pD3DRender->GetDevice()->CreateTexture2D(&flushTextureDesc, NULL, &m_pFlushTexture)))
				{
					Log("Failed to create flush texture!");
					return;
				}
			}

			uint64_t presentationTime = GetTimestampUs();

			// Copy a single pixel so we can block until rendering is finished in WaitForPresent.
			D3D11_BOX box = { 0, 0, 0, 1, 1, 1 };
			m_pD3DRender->GetContext()->CopySubresourceRegion(m_pFlushTexture, 0, 0, 0, 0, pTexture, 0, &box);

			//Log("[VDispDvr] Flush-Begin");

			// This can go away, but is useful to see it as a separate packet on the gpu in traces.
			m_pD3DRender->GetContext()->Flush();

			//Log("[VDispDvr] Flush-End");

			// Copy entire texture to staging so we can read the pixels to send to remote device.
			m_pEncoder->CopyToStaging(&pTexture, 1, presentationTime, m_LastReferencedFrameIndex, m_LastReferencedClientTime);

			//Log("[VDispDvr] Flush-Staging(begin)");

			m_pD3DRender->GetContext()->Flush();

			//Log("[VDispDvr] Flush-Staging(end)");

			if (pKeyedMutex)
			{
				pKeyedMutex->ReleaseSync(0);
				pKeyedMutex->Release();
			}

			//Log("[VDispDvr] ReleasedSync");
		}
	}

	virtual void WaitForPresent() override
	{
		Log("[VDispDvr] WaitForPresent(begin)");

		// First wait for rendering to finish on the gpu.
		if (m_pFlushTexture)
		{
			D3D11_MAPPED_SUBRESOURCE mapped = { 0 };
			if (SUCCEEDED(m_pD3DRender->GetContext()->Map(m_pFlushTexture, 0, D3D11_MAP_READ, 0, &mapped)))
			{
				Log("[VDispDvr] Mapped FlushTexture");

				m_pD3DRender->GetContext()->Unmap(m_pFlushTexture, 0);
			}
		}

		Log("[VDispDvr] RenderingFinished");

		// Now that we know rendering is done, we can fire off our thread that reads the
		// backbuffer into system memory.  We also pass in the earliest time that this frame
		// should get presented.  This is the real vsync that starts our frame.
		m_pEncoder->NewFrameReady(m_flLastVsyncTimeInSeconds + m_flAdditionalLatencyInSeconds);

		// Get latest timing info to work with.  This gets us sync'd up with the hardware in
		// the first place, and also avoids any drifting over time.
		double flLastVsyncTimeInSeconds;
		uint32_t nVsyncCounter;
		m_pRemoteDevice->GetTimingInfo(&flLastVsyncTimeInSeconds, &nVsyncCounter);

		// Account for encoder/transmit latency.
		// This is where the conversion from real to virtual vsync happens.
		flLastVsyncTimeInSeconds -= m_flAdditionalLatencyInSeconds;

		float flFrameIntervalInSeconds = m_pRemoteDevice->GetFrameIntervalInSeconds();

		// Realign our last time interval given updated timing reference.
		int32_t nTimeRefToLastVsyncFrames =
			(int32_t)roundf(float(m_flLastVsyncTimeInSeconds - flLastVsyncTimeInSeconds) / flFrameIntervalInSeconds);
		m_flLastVsyncTimeInSeconds = flLastVsyncTimeInSeconds + flFrameIntervalInSeconds * nTimeRefToLastVsyncFrames;

		// We could probably just use this instead, but it seems safer to go off the system timer calculation.
		//assert(m_nVsyncCounter == nVsyncCounter + nTimeRefToLastVsyncFrames);

		double flNow = SystemTime::GetInSeconds();

		// Find the next frame interval (keeping in mind we may get here during running start).
		int32_t nLastVsyncToNextVsyncFrames =
			(int32_t)(float(flNow - m_flLastVsyncTimeInSeconds) / flFrameIntervalInSeconds);
		nLastVsyncToNextVsyncFrames = max(nLastVsyncToNextVsyncFrames, 0) + 1;

		// And store it for use in GetTimeSinceLastVsync (below) and updating our next frame.
		m_flLastVsyncTimeInSeconds += flFrameIntervalInSeconds * nLastVsyncToNextVsyncFrames;
		m_nVsyncCounter = nVsyncCounter + nTimeRefToLastVsyncFrames + nLastVsyncToNextVsyncFrames;

		Log("[VDispDvr] WaitForPresent(end)");
	}

	virtual bool GetTimeSinceLastVsync(float *pfSecondsSinceLastVsync, uint64_t *pulFrameCounter) override
	{
		*pfSecondsSinceLastVsync = (float)(SystemTime::GetInSeconds() - m_flLastVsyncTimeInSeconds);
		*pulFrameCounter = m_nVsyncCounter;
		return true;
	}

	void ListenerCallback(sockaddr_in *addr)
	{
	}

	void OnPoseUpdated() {
		if (m_unObjectId != vr::k_unTrackedDeviceIndexInvalid)
		{
			Log("OnPoseUpdated");
			vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_unObjectId, GetPose(), sizeof(vr::DriverPose_t));
		}
	}

private:
	uint64_t m_nGraphicsAdapterLuid;
	float m_flAdditionalLatencyInSeconds;
	double m_flLastVsyncTimeInSeconds;
	uint32_t m_nVsyncCounter;

	CD3DRender *m_pD3DRender;
	ID3D11Texture2D *m_pFlushTexture;
	CNvEncoder *m_pRemoteDevice;
	CEncoder *m_pEncoder;
	Listener *m_Listener;
	VSyncThread *m_VSyncThread;
public:
	// -----------------------------------
	// Direct mode methods
	// -----------------------------------

	/** Specific to Oculus compositor support, textures supplied must be created using this method. */
	virtual void CreateSwapTextureSet(uint32_t unPid, uint32_t unFormat, uint32_t unWidth, uint32_t unHeight, vr::SharedTextureHandle_t(*pSharedTextureHandles)[3]) {
		Log("CreateSwapTextureSet %d %d %d %d", unPid, unFormat, unWidth, unHeight);

		//HRESULT hr = D3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_HARDWARE, NULL, creationFlags, NULL, 0, D3D11_SDK_VERSION, &pDevice, &eFeatureLevel, &pContext);

		D3D11_TEXTURE2D_DESC SharedTextureDesc = {};
		SharedTextureDesc.ArraySize = 1;
		SharedTextureDesc.MipLevels = 1;
		SharedTextureDesc.SampleDesc.Count = 1;
		SharedTextureDesc.SampleDesc.Quality = 0;
		SharedTextureDesc.Usage = D3D11_USAGE_DEFAULT;
		SharedTextureDesc.Format = (DXGI_FORMAT)unFormat;

		// Some(or all?) applications request larger texture than we specified in GetRecommendedRenderTargetSize.
		// But, we must create textures in requested size to prevent cropped output. And then we must shrink texture to H.264 movie size.
		SharedTextureDesc.Width = unWidth;
		SharedTextureDesc.Height = unHeight;

		SharedTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		//SharedTextureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		SharedTextureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
		
		ProcessResource *processResource = new ProcessResource();

		for (int i = 0; i < 3; i++) {
			HRESULT hr = m_pD3DRender->GetDevice()->CreateTexture2D(&SharedTextureDesc, NULL, &processResource->textures[i]);
			//Log("texture%d %p res:%d %s", i, texture[i], hr, GetDxErrorStr(hr).c_str());

			IDXGIResource* pResource;
			hr = processResource->textures[i]->QueryInterface(__uuidof(IDXGIResource), (void**)&pResource);
			//Log("QueryInterface %p res:%d %s", pResource, hr, GetDxErrorStr(hr).c_str());

			hr = pResource->GetSharedHandle(&processResource->sharedHandles[i]);
			//Log("GetSharedHandle %p res:%d %s", processResource->sharedHandles[i], hr, GetDxErrorStr(hr).c_str());

			m_handleMap.insert(std::make_pair(processResource->sharedHandles[i], std::make_pair(processResource, i)));

			(*pSharedTextureHandles)[i] = (vr::SharedTextureHandle_t)processResource->sharedHandles[i];

			pResource->Release();

			Log("texture %d %p", i, processResource->sharedHandles[i]);
		}
		//m_processMap.insert(std::pair<uint32_t, ProcessResource *>(unPid, processResource));
	}

	/** Used to textures created using CreateSwapTextureSet.  Only one of the set's handles needs to be used to destroy the entire set. */
	virtual void DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle) {
		Log("DestroySwapTextureSet");

		auto it = m_handleMap.find((HANDLE)sharedTextureHandle);
		if (it != m_handleMap.end()) {
			// Release all reference (a bit forcible)
			it->second.first->textures[it->second.second].Reset();
		}
		else {
			Log("Requested to destroy not managing texture. handle:%p", sharedTextureHandle);
		}
	}

	/** Used to purge all texture sets for a given process. */
	virtual void DestroyAllSwapTextureSets(uint32_t unPid) {
		Log("DestroyAllSwapTextureSets");

		for (auto it = m_handleMap.begin(); it != m_handleMap.end();) {
			if (it->second.first->pid == unPid) {
				if (it->second.second == 0) {
					delete it->second.first;
				}
				m_handleMap.erase(it++);
			}
			else {
				++it;
			}
		}
	}

	/** After Present returns, calls this to get the next index to use for rendering. */
	virtual void GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t(*pIndices)[2]) {
		Log("GetNextSwapTextureSetIndex %p %p %d %d", sharedTextureHandles[0], sharedTextureHandles[1], (*pIndices)[0], (*pIndices)[1]);
		(*pIndices)[0]++;
		(*pIndices)[0] %= 3;
		(*pIndices)[1]++;
		(*pIndices)[1] %= 3;
	}

	/** Call once per layer to draw for this frame.  One shared texture handle per eye.  Textures must be created
	* using CreateSwapTextureSet and should be alternated per frame.  Call Present once all layers have been submitted. */
	virtual void SubmitLayer(vr::SharedTextureHandle_t sharedTextureHandles[2], const vr::VRTextureBounds_t(&bounds)[2], const vr::HmdMatrix34_t *pPose) {
		Log("SubmitLayer %p %p %f-%f,%f-%f %f-%f,%f-%f  \n%f,%f,%f,%f\n%f,%f,%f,%f\n%f,%f,%f,%f", sharedTextureHandles[0], sharedTextureHandles[1]
			, bounds[0].uMin, bounds[0].uMax, bounds[0].vMin, bounds[0].vMax
			, bounds[1].uMin, bounds[1].uMax, bounds[1].vMin, bounds[1].vMax
			, pPose->m[0][0], pPose->m[0][1], pPose->m[0][2], pPose->m[0][3]
			, pPose->m[1][0], pPose->m[1][1], pPose->m[1][2], pPose->m[1][3]
			, pPose->m[2][0], pPose->m[2][1], pPose->m[2][2], pPose->m[2][3]
		);
		// 3x3 rotation matrix
		//pPose->m[0][0], pPose->m[0][1], pPose->m[0][2],
		//pPose->m[1][0], pPose->m[1][1], pPose->m[1][2], 
		//pPose->m[2][0], pPose->m[2][1], pPose->m[2][2], 
		// position
		// x = pPose->m[0][3], y = pPose->m[1][3], z = pPose->m[2][3]
		m_framePose = *pPose;

		m_poseMutex.Wait(INFINITE);
		float diff = 100000;
		int index = 0;
		int minIndex = 0;
		auto minIt = m_poseBuffer.begin();
		for (auto it = m_poseBuffer.begin(); it != m_poseBuffer.end(); it++, index++) {
			float distance = 0;
			// rotation matrix composes parts of ViewMatrix
			for (int i = 0; i < 3; i++) {
				for (int j = 0; j < 3; j++) {
					distance += pow(it->Eye[0].ViewMatrix.M[j * 3 + i] - pPose->m[i][j], 2);
				}
			}
			if (diff > distance) {
				minIndex = index;
				minIt = it;
			}
		}
		if (minIt != m_poseBuffer.end()) {
			// found the frameIndex
			m_submitFrameIndex = minIt->FrameIndex;
			m_submitClientTime = minIt->clientTime;
		}
		else {
			m_submitFrameIndex = 0;
			m_submitClientTime = 0;
		}
		m_poseMutex.Release();

		m_submitTextures[0] = sharedTextureHandles[0];
		m_submitTextures[1] = sharedTextureHandles[1];
	}

	/** Submits queued layers for display. */
	virtual void Present(vr::SharedTextureHandle_t syncTexture) {
		Log("Present %p", syncTexture);

		Log("[VDispDvr] Waiting for previous encode to finish...");

		// Wait for the encoder to be ready.  This is important because the encoder thread
		// blocks on transmit which uses our shared d3d context (which is not thread safe).
		m_pEncoder->WaitForEncode();

		Log("[VDispDvr] Done");

		ID3D11Texture2D *pSyncTexture = m_pD3DRender->GetSharedTexture((HANDLE)syncTexture);
		if (!pSyncTexture)
		{
			Log("[VDispDvr] SyncTexture is NULL!");
			return;
		}

		// Access to shared texture must be wrapped in AcquireSync/ReleaseSync
		// to ensure the compositor has finished rendering to it before it gets used.
		// This enforces scheduling of work on the gpu between processes.
		IDXGIKeyedMutex *pKeyedMutex = NULL;
		if (SUCCEEDED(pSyncTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **)&pKeyedMutex)))
		{
			// TODO: Reasonable timeout and timeout handling
			HRESULT hr = pKeyedMutex->AcquireSync(0, INFINITE);
			if (hr != S_OK)
			{
				pKeyedMutex->Release();
				Log("[VDispDvr] ACQUIRESYNC FAILED!!! hr=%d %p %s", hr, hr, GetDxErrorStr(hr).c_str());
				return;
			}
		}

		uint64_t presentationTime = GetTimestampUs();

		ID3D11Texture2D *pTexture[2];
		// Left eye
		auto it = m_handleMap.find((HANDLE)m_submitTextures[0]);
		// No AddRef
		pTexture[0] = it->second.first->textures[it->second.second].Get();

		// Right eye
		it = m_handleMap.find((HANDLE)m_submitTextures[0]);
		pTexture[1] = it->second.first->textures[it->second.second].Get();

		//Log("[VDispDvr] Flush-Begin");

		// This can go away, but is useful to see it as a separate packet on the gpu in traces.
		m_pD3DRender->GetContext()->Flush();

		//Log("[VDispDvr] Flush-End");

		// Copy entire texture to staging so we can read the pixels to send to remote device.
		Log("FrameIndex diff LastRef: %llu render:%llu  diff:%llu", m_LastReferencedFrameIndex, m_submitFrameIndex, m_LastReferencedFrameIndex - m_submitFrameIndex);
		m_pEncoder->CopyToStaging(pTexture, 2, presentationTime, m_submitFrameIndex, m_submitClientTime);

		//Log("[VDispDvr] Flush-Staging(begin)");

		m_pD3DRender->GetContext()->Flush();

		//Log("[VDispDvr] Flush-Staging(end)");

		if (pKeyedMutex)
		{
			pKeyedMutex->ReleaseSync(0);
			pKeyedMutex->Release();
		}

		m_pEncoder->NewFrameReady(m_flLastVsyncTimeInSeconds + m_flAdditionalLatencyInSeconds);
	}

private:
	// Resource for each process
	struct ProcessResource {
		ComPtr<ID3D11Texture2D> textures[3];
		HANDLE sharedHandles[3];
		uint32_t pid;
	};
	//std::unordered_multimap<uint32_t, ProcessResource *> m_processMap;
	std::map<HANDLE, std::pair<ProcessResource *, int> > m_handleMap;

	vr::SharedTextureHandle_t m_submitTextures[2];
	vr::HmdMatrix34_t m_framePose;
	uint64_t m_submitFrameIndex;
	uint64_t m_submitClientTime;
};

//-----------------------------------------------------------------------------
// Purpose: Server interface implementation.
//-----------------------------------------------------------------------------
class CServerDriver_DisplayRedirect : public vr::IServerTrackedDeviceProvider
{
public:
	CServerDriver_DisplayRedirect()
		: m_pRemoteHmd( NULL )
	{}

	virtual vr::EVRInitError Init( vr::IVRDriverContext *pContext ) override;
	virtual void Cleanup() override;
	virtual const char * const *GetInterfaceVersions() override
		{ return vr::k_InterfaceVersions;  }
	virtual const char *GetTrackedDeviceDriverVersion()
		{ return vr::ITrackedDeviceServerDriver_Version; }
	virtual void RunFrame();
	virtual bool ShouldBlockStandbyMode() override { return false; }
	virtual void EnterStandby() override {}
	virtual void LeaveStandby() override {}

private:
	CRemoteHmd *m_pRemoteHmd;
};

vr::EVRInitError CServerDriver_DisplayRedirect::Init( vr::IVRDriverContext *pContext )
{
	VR_INIT_SERVER_DRIVER_CONTEXT( pContext );

	m_pRemoteHmd = new CRemoteHmd();

	if (m_pRemoteHmd->IsValid() )
	{
		bool ret;
		ret = vr::VRServerDriverHost()->TrackedDeviceAdded(
			m_pRemoteHmd->GetSerialNumber().c_str(),
			vr::TrackedDeviceClass_HMD,
			//vr::TrackedDeviceClass_DisplayRedirect,
			m_pRemoteHmd);
		Log("TrackedDeviceAdded %d %s", ret, m_pRemoteHmd->GetSerialNumber().c_str());
	}

	return vr::VRInitError_None;
}

void CServerDriver_DisplayRedirect::Cleanup()
{
	delete m_pRemoteHmd;
	m_pRemoteHmd = NULL;

	VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

void CServerDriver_DisplayRedirect::RunFrame()
{
	if (m_pRemoteHmd)
	{
		m_pRemoteHmd->RunFrame();
	}
}

CServerDriver_DisplayRedirect g_serverDriverDisplayRedirect;

//-----------------------------------------------------------------------------
// Purpose: Entry point for vrserver when loading drivers.
//-----------------------------------------------------------------------------
extern "C" __declspec( dllexport )
void *HmdDriverFactory( const char *pInterfaceName, int *pReturnCode )
{
	Log("HmdDriverFactory %s (%s)", pInterfaceName, vr::IServerTrackedDeviceProvider_Version);
	if ( 0 == strcmp( vr::IServerTrackedDeviceProvider_Version, pInterfaceName ) )
	{
		Log("HmdDriverFactory server return");
		return &g_serverDriverDisplayRedirect;
	}

	if( pReturnCode )
		*pReturnCode = vr::VRInitError_Init_InterfaceNotFound;

	return NULL;
}

