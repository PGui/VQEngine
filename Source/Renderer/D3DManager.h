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

#pragma once

#if defined(D3D11)
#include <d3d11_1.h>
#elif defined(D3D12)
// https://www.3dgep.com/learning-directx12-1/
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

// D3D12 extension library.
//#include <d3dx12.h>
#else
#error "Graphics API not defined."
#endif

#include <string>

// auto convert for enum classes: https://stackoverflow.com/questions/8357240/how-to-automatically-convert-strongly-typed-enum-into-int/8357462#8357462
template<typename E>
constexpr auto to_underlying(E e) noexcept
{
	return static_cast<typename std::underlying_type_t<E>>(e);
}

class D3DManager
{
	friend class Renderer;

public:
	D3DManager();
	~D3DManager();

	bool Initialize(int width, int height, const bool VSYNC, HWND hwnd, const bool FULL_SCREEN, DXGI_FORMAT FrameBufferFormat);
	void Shutdown();

	void EndFrame();

	void GetVideoCardInfo(char*, int&);
	
	float    AspectRatio() const;
	unsigned WindowWidth() const;
	unsigned WindowHeight() const;
	inline HWND	 WindowHandle() const { return m_hwnd; }

	void ReportLiveObjects(const std::string& LogHeader = "") const;

private:
	bool InitSwapChain(HWND hwnd, bool fullscreen, int scrWidth, int scrHeight, unsigned numerator, unsigned denominator, DXGI_FORMAT FrameBufferFormat);

private:
	bool						m_vsync_enabled;
	int							m_VRAM;
	char						m_GPUDescription[128];
	HWND						m_hwnd;
	unsigned					m_wndWidth, m_wndHeight;

#if defined(D3D11)
#ifndef HIGHER_FEATURE_LEVEL
	IDXGISwapChain*			m_swapChain;
#else
	IDXGISwapChain1*			m_swapChain;
#endif // HIGHER_FEATURE_LEVEL
	
	ID3D11Device*				m_device;			// shared ptr
	ID3D11DeviceContext*		m_deviceContext;
	


#if _DEBUG
	ID3D11Debug*				m_debug;
	ID3DUserDefinedAnnotation*	m_annotation;
#endif


#elif defined(D3D12)
	short m_numFrames = 3;
	IDXGISwapChain4*			m_swapChain;
	ID3D12Device*				m_device;
	//ID3D12DeviceContext*		m_deviceContext;

#if _DEBUG
	ID3D12Debug*				m_debug;
	// ID3DUserDefinedAnnotation*	m_annotation; // TODO:
#endif


#else
	// Other APIs

#endif // API_DEFINE
};

