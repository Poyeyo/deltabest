// proxydll.h
#pragma once

// Exported function
IDirect3D9* WINAPI Direct3DCreate9 (UINT SDKVersion);

// regular functions
void InitInstance(HANDLE hModule);
void ExitInstance(void);
void LoadOriginalDll(void);
void LoadPluginDlls(void);

typedef bool (__cdecl *BOOL_FN_T)( void );
typedef void (__cdecl *D3D9_CALLBACK_T)( IDirect3DDevice9 * );

extern std::vector<D3D9_CALLBACK_T>	g_init_callbacks ;
extern std::vector<D3D9_CALLBACK_T>	g_release_callbacks ;
extern std::vector<D3D9_CALLBACK_T>	g_endscene_callbacks ;
extern std::vector<D3D9_CALLBACK_T>	g_present_callbacks ;

