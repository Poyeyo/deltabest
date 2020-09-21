// proxydll.cpp
#include "stdafx.h"
#include "proxydll.h"

#include <io.h>
#include <string>
using namespace std ;

// Handles to the various plugin's functions
vector<HINSTANCE>		g_plugin_dlls ;
vector<D3D9_CALLBACK_T>	g_init_callbacks ;
vector<D3D9_CALLBACK_T>	g_release_callbacks ;
vector<D3D9_CALLBACK_T>	g_endscene_callbacks ;
vector<D3D9_CALLBACK_T>	g_present_callbacks ;

// global variables
myIDirect3DSwapChain9*  gl_pmyIDirect3DSwapChain9;
myIDirect3DDevice9*		gl_pmyIDirect3DDevice9;
myIDirect3D9*			gl_pmyIDirect3D9;
HINSTANCE				gl_hOriginalDll;
HINSTANCE				gl_hThisInstance;

BOOL APIENTRY DllMain( HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	// to avoid compiler lvl4 warnings 
    LPVOID lpDummy = lpReserved;
    lpDummy = NULL;
    
    switch (ul_reason_for_call)
	{
	    case DLL_PROCESS_ATTACH: InitInstance(hModule); break;
	    case DLL_PROCESS_DETACH: ExitInstance(); break;
        
        case DLL_THREAD_ATTACH:  break;
	    case DLL_THREAD_DETACH:  break;
	}
    return TRUE;
}

// Exported function (faking d3d9.dll's one-and-only export)
IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion)
{
	if (!gl_hOriginalDll)
	{
		LoadOriginalDll(); // looking for the "right d3d9.dll"
	}
	
	// Hooking IDirect3D Object from Original Library
	typedef IDirect3D9 *(WINAPI* D3D9_Type)(UINT SDKVersion);
	D3D9_Type D3DCreate9_fn = (D3D9_Type) GetProcAddress( gl_hOriginalDll, "Direct3DCreate9");
    
    // Debug
	if (!D3DCreate9_fn) 
    {
        OutputDebugString("PROXYDLL: Pointer to original D3DCreate9 function not received ERROR ****\r\n");
        ::ExitProcess(0); // exit the hard way
    }
	
	// Request pointer from Original Dll. 
	IDirect3D9 *pIDirect3D9_orig = D3DCreate9_fn(SDKVersion);
	
	// Create my IDirect3D8 object and store pointer to original object there.
	// note: the object will delete itself once Ref count is zero (similar to COM objects)
	gl_pmyIDirect3D9 = new myIDirect3D9(pIDirect3D9_orig);

	// Return pointer to hooking Object instead of "real one"
	return (gl_pmyIDirect3D9);
}

void InitInstance(HANDLE hModule) 
{
	OutputDebugString("PROXYDLL: InitInstance called.\r\n");
	
	// Initialisation
	gl_hOriginalDll				= NULL;
	gl_hThisInstance			= NULL;
	gl_pmyIDirect3D9			= NULL;
	gl_pmyIDirect3DDevice9		= NULL;
	gl_pmyIDirect3DSwapChain9	= NULL;
	
	// Storing Instance handle into global var
	gl_hThisInstance = (HINSTANCE) hModule;

	// Scan for any rendering plugin dlls
	LoadPluginDlls() ;
}

void LoadOriginalDll(void)
{
    char buffer[MAX_PATH];
    
    // Getting path to system dir and to d3d8.dll
	::GetSystemDirectory(buffer,MAX_PATH);

	// Append dll name
	strcat(buffer,"\\d3d9.dll");
	
	// try to load the system's d3d9.dll, if pointer empty
	if (!gl_hOriginalDll)
	{
		gl_hOriginalDll = ::LoadLibrary(buffer);
	}

	// Debug
	if (!gl_hOriginalDll)
	{
		OutputDebugString("PROXYDLL: Original d3d9.dll not loaded ERROR ****\r\n");
		::ExitProcess(0); // exit the hard way
	}
}


static void RegisterPluginDLL( const char *dll_filename )
{
	HINSTANCE plugin_dll = ::LoadLibrary( dll_filename );
	if ( plugin_dll )
	{
		BOOL_FN_T dll_wants_d3d_updates = (BOOL_FN_T) GetProcAddress( plugin_dll, "WantsD3D9Updates" );
		if ( dll_wants_d3d_updates && dll_wants_d3d_updates() )
		{
			g_plugin_dlls.push_back( plugin_dll ) ;

			D3D9_CALLBACK_T plugin_callback ;
			plugin_callback = (D3D9_CALLBACK_T) ::GetProcAddress( plugin_dll, "Init" );
			if ( plugin_callback )
			{
				g_init_callbacks.push_back( plugin_callback ) ;
			}

			plugin_callback = (D3D9_CALLBACK_T) ::GetProcAddress( plugin_dll, "Release" );
			if ( plugin_callback )
			{
				g_release_callbacks.push_back( plugin_callback ) ;
			}

			plugin_callback = (D3D9_CALLBACK_T) ::GetProcAddress( plugin_dll, "EndScene" );
			if ( plugin_callback )
			{
				g_endscene_callbacks.push_back( plugin_callback ) ;
			}

			plugin_callback = (D3D9_CALLBACK_T) ::GetProcAddress( plugin_dll, "Present" );
			if ( plugin_callback )
			{
				g_present_callbacks.push_back( plugin_callback ) ;
			}
		}
		else
		{
			::FreeLibrary( plugin_dll ) ;
		}
	}
}

void LoadPluginDlls(void)
{
	struct _finddata_t c_file;
	long hFile;
	string plugin_path ;

	if ( ( hFile = _findfirst("Plugins\\*.dll", &c_file) ) != -1L )
	{
		do
		{
			plugin_path = "Plugins\\" ;
			plugin_path.append( c_file.name ) ;
			RegisterPluginDLL( plugin_path.c_str() );
		} while ( _findnext(hFile, &c_file) == 0 ) ;

		_findclose( hFile ) ;
	}
}

void ExitInstance() 
{    
    OutputDebugString("PROXYDLL: ExitInstance called.\r\n");
	
	// Release the system's d3d9.dll
	if (gl_hOriginalDll)
	{
		::FreeLibrary(gl_hOriginalDll);
	    gl_hOriginalDll = NULL;  
	}

	g_init_callbacks.clear() ;
	g_release_callbacks.clear() ;
	g_endscene_callbacks.clear() ;
	g_present_callbacks.clear() ;

	vector<HINSTANCE>::const_iterator dll_iterator;
	for ( dll_iterator = g_plugin_dlls.begin(); dll_iterator != g_plugin_dlls.end(); dll_iterator++ )
	{
		::FreeLibrary( *dll_iterator ) ;
	}
	g_plugin_dlls.clear() ;
}

