//	VQEngine | DirectX11 Renderer
//	Copyright(C) 2018  - Volkan Ilbeyli
//
//	This program is free software : you can redistribute it and / or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.If not, see <http://www.gnu.org/licenses/>.
//
//	Contact: volkanilbeyli@gmail.com

#define LOG_SEARCH 0

#include "Renderer.h"
#include "D3DManager.h"
#include "BufferObject.h"
#include "Shader.h"

#include "Engine/Mesh.h"
#include "Engine/Light.h"
#include "Engine/Settings.h"

#include "Application/SystemDefs.h"

#include "Utilities/utils.h"

#define STB_IMAGE_IMPLEMENTATION
#include "3rdParty/stb/stb_image.h"
#include "3rdParty/DirectXTex/DirectXTex/DirectXTex.h"

#include <wincodec.h>	// needed for GUID_ContainerFormatPng

#include <mutex>
#include <cassert>
#include <fstream>

#if _DEBUG
#include "Application/Application.h"
#endif

// HELPER FUNCTIONS
//=======================================================================================================================================================
std::vector<std::string> GetShaderPaths(const std::string& shaderFileName)
{	// try to open each file
	const std::string path = Renderer::sShaderRoot + shaderFileName;
	const std::string paths[] = 
	{
		path + "_vs.hlsl",
		path + "_gs.hlsl",
		path + "_ds.hlsl",
		path + "_hs.hlsl",
		path + "_ps.hlsl",
		path + "_cs.hlsl",
	};

	std::vector<std::string> existingPaths;
	for (size_t i = 0; i < EShaderStage::COUNT; i++)
	{
		std::ifstream file(paths[i]);
		if (file.is_open())
		{
			existingPaths.push_back(paths[i]);
			file.close();
		}
	}

	if (existingPaths.empty())
	{
		Log::Error("No suitable shader paths \"%s_xs\"", shaderFileName.c_str());
	}
	return std::move(existingPaths);
}

void PollShaderFiles()
{
	// Concerns:
	// separate thread sharing window resources like context and d3d11device
	// might not perform as expected
	// link: https://www.opengl.org/discussion_boards/showthread.php/185980-recompile-the-shader-on-runtime-like-hot-plug-the-new-compiled-shader
	// source: https://msdn.microsoft.com/en-us/library/aa365261(v=vs.85).aspx
	Log::Info("Thread here : PollStarted.\n");
	Sleep(800);

#if 0
	static HANDLE dwChangeHandle;
	DWORD dwWaitStatus;
	LPTSTR lpDir = "Data/Shaders/";

	dwChangeHandle = FindFirstChangeNotification(
		lpDir,                         // directory to watch 
		TRUE,                         // do not watch subtree 
		FILE_NOTIFY_CHANGE_FILE_NAME); // watch file name changes 

	if (dwChangeHandle == INVALID_HANDLE_VALUE)
	{
		Log::Error("FindFirstChangeNotification function failed.\n");
		;// ExitProcess(GetLastError());
	}

	while (TRUE)
	{
		//	Wait for notification.
		Log::Info("\nWaiting for notification...\n");

		dwWaitStatus = WaitForSingleObject(dwChangeHandle,
			INFINITE);

		switch (dwWaitStatus)
		{
		case WAIT_OBJECT_0:

			//A file was created, renamed, or deleted in the directory.
			//Refresh this directory and restart the notification.

			OnShaderChange(lpDir);
			if (FindNextChangeNotification(dwChangeHandle) == FALSE)
			{
				Log::Error("FindNextChangeNotification function failed.\n");
				ExitProcess(GetLastError());
			}
			break;

		case WAIT_OBJECT_0 + 1:

			// A directory was created, renamed, or deleted.
			// Refresh the tree and restart the notification.

			//RefreshTree(lpDrive);
			/*if (FindNextChangeNotification(dwChangeHandles[1]) == FALSE)
			{
			printf("\n ERROR: FindNextChangeNotification function failed.\n");
			ExitProcess(GetLastError());
			}*/
			break;

		case WAIT_TIMEOUT:

			//A timeout occurred, this would happen if some value other 
			//than INFINITE is used in the Wait call and no changes occur.
			//In a single-threaded environment you might not want an
			//INFINITE wait.

			OutputDebugString("\nNo changes in the timeout period.\n");
			break;

		default:
			OutputDebugString("\n ERROR: Unhandled dwWaitStatus.\n");
			ExitProcess(GetLastError());
			break;
		}
	}
	OutputDebugString("Done.\n");
#endif
}

void OnShaderChange(LPTSTR dir)
{
	Log::Info("OnShaderChange(%s)\n\n", dir);
	// we know that a change occurred in the 'dir' directory. Read source again
	// works		: create file, delete file
	// doesn't work	: modify file
	// source: https://msdn.microsoft.com/en-us/library/aa365261(v=vs.85).aspx
}
//=======================================================================================================================================================


const char*			Renderer::sShaderRoot		= "Source/Shaders/";
const char*			Renderer::sTextureRoot		= "Data/Textures/";
const char*			Renderer::sHDRTextureRoot	= "Data/Textures/EnvironmentMaps/";
bool				Renderer::sEnableBlend = true;

Renderer::Renderer()
	:
	m_Direct3D(nullptr),
	m_device(nullptr),
	//m_deviceContext(nullptr),
	mRasterizerStates  (std::vector<RasterizerState*>  (EDefaultRasterizerState::RASTERIZER_STATE_COUNT)),
	mDepthStencilStates(std::vector<DepthStencilState*>(EDefaultDepthStencilState::DEPTH_STENCIL_STATE_COUNT)),
	mBlendStates       (std::vector<BlendState>(EDefaultBlendState::BLEND_STATE_COUNT)),
	mSamplers		   (std::vector<Sampler>(EDefaultSamplerState::DEFAULT_SAMPLER_COUNT))
	//,	m_ShaderHotswapPollWatcher("ShaderHotswapWatcher")
{
	for (int i=0; i<(int)EDefaultRasterizerState::RASTERIZER_STATE_COUNT; ++i)
	{
		mRasterizerStates[i] = (RasterizerState*)malloc(sizeof(*mRasterizerStates[i]));
		memset(mRasterizerStates[i], 0, sizeof(*mRasterizerStates[i]));
	}

	for (int i = 0; i < (int)EDefaultBlendState::BLEND_STATE_COUNT; ++i)
	{
		mBlendStates[i].ptr = (ID3D11BlendState*)malloc(sizeof(*mBlendStates[i].ptr));
		memset(mBlendStates[i].ptr, 0, sizeof(*mBlendStates[i].ptr));
	}
}

Renderer::~Renderer(){}

bool Renderer::Initialize(HWND hwnd, const Settings::Window& settings)
{
	// DIRECT3D 11
	//--------------------------------------------------------------------
	mWindowSettings = settings;
	m_Direct3D = new D3DManager();
	if (!m_Direct3D)
	{
		assert(false);
		return false;
	}

	bool result = m_Direct3D->Initialize(
		settings.width,
		settings.height,
		settings.vsync == 1,
		hwnd,
		settings.fullscreen == 1,
		DXGI_FORMAT_R16G16B16A16_FLOAT
		// swapchain should be bgra unorm 32bit
	);

	if (!result)
	{
		MessageBox(hwnd, "Could not initialize Direct3D", "Error", MB_OK);
		return false;
	}
	m_device = m_Direct3D->m_device;
	//m_deviceContext = m_Direct3D->m_deviceContext;
	Mesh::spRenderer = this;

	// DEFAULT RENDER TARGET
	//--------------------------------------------------------------------
	{
		RenderTarget defaultRT;

		ID3D11Texture2D* backBufferPtr;
		HRESULT hr = m_Direct3D->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBufferPtr);
		if (FAILED(hr))
		{
			Log::Error("Cannot get back buffer pointer in DefaultRenderTarget initialization");
			return false;
		}
		defaultRT.texture._tex2D = backBufferPtr;
		defaultRT.texture._id = 0;

		D3D11_TEXTURE2D_DESC texDesc;		// get back buffer description
		backBufferPtr->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;	// create shader resource view from back buffer desc
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		m_device->CreateShaderResourceView(backBufferPtr, &srvDesc, &defaultRT.texture._srv);

		hr = m_device->CreateRenderTargetView(backBufferPtr, nullptr, &defaultRT.pRenderTargetView);
		if (FAILED(hr))
		{
			Log::Error("Cannot create default render target view.");
			return false;
		}

		mTextures.push_back(defaultRT.texture);	// set texture ID by adding it -- TODO: remove duplicate data - don't add texture to vector
		defaultRT.texture._id = static_cast<int>(mTextures.size() - 1);

		mRenderTargets.push_back(defaultRT);
		mBackBufferRenderTarget = static_cast<int>(mRenderTargets.size() - 1);
	}
	//m_Direct3D->ReportLiveObjects("Init Default RT\n");


	// DEFAULT DEPTH TARGET
	//--------------------------------------------------------------------
	{
		// Set up the description of the depth buffer.
		TextureDesc depthTexDesc;
		depthTexDesc.width = settings.width;
		depthTexDesc.height = settings.height;
		depthTexDesc.arraySize = 1;
		depthTexDesc.mipCount = 1;
		//depthTexDesc.format = R24G8;
		depthTexDesc.format = R32;
		depthTexDesc.usage = ETextureUsage(DEPTH_TARGET | RESOURCE);

		DepthTargetDesc depthDesc;
		depthDesc.format = EImageFormat::D32F;
		depthDesc.textureDesc = depthTexDesc;
		mDefaultDepthBufferTexture = GetDepthTargetTexture(AddDepthTarget(depthDesc)[0]);
	}
	//m_Direct3D->ReportLiveObjects("Init Depth Buffer\n");

	// DEFAULT RASTERIZER STATES
	//--------------------------------------------------------------------
	{
		HRESULT hr;
		const std::string err("Unable to create Rasterizer State: Cull ");

		// MSDN: https://msdn.microsoft.com/en-us/library/windows/desktop/ff476198(v=vs.85).aspx
		D3D11_RASTERIZER_DESC rsDesc = {};

		rsDesc.FillMode = D3D11_FILL_SOLID;
		rsDesc.FrontCounterClockwise = false;
		rsDesc.DepthBias = 0;
		rsDesc.ScissorEnable = false;
		rsDesc.DepthBiasClamp = 0;
		rsDesc.SlopeScaledDepthBias = 0.0f;
		rsDesc.DepthClipEnable = true;
		rsDesc.AntialiasedLineEnable = true;
		rsDesc.MultisampleEnable = false;

		rsDesc.CullMode = D3D11_CULL_BACK;
		hr = m_device->CreateRasterizerState(&rsDesc, &mRasterizerStates[(int)EDefaultRasterizerState::CULL_BACK]);
		if (FAILED(hr))
		{
			Log::Error(err + "Back\n");
		}

		rsDesc.CullMode = D3D11_CULL_FRONT;
		hr = m_device->CreateRasterizerState(&rsDesc, &mRasterizerStates[(int)EDefaultRasterizerState::CULL_FRONT]);
		if (FAILED(hr))
		{
			Log::Error(err + "Front\n");
		}

		rsDesc.CullMode = D3D11_CULL_NONE;
		hr = m_device->CreateRasterizerState(&rsDesc, &mRasterizerStates[(int)EDefaultRasterizerState::CULL_NONE]);
		if (FAILED(hr))
		{
			Log::Error(err + "None\n");
		}

		rsDesc.FillMode = static_cast<D3D11_FILL_MODE>(ERasterizerFillMode::WIREFRAME);
		hr = m_device->CreateRasterizerState(&rsDesc, &mRasterizerStates[(int)EDefaultRasterizerState::WIREFRAME]);
		if (FAILED(hr))
		{
			Log::Error(err + "Wireframe\n");
		}
	}
	//m_Direct3D->ReportLiveObjects("Init Default RS ");


	// DEFAULT BLEND STATES
	//--------------------------------------------------------------------
	{
		D3D11_RENDER_TARGET_BLEND_DESC rtBlendDesc = {};
		rtBlendDesc.BlendEnable = true;
		rtBlendDesc.BlendOp = D3D11_BLEND_OP_ADD;
		rtBlendDesc.SrcBlend = D3D11_BLEND_ONE;
		rtBlendDesc.DestBlend = D3D11_BLEND_ONE;
		rtBlendDesc.BlendOpAlpha = D3D11_BLEND_OP_MIN;
		rtBlendDesc.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		rtBlendDesc.DestBlendAlpha = D3D11_BLEND_DEST_ALPHA;
		rtBlendDesc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		D3D11_BLEND_DESC desc = {};
		desc.RenderTarget[0] = rtBlendDesc;

		m_device->CreateBlendState(&desc, &(mBlendStates[EDefaultBlendState::ADDITIVE_COLOR].ptr));

		rtBlendDesc.BlendOp = D3D11_BLEND_OP_ADD;
		rtBlendDesc.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		rtBlendDesc.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		rtBlendDesc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rtBlendDesc.SrcBlendAlpha = D3D11_BLEND_ONE;
		rtBlendDesc.DestBlendAlpha = D3D11_BLEND_ZERO;
		rtBlendDesc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		desc.RenderTarget[0] = rtBlendDesc;
		m_device->CreateBlendState(&desc, &(mBlendStates[EDefaultBlendState::ALPHA_BLEND].ptr));

		rtBlendDesc.BlendEnable = false;
		desc.RenderTarget[0] = rtBlendDesc;
		m_device->CreateBlendState(&desc, &(mBlendStates[EDefaultBlendState::DISABLED].ptr));
	}
	//m_Direct3D->ReportLiveObjects("Init Default BlendStates ");


	// DEFAULT SAMPLER STATES
	//--------------------------------------------------------------------
	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::WRAP_SAMPLER]._samplerState));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::POINT_SAMPLER]._samplerState));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::LINEAR_FILTER_SAMPLER_WRAP_UVW]._samplerState));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::LINEAR_FILTER_SAMPLER]._samplerState));


		samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::ANISOTROPIC_1_CLAMPED_SAMPLER]._samplerState));
		samplerDesc.MaxAnisotropy = 2;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::ANISOTROPIC_2_CLAMPED_SAMPLER]._samplerState));
		samplerDesc.MaxAnisotropy = 4;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::ANISOTROPIC_4_CLAMPED_SAMPLER]._samplerState));
		samplerDesc.MaxAnisotropy = 16;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::ANISOTROPIC_16_CLAMPED_SAMPLER]._samplerState));
		
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxAnisotropy = 1;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::ANISOTROPIC_1_WRAPPED_SAMPLER]._samplerState));
		samplerDesc.MaxAnisotropy = 2;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::ANISOTROPIC_2_WRAPPED_SAMPLER]._samplerState));
		samplerDesc.MaxAnisotropy = 4;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::ANISOTROPIC_4_WRAPPED_SAMPLER]._samplerState));
		samplerDesc.MaxAnisotropy = 16;
		m_device->CreateSamplerState(&samplerDesc, &(mSamplers[EDefaultSamplerState::ANISOTROPIC_16_WRAPPED_SAMPLER]._samplerState));
	}

	// DEFAULT DEPTHSTENCIL SATATES
	//--------------------------------------------------------------------
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	auto checkFailed = [&](HRESULT hr)
	{
		if (FAILED(result))
		{
			Log::Error("Default Depth Stencil State");
			return false;
		}
		return true;
	};


	// Set up the description of the stencil state.
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

	depthStencilDesc.StencilEnable = true;
	depthStencilDesc.StencilReadMask = 0xFF;
	depthStencilDesc.StencilWriteMask = 0xFF;

	// Stencil operations if pixel is front-facing.
	depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Stencil operations if pixel is back-facing.
	depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Create the depth stencil states.
	HRESULT hr = m_device->CreateDepthStencilState(&depthStencilDesc, &mDepthStencilStates[EDefaultDepthStencilState::DEPTH_STENCIL_WRITE]);
	if (!checkFailed(hr)) return false;

	depthStencilDesc.DepthEnable = false;
	depthStencilDesc.StencilEnable = false;
	hr = m_device->CreateDepthStencilState(&depthStencilDesc, &mDepthStencilStates[EDefaultDepthStencilState::DEPTH_STENCIL_DISABLED]);
	if (!checkFailed(hr)) return false;

	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.StencilEnable = false;
	hr = m_device->CreateDepthStencilState(&depthStencilDesc, &mDepthStencilStates[EDefaultDepthStencilState::DEPTH_WRITE]);
	if (!checkFailed(hr)) return false;

	depthStencilDesc.DepthEnable = false;
	depthStencilDesc.StencilEnable = true;
	hr = m_device->CreateDepthStencilState(&depthStencilDesc, &mDepthStencilStates[EDefaultDepthStencilState::STENCIL_WRITE]);
	if (!checkFailed(hr)) return false;

	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.StencilEnable = true;
	hr = m_device->CreateDepthStencilState(&depthStencilDesc, &mDepthStencilStates[EDefaultDepthStencilState::DEPTH_TEST_ONLY]);
	if (!checkFailed(hr)) return false;

	return true;
}

void Renderer::Exit()
{
	//m_Direct3D->ReportLiveObjects("BEGIN EXIT");

	constexpr size_t BUFFER_TYPE_COUNT = 3;
	std::vector<Buffer>* buffers[BUFFER_TYPE_COUNT] = { &mVertexBuffers, &mIndexBuffers, &mUABuffers };
	for (int i = 0; i < BUFFER_TYPE_COUNT; ++i)
	{
		auto& refBuffer = *buffers[i];
		std::for_each(refBuffer.begin(), refBuffer.end(), [](Buffer& b) {b.CleanUp(); });
		refBuffer.clear();
	}
	
	// Unload shaders
	for (Shader*& shd : mShaders)
	{
		delete shd;
		shd = nullptr;
	}
	mShaders.clear();

	for (Texture& tex : mTextures)
	{
		tex.Release();
	}
	mTextures.clear();

	for (Sampler& s : mSamplers)
	{
		if (s._samplerState)
		{
			s._samplerState->Release();
			s._samplerState = nullptr;
		}
	}

	for (RenderTarget& rt : mRenderTargets)
	{
		if (rt.pRenderTargetView)
		{
			rt.pRenderTargetView->Release();
			rt.pRenderTargetView = nullptr;
		}
		if (rt.texture._srv)
		{
			//rs._texture._srv->Release();
			rt.texture._srv = nullptr;
		}
		if (rt.texture._tex2D)
		{
			//rs._texture._tex2D->Release();
			rt.texture._tex2D = nullptr;
		}
	}

	for (RasterizerState*& rs : mRasterizerStates)
	{
		if (rs)
		{
			rs->Release();
			rs = nullptr;
		}
	}

	for (DepthStencilState*& dss : mDepthStencilStates)
	{
		if (dss)
		{
			dss->Release();
			dss = nullptr;
		}
	}

	for (BlendState& bs : mBlendStates)
	{
		if (bs.ptr)
		{
			bs.ptr->Release();
			bs.ptr = nullptr;
		}
	}

	for (DepthTarget& dt : mDepthTargets)
	{
		if (dt.pDepthStencilView)
		{
			dt.pDepthStencilView->Release();
			dt.pDepthStencilView = nullptr;
		}
	}

	//m_Direct3D->ReportLiveObjects("END EXIT\n");	// todo: ifdef debug & log_mem
	if (m_Direct3D)
	{
		m_Direct3D->Shutdown();
		delete m_Direct3D;
		m_Direct3D = nullptr;
	}

	Log::Info("---------------------------");
}

void Renderer::ReloadShaders()
{
	int reloadedShaderCount = 0;
	std::vector<std::string> reloadedShaderNames;
	for (Shader* pShader : mShaders)
	{
		if (pShader->HasSourceFileBeenUpdated())
		{
			const bool bLoadSuccess = pShader->Reload(m_device);
			if (!bLoadSuccess)
			{
				//Log::Error("");
				continue;
			}

			++reloadedShaderCount;
			reloadedShaderNames.push_back(pShader->Name());
		}
	}

	if (reloadedShaderCount == 0)
	{
		Log::Info("No updates have been made to shader source files: no shaders have been loaded");
	}
	else
	{
		Log::Info("Reloaded %d Shaders:", reloadedShaderCount);
		for (const std::string& name : reloadedShaderNames)
			Log::Info("\t%s", name.c_str());
	}
}

float	 Renderer::AspectRatio()	const { return m_Direct3D->AspectRatio(); };
unsigned Renderer::WindowHeight()	const { return m_Direct3D->WindowHeight(); };
unsigned Renderer::WindowWidth()	const { return m_Direct3D->WindowWidth(); }
vec2	 Renderer::GetWindowDimensionsAsFloat2() const { return vec2(static_cast<float>(this->WindowWidth()), static_cast<float>(this->WindowHeight())); }
HWND	 Renderer::GetWindow()			const { return m_Direct3D->WindowHandle(); };

const Shader* Renderer::GetShader(ShaderID shader_id) const
{
	assert(shader_id >= 0 && (int)mShaders.size() > shader_id);
	return mShaders[shader_id];
}



const PipelineState& Renderer::GetState() const
{
	return mPipelineState;
}

ShaderID Renderer::CreateShader(const ShaderDesc& shaderDesc)
{
	Shader* shader = new Shader(shaderDesc.shaderName);
	shader->CompileShaders(m_device, shaderDesc);

	mShaders.push_back(shader);
	shader->mID = (static_cast<int>(mShaders.size()) - 1);
	return shader->ID();
}

ShaderID Renderer::ReloadShader(const ShaderDesc& shaderDesc, const ShaderID shaderID)
{
	if (shaderID == -1)
	{
		Log::Warning("Reload shader called on uninitialized shader.");
		return CreateShader(shaderDesc);
	}

	assert(shaderID >= 0 && shaderID < mShaders.size());
	Shader* pShader = mShaders[shaderID];
	delete pShader;
	pShader = new Shader(shaderDesc.shaderName);

	pShader->CompileShaders(m_device, shaderDesc);
	pShader->mID = shaderID;
	mShaders[shaderID] = pShader;
	return pShader->ID();
}

ShaderDesc Renderer::GetShaderDesc(ShaderID shaderID) const
{
	assert(shaderID >= 0 && mShaders.size() > shaderID);
	return mShaders[shaderID]->mDescriptor;
}

RasterizerStateID Renderer::AddRasterizerState(ERasterizerCullMode cullMode, ERasterizerFillMode fillMode, bool bEnableDepthClip, bool bEnableScissors)
{
	D3D11_RASTERIZER_DESC RSDesc = {};

	RSDesc.CullMode = static_cast<D3D11_CULL_MODE>(cullMode);
	RSDesc.FillMode = static_cast<D3D11_FILL_MODE>(fillMode);
	RSDesc.DepthClipEnable = bEnableDepthClip;
	RSDesc.ScissorEnable = bEnableScissors;
	// todo: add params, scissors, multisample, antialiased line
	

	ID3D11RasterizerState* newRS;
	int hr = m_device->CreateRasterizerState(&RSDesc, &newRS);
	if (!SUCCEEDED(hr))
	{
		Log::Error("Cannot create Rasterizer State");
		return -1;
	}

	mRasterizerStates.push_back(newRS);
	return static_cast<RasterizerStateID>(mRasterizerStates.size() - 1);
}

// example params: "openart/185.png", "Data/Textures/"
TextureID Renderer::CreateTextureFromFile(const std::string& texFileName, const std::string& fileRoot /*= s_textureRoot*/, bool bGenerateMips /*= false*/)
{
	// renderer is single threaded in general, therefore finer granularity is not currently provided
	// and instead, a mutex lcok is employed for the entire duration of creating a texture from file.
	// this will require refactoring if its decided to go for a truely multi-threaded renderer.
	//
	std::unique_lock<std::mutex> l(mTexturesMutex);	

	if (texFileName.empty() || texFileName == "\"\"")
	{
		Log::Warning("Warning: CreateTextureFromFile() - empty texture file name passed as parameter");
		return -1;
	}
	
	auto found = std::find_if(mTextures.begin(), mTextures.end(), [&texFileName](auto& tex) { return tex._name == texFileName; });
	if (found != mTextures.end())
	{
		return (*found)._id;
	}
	

	const std::string path = fileRoot + texFileName;
#if _DEBUG
	Log::Info("\tLoading Texture: %s", path.c_str());
#endif

	Texture tex;

	tex._name = texFileName;
	std::wstring wpath(path.begin(), path.end());
	std::unique_ptr<DirectX::ScratchImage> img = std::make_unique<DirectX::ScratchImage>();
	if (SUCCEEDED(LoadFromWICFile(wpath.c_str(), WIC_FLAGS_NONE, nullptr, *img)))
	{
		auto meta = img->GetMetadata();
		
		CreateShaderResourceView(m_device, img->GetImages(), img->GetImageCount(), meta, &tex._srv);
		if (bGenerateMips)
		{

			ID3D11Resource* resource = nullptr;
			tex._srv->GetResource(&resource);
			if (tex._srv) tex._srv->Release();
			if (tex._tex2D) tex._tex2D->Release();

			meta.mipLevels = min(
				  static_cast<size_t>(std::log2(meta.width)	 )
				, static_cast<size_t>(std::log2(meta.height)));

			TextureDesc texDesc = {};
			texDesc.bGenerateMips = true;
			texDesc.bIsCubeMap = meta.depth != 1; // false?
			texDesc.arraySize = static_cast<int>(meta.arraySize);
			texDesc.format = static_cast<EImageFormat>(meta.format);
			texDesc.width = static_cast<int>(meta.width );
			texDesc.height = static_cast<int>(meta.height);
			texDesc.mipCount = static_cast<int>(meta.mipLevels);
			texDesc.usage = ETextureUsage::RENDER_TARGET_RW;
			texDesc.texFileName = texFileName;
			
			//texDesc.pData = img->GetPixels();
			//texDesc.dataPitch = img->GetImages()->rowPitch;
			//texDesc.dataSlicePitch = img->GetImages()->slicePitch;

			tex = GetTextureObject(CreateTexture2D(texDesc));

			//m_deviceContext->CopySubresourceRegion(tex._tex2D, 0, 0, 0, 0, resource, 0, NULL);
			//m_deviceContext->GenerateMips(tex._srv);
			//m_deviceContext->Flush();

//#if _DEBUG
//			SaveTextureToDisk(tex._id, Application::s_WorkspaceDirectory + "/DEBUG.png", false);
//#endif
			resource->Release();
		}
		else
		{

			// get srv from img
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			tex._srv->GetDesc(&srvDesc);

			// read width & height
			ID3D11Resource* resource = nullptr;
			tex._srv->GetResource(&resource);
			if (SUCCEEDED(resource->QueryInterface(&tex._tex2D)))
			{
				D3D11_TEXTURE2D_DESC desc;
				tex._tex2D->GetDesc(&desc);
				tex._width = desc.Width;
				tex._height = desc.Height;
			}
			resource->Release();

			tex._id = static_cast<int>(mTextures.size());
			mTextures.emplace_back(std::move(tex));
		}
		return mTextures.back()._id;
	}
	else
	{
		Log::Error("Cannot load texture file: %s\n", texFileName.c_str());
		return mTextures[0]._id;
	}
}

TextureID Renderer::CreateTexture2D(const TextureDesc& texDesc)
{
	Texture tex;
	tex._width = texDesc.width;
	tex._height = texDesc.height;
	tex._name = texDesc.texFileName;


	// check multi sampling quality level
	// https://msdn.microsoft.com/en-us/library/windows/desktop/bb173072(v=vs.85).aspx
	//UINT maxMultiSamplingQualityLevel = 0;
	//m_device->CheckMultisampleQualityLevels(, , &maxMultiSamplingQualityLevel);
	//---


	// Texture2D Resource
	UINT miscFlags = 0;
	miscFlags |= texDesc.bIsCubeMap ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
	miscFlags |= texDesc.bGenerateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

	UINT arrSize = texDesc.arraySize;
	const bool bIsTextureArray = texDesc.arraySize > 1;
	arrSize = texDesc.bIsCubeMap ? 6 * arrSize : arrSize;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Format = (DXGI_FORMAT)texDesc.format;
	desc.Height = max(texDesc.height, 1);
	desc.Width =  max(texDesc.width, 1);
	desc.ArraySize = arrSize;
	desc.MipLevels = texDesc.mipCount;
	desc.SampleDesc = { 1, 0 };
	desc.BindFlags = texDesc.usage;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = static_cast<D3D11_CPU_ACCESS_FLAG>(texDesc.cpuAccessMode);
	desc.MiscFlags = miscFlags;

	D3D11_SUBRESOURCE_DATA dataDesc = {};
	D3D11_SUBRESOURCE_DATA* pDataDesc = nullptr;
	if (texDesc.pData)
	{
		dataDesc.pSysMem = texDesc.pData;
		dataDesc.SysMemPitch = texDesc.dataPitch;
		dataDesc.SysMemSlicePitch = texDesc.dataSlicePitch;
		pDataDesc = &dataDesc;
	}
	m_device->CreateTexture2D(&desc, pDataDesc, &tex._tex2D);

#if defined(_DEBUG) || defined(PROFILE)
	if (!texDesc.texFileName.empty())
	{
		tex._tex2D->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(texDesc.texFileName.length()), texDesc.texFileName.c_str());
	}
#endif

	// Shader Resource View
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = (DXGI_FORMAT)texDesc.format;
	switch (texDesc.format)
	{
		// caution: if initializing for depth texture, and the depth texture
		//			has stencil defined (d24s8), we have to check for 
		//			DXGI_FORMAT_R24_UNORM_X8_TYPELESS vs R32F
	case EImageFormat::R24G8:
		srvDesc.Format = (DXGI_FORMAT)EImageFormat::R24_UNORM_X8_TYPELESS;
		break;
	case EImageFormat::R32:
		srvDesc.Format = (DXGI_FORMAT)EImageFormat::R32F;
		break;
	}

	if (texDesc.bIsCubeMap)
	{
		if (bIsTextureArray)
		{
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
			srvDesc.TextureCubeArray.NumCubes = arrSize / 6;
			srvDesc.TextureCubeArray.MipLevels = texDesc.mipCount;
			srvDesc.TextureCubeArray.MostDetailedMip = 0;
			srvDesc.TextureCubeArray.First2DArrayFace = 0;
			m_device->CreateShaderResourceView(tex._tex2D, &srvDesc, &tex._srv);
		}
		else
		{
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MipLevels = texDesc.mipCount;
			srvDesc.TextureCube.MostDetailedMip = 0;
			m_device->CreateShaderResourceView(tex._tex2D, &srvDesc, &tex._srv);
		}
	}
	else
	{
		if (bIsTextureArray)
		{
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MipLevels = texDesc.mipCount;
			srvDesc.Texture2DArray.MostDetailedMip = 0;

			tex._srvArray.resize(desc.ArraySize, nullptr);
			tex._depth = desc.ArraySize;
			for (unsigned i = 0; i < desc.ArraySize; ++i)
			{
				srvDesc.Texture2DArray.FirstArraySlice = i;
				srvDesc.Texture2DArray.ArraySize = desc.ArraySize - i;
				m_device->CreateShaderResourceView(tex._tex2D, &srvDesc, &tex._srvArray[i]);
				if (i == 0)
					tex._srv = tex._srvArray[i];
			}

			if (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
			{
				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
				uavDesc.Format = (DXGI_FORMAT)texDesc.format;
				uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
				uavDesc.Texture2D.MipSlice = 0;

				tex._uavArray.resize(desc.ArraySize, nullptr);
				tex._depth = desc.ArraySize;
				for (unsigned i = 0; i < desc.ArraySize; ++i)
				{
					uavDesc.Texture2DArray.FirstArraySlice = i;
					uavDesc.Texture2DArray.ArraySize = desc.ArraySize - i;
					m_device->CreateUnorderedAccessView(tex._tex2D, &uavDesc, &tex._uavArray[i]);
					if (i == 0)
						tex._uav = tex._uavArray[i];
				}
			}
		}
		else
		{
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = texDesc.mipCount;
			srvDesc.Texture2D.MostDetailedMip = 0;
			m_device->CreateShaderResourceView(tex._tex2D, &srvDesc, &tex._srv);

			if (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
			{
				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
				uavDesc.Format = (DXGI_FORMAT)texDesc.format;
				uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				uavDesc.Texture2D.MipSlice = 0;

				m_device->CreateUnorderedAccessView(tex._tex2D, &uavDesc, &tex._uav);
			}
		}
	}

	TextureID retID = -1;
	auto itTex = std::find_if(mTextures.begin(), mTextures.end(), [](const Texture& tex1) {return tex1._id == -1; });
	if (itTex != mTextures.end())
	{
		*itTex = tex;
		itTex->_id = static_cast<TextureID>((int)std::distance(mTextures.begin(), itTex));
		retID = itTex->_id;
	}
	else
	{
		tex._id = static_cast<int>(mTextures.size());
		mTextures.push_back(tex);
		retID = mTextures.back()._id;
	}
	return retID;
}

TextureID Renderer::CreateTexture2D(D3D11_TEXTURE2D_DESC & textureDesc, bool initializeSRV)
{
	Texture tex;
	tex.InitializeTexture2D(textureDesc, this, initializeSRV);
	mTextures.push_back(tex);
	mTextures.back()._id = static_cast<int>(mTextures.size() - 1);
	return mTextures.back()._id;
}

TextureID Renderer::CreateHDRTexture(const std::string& texFileName, const std::string& fileRoot /*= sHDRTextureRoot*/)
{
	// cache lookup, return early if the texture already exists
	auto found = std::find_if(mTextures.begin(), mTextures.end(), [&texFileName](auto& tex) { return tex._name == texFileName; });
	if (found != mTextures.end())
	{
		return (*found)._id;
	}

	std::string path = fileRoot + texFileName;
	
	int width = 0;
	int height = 0;
	int numComponents = 0;
	float* data = stbi_loadf(path.c_str(), &width, &height, &numComponents, 4);

	if (!data)
	{
		Log::Error("Cannot load HDR Texture: %s", path.c_str());
		return -1;
	}

	TextureDesc texDesc = {};
	texDesc.width = width;
	texDesc.height = height;
	texDesc.format = EImageFormat::RGBA32F;
	texDesc.texFileName = texFileName;
	texDesc.pData = data;
	texDesc.dataPitch = sizeof(vec4) * width;
	texDesc.mipCount = 1;
	texDesc.bGenerateMips = false;

	TextureID newTex = CreateTexture2D(texDesc);
	if (newTex == -1)
	{
		Log::Error("Cannot create HDR Texture from data: %s", path.c_str());
	}
	stbi_image_free(data);
	return newTex;
}

bool Renderer::SaveTextureToDisk(TextureID texID, const std::string& filePath, bool bConverToSRGB) const
{
	const std::string folderPath = DirectoryUtil::GetFolderPath(filePath);

	// create directory if it doesn't exist
	DirectoryUtil::CreateFolderIfItDoesntExist(folderPath);

	// get the texture object
	const Texture& tex = GetTextureObject(texID);
	D3D11_TEXTURE2D_DESC texDesc = {};
	tex._tex2D->GetDesc(&texDesc);

	// capture texture in an image
	std::unique_ptr<DirectX::ScratchImage> imgOut = std::make_unique<DirectX::ScratchImage>();
	std::unique_ptr<DirectX::ScratchImage> imgOutSRGB = std::make_unique<DirectX::ScratchImage>();
	//DirectX::CaptureTexture(m_device, m_deviceContext, tex._tex2D, *imgOut);

	if (bConverToSRGB)
	{
		// convert the source image into srgb to store on disk
		D3D11_TEXTURE2D_DESC texDesc = {};
		tex._tex2D->GetDesc(&texDesc);
		if (!SUCCEEDED(DirectX::Convert(
			*imgOut->GetImage(0, 0, 0)
			, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
			, 0
			, 0.0f
			, *imgOutSRGB
		)))
		{
			assert(false);
		}
	}

	// save image to file
	const std::string fileName = DirectoryUtil::GetFileNameWithoutExtension(filePath);
	const std::string extension = "." + DirectoryUtil::GetFileExtension(filePath);
#if 1
	const bool bArray = texDesc.ArraySize > 1;
	const bool bHasMips = texDesc.MipLevels > 1;

	for (UINT mip = 0; mip < texDesc.MipLevels; ++mip)
	{
		std::string outFilePath = folderPath + fileName; // start setting output path
		if (bHasMips)	// mip extension
		{
			outFilePath += "_mip" + std::to_string(mip);
		}
		for (UINT index = 0; index < texDesc.ArraySize; ++index)
		{
			if (bArray)	// array extension
			{
				outFilePath += "_" + std::to_string(index);
			}

			outFilePath += extension;	// finish setting output path

										// gather the parameters for saving to disk
			const DirectX::Image& image = bConverToSRGB ? *imgOutSRGB->GetImage(mip, index, 0) : *imgOut->GetImage(mip, index, 0);
			const std::wstring outFilePathW = std::wstring(RANGE(outFilePath));
			const bool bSaveHDR = extension == ".hdr" || extension == ".HDR";

			// save to disk
			const bool bSaveSuccess = bSaveHDR
				? SUCCEEDED(SaveToHDRFile(image, outFilePathW.c_str()))
				: SUCCEEDED(SaveToWICFile(image, DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, outFilePathW.c_str()));

			if (!bSaveSuccess)
			{
				Log::Error("Cannot save texture to disk: %s", outFilePath.c_str());
				MessageBox(m_Direct3D->WindowHandle(), ("Cannot save texture to disk: " + outFilePath).c_str(), "Error", MB_OK);
				return false;
			}

			Log::Info("Saved texture to file: %s", outFilePath.c_str());

			// reset output path
			outFilePath = folderPath + fileName + (bHasMips ? "_mip" + std::to_string(mip) : "");
		}
	}
#else

	std::string outFilePath = folderPath + fileName; // start setting output path
	outFilePath += extension;	// finish setting output path

	// gather the parameters for saving to disk
	const DirectX::Image& image = bConverToSRGB ? *imgOutSRGB->GetImages() : *imgOut->GetImages();
	const std::wstring outFilePathW = std::wstring(RANGE(outFilePath));
	const bool bSaveHDR = extension == ".hdr" || extension == ".HDR";

	// save to disk
	const bool bSaveSuccess = bSaveHDR
		? SUCCEEDED(SaveToHDRFile(image, outFilePathW.c_str()))
		: SUCCEEDED(SaveToWICFile(image, DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, outFilePathW.c_str()));

	if (!bSaveSuccess)
	{
		Log::Error("Cannot save texture to disk: %s", outFilePath.c_str());
		MessageBox(m_Direct3D->WindowHandle(), ("Cannot save texture to disk: " + outFilePath).c_str(), "Error", MB_OK);
		return false;
	}

	Log::Info("Saved texture to file: %s", outFilePath.c_str());

	// reset output path
	outFilePath = folderPath + fileName;
		
#endif
	return true;
}

TextureID Renderer::CreateCubemapFromFaceTextures(const std::vector<std::string>& textureFiles, bool bGenerateMips, unsigned mipLevels)
{
	constexpr size_t FACE_COUNT = 6;

	TexMetadata meta = {};

	// get subresource data for each texture to initialize the cubemap
	std::vector<D3D11_SUBRESOURCE_DATA> pSubresourceData( FACE_COUNT * mipLevels );
	std::vector<std::array<DirectX::ScratchImage, FACE_COUNT>> faceImageArray(mipLevels);
	for (unsigned mip = 0; mip < mipLevels; ++mip)
	{
		std::array<DirectX::ScratchImage, FACE_COUNT>& faceImages = faceImageArray[mip];
		for (int cubeMapFaceIndex = 0; cubeMapFaceIndex < FACE_COUNT; cubeMapFaceIndex++)
		{
			const size_t index = mip * FACE_COUNT + cubeMapFaceIndex;
			const std::string path = textureFiles[index];
			const std::wstring wpath(path.begin(), path.end());

			const std::string extension = DirectoryUtil::GetFileExtension(path);
			const bool bHDRTexture = extension == "hdr" || extension == "HDR";

			DirectX::ScratchImage* img = &faceImages[cubeMapFaceIndex];

			const bool bLoadSuccess = bHDRTexture
				? SUCCEEDED(LoadFromHDRFile(wpath.c_str(), nullptr, *img))
				: SUCCEEDED(LoadFromWICFile(wpath.c_str(), WIC_FLAGS_NONE, nullptr, *img));

			if (!bLoadSuccess)
			{
				Log::Error(textureFiles[index]);
				continue;
			}

			pSubresourceData[index].pSysMem = img->GetPixels(); // Pointer to the pixel data
			pSubresourceData[index].SysMemPitch = static_cast<UINT>(img->GetImage(0,0,0)->rowPitch); // Line width in bytes
			pSubresourceData[index].SysMemSlicePitch = static_cast<UINT>(img->GetImages()->slicePitch); // This is only used for 3d textures

			if (cubeMapFaceIndex == 0 && mip == 0)
			{
				meta = faceImages[0].GetMetadata();
			}
		}
	}

#if _DEBUG
	Log::Info("\tLoading Cubemap Texture: %s", textureFiles.back().c_str());
#endif

	// initialize the destination texture desc
	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.Width     = static_cast<UINT>(meta.width);
	texDesc.Height    = static_cast<UINT>(meta.height);
	texDesc.MipLevels = bGenerateMips ? mipLevels : static_cast<UINT>(meta.mipLevels);
	texDesc.ArraySize = FACE_COUNT;
	texDesc.Format    = meta.format;
	texDesc.CPUAccessFlags = 0;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	if (bGenerateMips)
	{
		texDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	}
	

	// create the resource
	ID3D11Texture2D* finalCubemapTexture;
	const D3D11_SUBRESOURCE_DATA* pData = bGenerateMips ? nullptr : pSubresourceData.data();
	HRESULT hr = m_device->CreateTexture2D(&texDesc, pData, &finalCubemapTexture);
	if (hr != S_OK)
	{
		Log::Error(std::string("Cannot create cubemap texture: ") + StrUtil::split(textureFiles.front(), '_').front());
		return -1;
	}

	// create cubemap srv
	ID3D11ShaderResourceView* cubeMapSRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC cubemapDesc;
	cubemapDesc.Format = texDesc.Format;
	cubemapDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
	cubemapDesc.TextureCube.MipLevels = texDesc.MipLevels;
	cubemapDesc.TextureCube.MostDetailedMip = 0;
	hr = m_device->CreateShaderResourceView(finalCubemapTexture, &cubemapDesc, &cubeMapSRV);
	if (hr != S_OK)
	{
		Log::Error(std::string("Cannot create Shader Resource View for ") + StrUtil::split(textureFiles.front(), '_').front());
		return -1;
	}

	// copy the mip levels into the final resource
	if (bGenerateMips)
	{
		//https://www.gamedev.net/forums/topic/599837-dx11-createtexture2d-automatic-mips-initial-data/
		//m_deviceContext->GenerateMips(cubeMapSRV);
		for (unsigned mip = 0; mip < mipLevels; ++mip)
		{
			for (int cubeMapFaceIndex = 0; cubeMapFaceIndex < FACE_COUNT; cubeMapFaceIndex++)
			{
				const size_t index = mip * FACE_COUNT + cubeMapFaceIndex;
				//m_deviceContext->UpdateSubresource(
				//	finalCubemapTexture, D3D11CalcSubresource(mip, cubeMapFaceIndex, mipLevels)
				//	, nullptr //&box
				//	, pSubresourceData[index].pSysMem			// data
				//	, pSubresourceData[index].SysMemPitch		// row pitch
				//	, pSubresourceData[index].SysMemSlicePitch	// depth pitch
				//);
			}
		}
	}

	// return param
	Texture cubemapOut;
	cubemapOut._srv = cubeMapSRV;
	cubemapOut._name = "todo:Skybox file name";
	cubemapOut._tex2D = finalCubemapTexture;
	cubemapOut._height = texDesc.Height;
	cubemapOut._width = texDesc.Width;
	cubemapOut._id = static_cast<int>(mTextures.size());
	mTextures.push_back(cubemapOut);
	return cubemapOut._id;
}


#ifdef max
#undef max
#endif
BufferID Renderer::CreateBuffer(const BufferDesc & bufferDesc, const void* pData /*=nullptr*/)
{
	Buffer buffer(bufferDesc);
	buffer.Initialize(m_device, pData);
	return static_cast<int>([&]() {
		switch (bufferDesc.mType)
		{
		case VERTEX_BUFER:
			mVertexBuffers.push_back(buffer);
			return mVertexBuffers.size() - 1;
		case INDEX_BUFFER:
			mIndexBuffers.push_back(buffer);
			return mIndexBuffers.size() - 1;
		case COMPUTE_RW_BUFFER:
			mUABuffers.push_back(buffer);
			return mUABuffers.size() - 1;
		default:
			Log::Warning("Unknown Buffer Type");
			return std::numeric_limits<size_t>::max();
		}
	}());
}

SamplerID Renderer::CreateSamplerState(D3D11_SAMPLER_DESC & samplerDesc)
{
	ID3D11SamplerState*	pSamplerState;
	HRESULT hr = m_device->CreateSamplerState(&samplerDesc, &pSamplerState);
	if (FAILED(hr))
	{
		Log::Error("Cannot create sampler state\n");
	}

	Sampler out;
	out._id = static_cast<SamplerID>(mSamplers.size());
	out._samplerState = pSamplerState;
	out._name = "";	// ?
	mSamplers.push_back(out);
	return out._id;
}

DepthStencilStateID Renderer::AddDepthStencilState(bool bEnableDepth, bool bEnableStencil)
{
	DepthStencilState* newDSState = (DepthStencilState*)malloc(sizeof(DepthStencilState));

	HRESULT result;
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	ZeroMemory(&depthStencilDesc, sizeof(depthStencilDesc));

	// Set up the description of the stencil state.
	depthStencilDesc.DepthEnable = bEnableDepth;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

	depthStencilDesc.StencilEnable = bEnableStencil;
	depthStencilDesc.StencilReadMask = 0xFF;
	depthStencilDesc.StencilWriteMask = 0xFF;

	// Stencil operations if pixel is front-facing.
	depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Stencil operations if pixel is back-facing.
	depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Create the depth stencil state.
	result = m_device->CreateDepthStencilState(&depthStencilDesc, &newDSState);
	if (FAILED(result))
	{
		Log::Error("Depth Stencil");
		return false;
	}

	mDepthStencilStates.push_back(newDSState);
	return static_cast<DepthStencilStateID>(mDepthStencilStates.size() - 1);
}

DepthStencilStateID Renderer::AddDepthStencilState(const D3D11_DEPTH_STENCIL_DESC & dsDesc)
{
	DepthStencilState* newDSState = (DepthStencilState*)malloc(sizeof(DepthStencilState));
	HRESULT result;

	result = m_device->CreateDepthStencilState(&dsDesc, &newDSState);
	if (FAILED(result))
	{
		Log::Error("Depth Stencil");
		return false;
	}

	mDepthStencilStates.push_back(newDSState);
	return static_cast<DepthStencilStateID>(mDepthStencilStates.size() - 1);
}

BlendStateID Renderer::AddBlendState()
{
	D3D11_RENDER_TARGET_BLEND_DESC rtBlendDesc = {};
	rtBlendDesc.BlendEnable = true;
	rtBlendDesc.BlendOp = D3D11_BLEND_OP_ADD;
	rtBlendDesc.SrcBlend = D3D11_BLEND_SRC_ALPHA;
	rtBlendDesc.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	rtBlendDesc.BlendOpAlpha = D3D11_BLEND_OP_MIN;
	rtBlendDesc.SrcBlendAlpha = D3D11_BLEND_ZERO;
	rtBlendDesc.DestBlendAlpha = D3D11_BLEND_ONE;
	rtBlendDesc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	
	D3D11_BLEND_DESC desc = {};
	desc.RenderTarget[0] = rtBlendDesc;

	BlendState blend;
	m_device->CreateBlendState(&desc, &blend.ptr);
	mBlendStates.push_back(blend);

	return static_cast<BlendStateID>(mBlendStates.size() - 1);
}

RenderTargetID Renderer::AddRenderTarget(const Texture& textureObj, D3D11_RENDER_TARGET_VIEW_DESC& RTVDesc)
{
	RenderTarget newRenderTarget;
	newRenderTarget.texture = textureObj;
	HRESULT hr = m_device->CreateRenderTargetView(newRenderTarget.texture._tex2D, &RTVDesc, &newRenderTarget.pRenderTargetView);
	if (!SUCCEEDED(hr))
	{
		Log::Error("Render Target View");
		return -1;
	}

	mRenderTargets.push_back(newRenderTarget);
	return static_cast<int>(mRenderTargets.size() - 1);
}


RenderTargetID Renderer::AddRenderTarget(const RenderTargetDesc& renderTargetDesc)
{
	RenderTarget newRenderTarget;

	// create the texture of the render target
	const TextureID texID = CreateTexture2D(renderTargetDesc.textureDesc);
	Texture& textureObj = const_cast<Texture&>(GetTextureObject(texID));
	newRenderTarget.texture = textureObj;
	
	// create the render target view
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = static_cast<DXGI_FORMAT>(renderTargetDesc.format);
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	HRESULT hr = m_device->CreateRenderTargetView(newRenderTarget.texture._tex2D, &rtvDesc, &newRenderTarget.pRenderTargetView);
	if (!SUCCEEDED(hr))
	{
		Log::Error("Cannot create Render Target View");
		return -1;
	}

	// register & return
	mRenderTargets.push_back(newRenderTarget);
	return static_cast<int>(mRenderTargets.size() - 1);
}

std::vector<DepthTargetID> Renderer::AddDepthTarget(const DepthTargetDesc& depthTargetDesc)
{
	const bool bIsDepthTargetCubemap = depthTargetDesc.textureDesc.bIsCubeMap;
	const int numTextures = depthTargetDesc.textureDesc.arraySize * (bIsDepthTargetCubemap ? 6 : 1);
	const bool bIsDepthTargetArray = numTextures > 1;

	// allocate new depth target
	std::vector<DepthTargetID> newDepthTargetIDs(numTextures, -1);
	std::vector<DepthTarget> newDepthTargets(numTextures);
	for (DepthTarget& newDepthTarget : newDepthTargets)
	{
		newDepthTarget.pDepthStencilView = (ID3D11DepthStencilView*)malloc(sizeof(*newDepthTarget.pDepthStencilView));
		memset(newDepthTarget.pDepthStencilView, 0, sizeof(*newDepthTarget.pDepthStencilView));
	}

	// create depth texture
	const TextureID texID = CreateTexture2D(depthTargetDesc.textureDesc);
	Texture& textureObj = const_cast<Texture&>(GetTextureObject(texID));

	// create depth stencil view
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = static_cast<DXGI_FORMAT>(depthTargetDesc.format);
	dsvDesc.ViewDimension = bIsDepthTargetArray ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2DArray.MipSlice = 0;

	const int faceCount = (bIsDepthTargetCubemap ? 6 : 1);
	const int cubemapCount = bIsDepthTargetCubemap ? (numTextures / faceCount) : numTextures;
	for (int i = 0; i < cubemapCount; ++i)
	{
		for (int face = 0; face < faceCount; ++face)
		{
			const int depthTargetIndex = i * faceCount + face;
			DepthTarget& newDepthTarget = newDepthTargets[depthTargetIndex];
			dsvDesc.Texture2DArray.ArraySize = numTextures - (face + i * faceCount);
			dsvDesc.Texture2DArray.FirstArraySlice = face + i * faceCount;

			HRESULT hr = m_device->CreateDepthStencilView(textureObj._tex2D, &dsvDesc, &newDepthTarget.pDepthStencilView);
			if (FAILED(hr))
			{
				Log::Error("Depth Stencil Target View");
				continue;
			}

			// register
			newDepthTarget.texture = textureObj;
			mDepthTargets.push_back(newDepthTarget);
			newDepthTargetIDs[depthTargetIndex] = static_cast<DepthTargetID>(mDepthTargets.size() - 1);
		}
	}

	return newDepthTargetIDs;
}


bool Renderer::RecycleDepthTarget(DepthTargetID depthTargetID, const DepthTargetDesc& newDepthTargetDesc)
{
	const int numTextures = newDepthTargetDesc.textureDesc.arraySize;
	assert(newDepthTargetDesc.textureDesc.arraySize == 1); // depth target array not supported.
	
	// recycle depth target texture and DSV
	const TextureID texID = GetDepthTargetTexture(depthTargetID);
	Texture& textureObj = const_cast<Texture&>(GetTextureObject(texID));
	textureObj.Release();
	mDepthTargets[depthTargetID].pDepthStencilView->Release();
	mDepthTargets[depthTargetID].pDepthStencilView = nullptr;

	// CreateTexture2D will use the first Release()d Texture instead of adding a new one.
	CreateTexture2D(newDepthTargetDesc.textureDesc);

	// create depth stencil view
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = static_cast<DXGI_FORMAT>(newDepthTargetDesc.format);
	dsvDesc.ViewDimension = numTextures == 1 ? D3D11_DSV_DIMENSION_TEXTURE2D : D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Texture2DArray.MipSlice = 0;

	for (int i = 0; i < numTextures; ++i)
	{
		dsvDesc.Texture2DArray.ArraySize = numTextures - i;
		dsvDesc.Texture2DArray.FirstArraySlice = i;
		HRESULT hr = m_device->CreateDepthStencilView(textureObj._tex2D, &dsvDesc, &mDepthTargets[depthTargetID].pDepthStencilView);
		if (FAILED(hr))
		{
			Log::Error("Depth Stencil Target View");
			continue;
		}
	}

	return true;
}

const Texture& Renderer::GetTextureObject(TextureID id) const
{
	assert(id >= 0 && static_cast<unsigned>(id) < mTextures.size());
	return mTextures[id];
}

const TextureID Renderer::GetTexture(const std::string name) const
{
	auto found = std::find_if(mTextures.begin(), mTextures.end(), [&name](auto& tex) { return tex._name == name; });
	if (found != mTextures.end())
	{
		return found->_id;
	}
	Log::Error("Texture not found: " + name);
	return -1;
}


void Renderer::SetShader(ShaderID id, bool bUnbindRenderTargets, bool bUnbindTextures)
{
	assert(false);
}

void Renderer::SetVertexBuffer(BufferID bufferID)
{
	mPipelineState.vertexBuffer = bufferID;
	UINT offset = 0;
}

void Renderer::SetIndexBuffer(BufferID bufferID)
{
	mPipelineState.indexBuffer = bufferID;
}

void Renderer::SetUABuffer(BufferID bufferID)
{
	//mPipelineState.indexBuffer = bufferID;
}

void Renderer::ResetPipelineState()
{
	mPipelineState.shader = -1;
}


void Renderer::SetViewport(const unsigned width, const unsigned height)
{
	mPipelineState.viewPort.TopLeftX = 0;
	mPipelineState.viewPort.TopLeftY = 0;
	mPipelineState.viewPort.Width	= static_cast<float>(width);
	mPipelineState.viewPort.Height	= static_cast<float>(height);
	mPipelineState.viewPort.MinDepth = 0;
	mPipelineState.viewPort.MaxDepth = 1;
}

void Renderer::SetViewport(const D3D11_VIEWPORT & viewport)
{
	mPipelineState.viewPort = viewport;
}

void Renderer::SetConstant4x4f(const char* cName, const XMMATRIX& matrix)
{
	// maybe read from SIMD registers?
	XMFLOAT4X4 m;	XMStoreFloat4x4(&m, matrix);
	float* data = &m.m[0][0];
	SetConstant(cName, data);
}

void Renderer::SetConstant(const char * cName, const void * data)
{
	// Here, we write to the CPU address of the constant buffer if the contents are updated.
	// otherwise we don't write and flag the buffer that contains the GPU address dirty.
	// When all the constants are set on the CPU side, right before the draw call,
	// we will use a mapped resource as a block of memory, transfer our CPU
	// version of constants to there, and then send it to GPU CBuffers at one call as a block memory.
	// Otherwise, we would have to make an API call each time we set the constants, which would be slower.
	// Read more here: https://developer.nvidia.com/sites/default/files/akamai/gamedev/files/gdc12/Efficient_Buffer_Management_McDonald.pdf
	//      and  here: https://developer.nvidia.com/content/constant-buffers-without-constant-pain-0

	Shader* shader = mShaders[mPipelineState.shader];

#if 1
	// LINEAR LOOKUP
	bool found = false;
	for (const ConstantBufferMapping& bufferSlotIDPair : shader->m_constants)
	{
		const size_t GPUcBufferSlot = bufferSlotIDPair.first;
		const CPUConstantID constID = bufferSlotIDPair.second;
		CPUConstant& c = shader->mCPUConstantBuffers[constID];
		if (strcmp(cName, c._name.c_str()) == 0)		// if name matches
		{
			found = true;
			memcpy(c._data, data, c._size);
			shader->mConstantBuffers[GPUcBufferSlot].dirty = true;
			//break;	// ensures write on first occurrence
		}
	}
	if (!found)
	{
		Log::Error("CONSTANT NOT FOUND: %s", cName);
	}

#else
	// TODO: Fix binary search algorithm...
	// BINARY SEARCH 
	const auto& BinarySearch = [cName, &shader]()
	{
		bool bKeepSearching = true;
		size_t lowIndex  = 0;
		size_t highIndex = shader->m_constants.size() - 1;
		size_t currIndex = highIndex / 2;

#if LOG_SEARCH
		{
			Log::Info("BinarySearch: %s", cName);
			for (const auto& slotIndexPair : shader->m_constants)
			{	// dump sorted buffer slot
				const char* constantName = CPUConstant::Get(slotIndexPair.second)._name.c_str();
				Log::Info(" GPU:%d | CPU:%d - %s", slotIndexPair.first, slotIndexPair.second, constantName);
			}
			Log::Info("--------------------------");
		}
#endif

		while (bKeepSearching)
		{
#if LOG_SEARCH
			Log::Info("begin: low:%d\tcur:%d\thi:%d", lowIndex, currIndex, highIndex);
#endif
			const ConstantBufferMapping& bufferSlotIDPair = shader->m_constants[currIndex];
			const CPUConstantID constID = bufferSlotIDPair.second;
			const CPUConstant& c = CPUConstant::Get(constID);
			int res = strcmp(cName, c._name.c_str());
#if LOG_SEARCH
			Log::Info(" \"%s\" strcmp \"%s\" -> %d", cName, c._name.c_str(), res);
#endif
			if (res == 0)		
			{	
#if LOG_SEARCH
				Log::Info("found: %s", c._name.c_str());
#endif
				return currIndex;
			}

			else if (res > 0)
			{
				lowIndex = currIndex;
				currIndex = lowIndex + (highIndex - lowIndex + 1) / 2;
#if LOG_SEARCH
				{
					const ConstantBufferMapping& bufferSlotIDPair = shader->m_constants[currIndex];
					const CPUConstantID constID = bufferSlotIDPair.second;
					const CPUConstant& c = CPUConstant::Get(constID);
					Log::Info("looking next(%s)", c._name.c_str());
				}
#endif
			}

			else
			{
				highIndex = currIndex - 1;
				currIndex = lowIndex + (highIndex - lowIndex) / 2;
#if LOG_SEARCH
				{
					const ConstantBufferMapping& bufferSlotIDPair = shader->m_constants[currIndex];
					const CPUConstantID constID = bufferSlotIDPair.second;
					const CPUConstant& c = CPUConstant::Get(constID);
					Log::Info("looking previous(%s)", c._name.c_str());
				}
#endif
			}

#if LOG_SEARCH
			Log::Info("end: low:%d\tcur:%d\thi:%d\n", lowIndex, currIndex, highIndex);
#endif
			bKeepSearching = lowIndex < highIndex;// || ((currIndex == lowIndex) && (lowIndex == highIndex));
		}

		Log::Error("CONSTANT NOT FOUND: %s", cName);
		return currIndex;
	};

	size_t bufferMappingIndex = BinarySearch();
	const ConstantBufferMapping& bufferSlotIDPair = shader->m_constants[bufferMappingIndex];
	const size_t GPUcBufferSlot = bufferSlotIDPair.first;
	const CPUConstantID constID = bufferSlotIDPair.second;
	CPUConstant& c = CPUConstant::Get(constID);
	if (memcmp(c._data, data, c._size) != 0)	// copy data if its not the same
	{
		memcpy(c._data, data, c._size);
		shader->mConstantBuffers[GPUcBufferSlot].dirty = true;
	}
#endif

}

void Renderer::SetTexture_(const char* texName, TextureID tex, unsigned slice /*= 0 /* only for texture arrays */)
{
	assert(tex >= 0);

	const Shader* shader = mShaders[mPipelineState.shader];
	const std::string textureName = std::string(texName);

	const bool bFound = shader->HasTextureBinding(textureName);

	if (bFound)
	{
		SetTextureCommand cmd(tex, shader->GetTextureBinding(textureName), slice);
		mSetTextureCmds.push(cmd);
	}

#ifdef _DEBUG
	if (!bFound)
	{
		Log::Error("Texture not found: \"%s\" in Shader(Id=%d) \"%s\"", texName, mPipelineState.shader, shader->Name().c_str());
	}
#endif
}

void Renderer::SetTextureArray(const char* texName, const std::array<TextureID, TEXTURE_ARRAY_SIZE>& TextureIDs, unsigned numTextures)
{
	const Shader* shader = mShaders[mPipelineState.shader];
	if (shader->HasTextureBinding(texName))
	{
		SetTextureCommand cmd(TextureIDs, numTextures, shader->GetTextureBinding(texName), 0);
		mSetTextureCmds.push(cmd);
	}
#ifdef _DEBUG
	else
	{
		Log::Error("Texture not found: \"%s\" in Shader(Id=%d) \"%s\"", texName, mPipelineState.shader, shader->Name().c_str());
	}
#endif
}


void Renderer::SetTexture(const char * texName, TextureID tex) { SetTexture_(texName, tex, 0); }

void Renderer::SetRWTexture(const char* texName, TextureID tex)
{
	assert(tex >= 0);

	const Shader* shader = mShaders[mPipelineState.shader];
	const std::string textureName = std::string(texName);

	const bool bFound = shader->HasTextureBinding(textureName);

	if (bFound)
	{
		SetTextureCommand cmd(tex, shader->GetTextureBinding(textureName), 0, true);
		mSetTextureCmds.push(cmd);
	}

#ifdef _DEBUG
	if (!bFound)
	{
		Log::Error("UnorderedAccessTexture not found: \"%s\" in Shader(Id=%d) \"%s\"", texName, mPipelineState.shader, shader->Name().c_str());
	}
#endif
}

void Renderer::SetSamplerState(const char * samplerName, SamplerID samplerID)
{
	const Shader* shader = mShaders[mPipelineState.shader];

	const bool bFound = shader->HasSamplerBinding(samplerName);

	if (bFound)
	{
		SetSamplerCommand cmd(samplerID, shader->GetSamplerBinding(samplerName));
		mSetSamplerCmds.push(cmd);
	}

#ifdef _DEBUG
	if (!bFound)
	{
		Log::Error("Sampler not found: \"%s\" in Shader(Id=%d) \"%s\"\n", samplerName, mPipelineState.shader, shader->Name().c_str());
	}
#endif
}

void Renderer::SetRasterizerState(RasterizerStateID rsStateID)
{
	assert(rsStateID > -1 && static_cast<size_t>(rsStateID) < mRasterizerStates.size());
	mPipelineState.rasterizerState = rsStateID;
}

void Renderer::SetBlendState(BlendStateID blendStateID)
{
	assert(blendStateID > -1 && static_cast<size_t>(blendStateID) < mBlendStates.size());
	mPipelineState.blendState = blendStateID;
}

void Renderer::SetDepthStencilState(DepthStencilStateID depthStencilStateID)
{
	assert(depthStencilStateID > -1 && static_cast<size_t>(depthStencilStateID) < mDepthStencilStates.size());
	mPipelineState.depthStencilState = depthStencilStateID;
}

void Renderer::SetScissorsRect(int left, int right, int top, int bottom)
{
	D3D11_RECT rects[1];
	rects[0].left = left;
	rects[0].right = right;
	rects[0].top = top;
	rects[0].bottom = bottom;
	
	// only called from debug for now, so immediate api call. rethink: make this command?
	//m_deviceContext->RSSetScissorRects(1, rects);
}

void Renderer::BindRenderTarget(RenderTargetID rtvID)
{
	assert(rtvID > -1 && static_cast<size_t>(rtvID) < mRenderTargets.size());
	//for(RenderTargetID& hRT : m_state._boundRenderTargets) 
	mPipelineState.renderTargets = { rtvID };
}

void Renderer::BindDepthTarget(DepthTargetID dsvID)
{
	assert(dsvID > -1 && static_cast<size_t>(dsvID) < mDepthTargets.size());
	mPipelineState.depthTargets = dsvID;
}

void Renderer::UnbindRenderTargets()
{
	mPipelineState.renderTargets = { -1, -1, -1, -1, -1, -1 };
}

void Renderer::UnbindDepthTarget()
{
	mPipelineState.depthTargets = -1;
}

// temp
void Renderer::DrawLine()
{
	// draw line between 2 coords
	vec3 pos1 = vec3(0, 0, 0);
	vec3 pos2 = pos1;	pos2.x() += 5.0f;

	SetConstant3f("p1", pos1);
	SetConstant3f("p2", pos2);
	SetConstant3f("color", LinearColor::green.Value());
	Apply();
	Draw(1, EPrimitiveTopology::POINT_LIST);
}

void Renderer::DrawLine(const vec3& pos1, const vec3& pos2, const vec3& color)
{
	SetConstant3f("p1", pos1);
	SetConstant3f("p2", pos2);
	SetConstant3f("color", color);
	Apply();
	Draw(1, EPrimitiveTopology::POINT_LIST);
}

// todo: try to remove this dependency
#include "Engine/Engine.h"	
// assumes (0, 0) is Bottom Left corner of the screen.
void Renderer::DrawQuadOnScreen(const DrawQuadOnScreenCommand& cmd)
{														// warning:
	const int screenWidth = mWindowSettings.width;		// 2 copies of renderer settings, one here on in Engine
	const int screenHeight = mWindowSettings.height;	// dynamic window size change might break things...
	const float& dimx = cmd.dimensionsInPixels.x();
	const float& dimy = cmd.dimensionsInPixels.y();
	const float posx = cmd.bottomLeftCornerScreenCoordinates.x() * 2.0f - screenWidth;	// NDC is [-1, 1] ; if (0,0) is given
	const float posy = cmd.bottomLeftCornerScreenCoordinates.y() * 2.0f - screenHeight;	// texture is drawn in bottom left corner of the screen
	const vec2 posCenter( (posx + dimx)/screenWidth, (posy + dimy) / screenHeight);

	const XMVECTOR scale = vec3(dimx / screenWidth, dimy / screenHeight, 0.0f);
	const XMVECTOR translation = vec3(posCenter.x(), posCenter.y(), 0);
	const XMMATRIX transformation = XMMatrixAffineTransformation(scale, vec3::Zero, XMQuaternionIdentity(), translation);
	
	const auto IABuffers = ENGINE->GetGeometryVertexAndIndexBuffers(EGeometry::FULLSCREENQUAD);

	SetConstant4x4f("screenSpaceTransformation", transformation);
	SetConstant1f("isDepthTexture", cmd.bIsDepthTexture ? 1.0f : 0.0f);
	SetConstant1i("numChannels", cmd.numChannels);
	SetTexture("inputTexture", cmd.texture);
	SetVertexBuffer(IABuffers.first);
	SetIndexBuffer(IABuffers.second);
	Apply();
	DrawIndexed();
}


void Renderer::BeginRender(const ClearCommand & clearCmd)
{
#if 0
	if (clearCmd.bDoClearColor)
	{
		for (const RenderTargetID rtv : mPipelineState.renderTargets)
		{
			if (rtv >= 0)	m_deviceContext->ClearRenderTargetView(mRenderTargets[rtv].pRenderTargetView, clearCmd.clearColor.data());
			else			Log::Error("Begin called with clear color command without a render target bound to pipeline!");
		}
	}

	const bool bClearDepthStencil = clearCmd.bDoClearDepth || clearCmd.bDoClearStencil;
	if (bClearDepthStencil)
	{
		const DepthTargetID dsv = mPipelineState.depthTargets;
		const UINT clearFlag = [&]() -> UINT	
		{
			if (clearCmd.bDoClearDepth && clearCmd.bDoClearStencil)
				return D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL;

			if (clearCmd.bDoClearDepth)
				return D3D11_CLEAR_DEPTH;

			return D3D11_CLEAR_STENCIL;
		}();

		if (dsv >= 0)	m_deviceContext->ClearDepthStencilView(mDepthTargets[dsv].pDepthStencilView, clearFlag, clearCmd.clearDepth, clearCmd.clearStencil);
		else			Log::Error("Begin called with clear depth_stencil command without a depth target bound to pipeline!");
	}
#endif
}

void Renderer::BeginFrame()
{
	mRenderStats = { 0, 0, 0 };
}

void Renderer::EndFrame()
{
	m_Direct3D->EndFrame();
}


void Renderer::UpdateBuffer(BufferID buffer, const void * pData)
{
	assert(buffer >= 0 && buffer < mVertexBuffers.size());
	mVertexBuffers[buffer].Update(this, pData);
}

void Renderer::Apply()
{
	assert(false);
}

void Renderer::BeginEvent(const std::string & marker)
{
#if _DEBUG
	StrUtil::UnicodeString umarker(marker);
	//m_Direct3D->m_annotation->BeginEvent(umarker.GetUnicodePtr());
#endif
}

void Renderer::EndEvent()
{
#if _DEBUG
	//m_Direct3D->m_annotation->EndEvent();
#endif
}

#if 0
void Renderer::DrawIndexed(EPrimitiveTopology topology)
{
	const Buffer& VertexBuffer = mVertexBuffers[mPipelineState.vertexBuffer];
	const Buffer& IndexBuffer = mIndexBuffers[mPipelineState.indexBuffer];

	const unsigned numIndices = IndexBuffer.mDesc.mElementCount;
	const unsigned numVertices = VertexBuffer.mDesc.mElementCount;

	mPipelineState.topology = topology;
	if (mPipelineState.topology != mPrevPipelineState.topology) 
	{ 
		m_deviceContext->IASetPrimitiveTopology(static_cast<D3D_PRIMITIVE_TOPOLOGY>(topology)); 
	}

	m_deviceContext->DrawIndexed(numIndices, 0, 0);
	
	++mRenderStats.numDrawCalls;
	mRenderStats.numIndices += numIndices;
	mRenderStats.numVertices += numVertices;
	mRenderStats.numTriangles += numIndices / 3;
}

void Renderer::DrawIndexedInstanced(int instanceCount, EPrimitiveTopology topology /*= EPrimitiveTopology::POINT_LIST*/)
{
	const Buffer& VertexBuffer = mVertexBuffers[mPipelineState.vertexBuffer];
	const Buffer& IndexBuffer = mIndexBuffers[mPipelineState.indexBuffer];

	const unsigned numIndices = IndexBuffer.mDesc.mElementCount;
	const unsigned numVertices = VertexBuffer.mDesc.mElementCount;

	mPipelineState.topology = topology;
	if (mPipelineState.topology != mPrevPipelineState.topology)
	{
		m_deviceContext->IASetPrimitiveTopology(static_cast<D3D_PRIMITIVE_TOPOLOGY>(topology));
	}

	m_deviceContext->DrawIndexedInstanced(numIndices, instanceCount, 0, 0, 0);

	++mRenderStats.numDrawCalls;
	mRenderStats.numIndices += numIndices;
	mRenderStats.numVertices += numVertices;
	mRenderStats.numTriangles += numIndices / 3;
}

void Renderer::Draw(int vertCount, EPrimitiveTopology topology /*= EPrimitiveTopology::POINT_LIST*/)
{
	m_deviceContext->IASetPrimitiveTopology(static_cast<D3D_PRIMITIVE_TOPOLOGY>(topology));
	m_deviceContext->Draw(vertCount, 0);
	
	++mRenderStats.numDrawCalls;
	mRenderStats.numVertices += vertCount;
}

void Renderer::Dispatch(int x, int y, int z)
{
	m_deviceContext->Dispatch(x, y, z);
}
#endif