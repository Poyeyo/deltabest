#define WIN32_LEAN_AND_MEAN		
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include "d3dx9.h"
#include "d3d9.h"

#include "rFplugin.hpp"          // corresponding header file

// plugin information
unsigned g_uPluginID          = 0;
char     g_szPluginName[]     = "rFactorDeltaBestPlugin";
unsigned g_uPluginVersion     = 001;
unsigned g_uPluginObjectCount = 1;
RenderPluginInfo g_PluginInfo;

// interface to plugin information
extern "C" __declspec(dllexport)
const char* __cdecl GetPluginName() { return g_szPluginName; }

extern "C" __declspec(dllexport)
unsigned __cdecl GetPluginVersion() { return g_uPluginVersion; }

extern "C" __declspec(dllexport)
unsigned __cdecl GetPluginObjectCount() { return g_uPluginObjectCount; }

// get the plugin-info object used to create the plugin.
extern "C" __declspec(dllexport)
PluginObjectInfo* __cdecl GetPluginObjectInfo( const unsigned uIndex )
{
	switch(uIndex)
	{
	case 0:
		return  &g_PluginInfo;
	default:
		return 0;
	}
}


bool in_realtime = false;              /* Are we in cockpit? As opposed to monitor */
bool session_started = false;          /* Is a Practice/Race/Q session started or are we in spectator mode, f.ex.? */
bool lap_was_timed = false;            /* If current/last lap that ended was timed or not */
bool green_flag = false;               /* Is the race in green flag condition? */
bool key_switch = true;                /* Enabled/disabled state by keyboard action */
bool displayed_welcome = false;        /* Whether we displayed the "plugin enabled" welcome message */
bool loaded_best_in_session = false;   /* Did we already load the best lap in this session? */
bool shown_best_in_session = false;    /* Did we show a message for the best lap restored from file? */
bool player_in_pits = false;           /* Is the player currently in the pits? */
unsigned int prev_pos = 0;             /* Meters around the track of the current lap (previous interval) */
unsigned int last_pos = 0;             /* Meters around the track of the current lap */
unsigned int scoring_ticks = 0;        /* Advances every time UpdateScoring() is called */
unsigned int laps_since_realtime = 0;  /* Number of laps completed since entering realtime last time */
double current_delta_best = NULL;      /* Current calculated delta best time */
double prev_delta_best = NULL;
double prev_lap_dist = 0;              /* Used to accurately calculate dt and */
double previous_elapsed_time = 0;      /* elpased time of last interval */
double previous_velocity = 0;          /* vehicle velocity of last interval */
double previous_delta_speed = 0;
double inbtw_scoring_traveled = 0;     /* Distance traveled (m) between successive UpdateScoring() calls */
double inbtw_scoring_elapsed = 0;
long render_ticks = 0;
long render_ticks_int = 2;
char userappdata[FILENAME_MAX];
// char datapath[FILENAME_MAX] = "";
char bestlap_dir[FILENAME_MAX] = "";
char bestlap_filename[FILENAME_MAX] = "";

/* Keeps information about last and best laps */

struct LapInterval {
	int sector;
	double elapsed;
	double speed;
};

struct LapTime {
	double final;
	double started;
	double ended;
	double interval_offset;
	// double elapsed[MAX_TRACK_LENGTH];
	LapInterval intervals[MAX_TRACK_LENGTH];
} best_lap, previous_lap, last_lap, optimal_lap, spy_lap;

struct PluginConfig {

	bool bar_enabled;
	unsigned int bar_left;
	unsigned int bar_top;
	unsigned int bar_width;
	unsigned int bar_height;
	unsigned int bar_gutter;

	bool time_enabled;
	bool hires_updates;
	unsigned int time_top;
	unsigned int time_width;
	unsigned int time_height;
	unsigned int time_font_size;
	char time_font_name[FONT_NAME_MAXLEN];

	bool info_enabled;
	unsigned int info_left;
	unsigned int info_top;
	unsigned int info_width;
	unsigned int info_height;
	unsigned int info_timeout;

	unsigned int keyboard_magic;
	unsigned int keyboard_reset;
} config;

#ifdef ENABLE_LOG
FILE* out_file = NULL;
#endif

// DirectX 9 objects, to render some text on screen
LPD3DXFONT g_Font = NULL;
D3DXFONT_DESC FontDesc = {
	DEFAULT_FONT_SIZE, 0, 400, 0, false, DEFAULT_CHARSET,
	OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_PITCH, DEFAULT_FONT_NAME
};
RECT FontPosition, ShadowPosition;
LPD3DXSPRITE bar = NULL;
LPDIRECT3DTEXTURE9 texture = NULL;

D3DDEVICE_CREATION_PARAMETERS cparams;
RECT size_rect;

float SCREEN_WIDTH    = 1920;
float SCREEN_HEIGHT   = 1080;
float SCREEN_CENTER   = SCREEN_WIDTH / 2.0;


/* Simple style: negative delta = green, positive delta = red */
D3DCOLOR TextColor(double delta)
{
	D3DCOLOR text_color = 0xE0000000;      /* Alpha (transparency) value */
	bool is_negative = delta < 0;
	double cutoff_val = 0.20;
	double abs_val = abs(delta);

	text_color |= is_negative
		? (COLOR_INTENSITY << 8)
		: (COLOR_INTENSITY << 16);

	/* Blend red or green with white when closer to zero */
	if (abs_val <= cutoff_val) {
		unsigned int col_val = int(COLOR_INTENSITY * (1 / cutoff_val) * (cutoff_val - abs_val));
		if (is_negative)
			text_color |= (col_val << 16) + col_val;
		else
			text_color |= (col_val << 8) + col_val;
	}

	return text_color;
}

D3DCOLOR BarColor(double delta, double delta_diff)
{
	static const D3DCOLOR ALPHA = 0xE0000000;
	bool is_gaining = delta_diff > 0;
	D3DCOLOR bar_color = ALPHA;
	bar_color |= is_gaining ? (COLOR_INTENSITY << 16) : (COLOR_INTENSITY << 8);

	double abs_val = abs(delta_diff);
	double cutoff_val = 2.5;

	if (abs_val <= cutoff_val) {
		unsigned int col_val = int(COLOR_INTENSITY * (1 / cutoff_val) * (cutoff_val - abs_val));
		if (is_gaining)
			bar_color |= (col_val << 8) + col_val;
		else
			bar_color |= (col_val << 16) + col_val;
	}

	return bar_color;
}

// Signal that this plugin wants to get notifications from the D3D9 proxy DLL
extern "C" __declspec(dllexport)
bool __cdecl WantsD3D9Updates( void )
{
	return true ;
}

extern "C" __declspec(dllexport)
void __cdecl Init( IDirect3DDevice9 *d3d )
{
#ifdef ENABLE_LOG
	WriteLog("---INIT DX9---");
#endif /* ENABLE_LOG */

	d3d->GetCreationParameters(&cparams);
	GetClientRect(cparams.hFocusWindow, &size_rect);

	SCREEN_WIDTH    = size_rect.right;
	SCREEN_HEIGHT   = size_rect.bottom;
	SCREEN_CENTER   = SCREEN_WIDTH / 2.0;

	LoadConfig(config, CONFIG_FILE, SCREEN_WIDTH, SCREEN_HEIGHT);

	/* Now we know screen X/Y, we can place the text somewhere specific (in height).
	If everything is zero then apply our defaults. */

	if (config.time_width == 0)
		config.time_width = SCREEN_WIDTH;
	if (config.time_height == 0)
		config.time_height = SCREEN_HEIGHT;
	if (config.time_top == 0)
		config.time_top = SCREEN_HEIGHT / 6.0;

	//config.bar_left = GetPrivateProfileInt("Bar", "Left", 0, ini_file);
	//config.bar_top = GetPrivateProfileInt("Bar", "Top", 0, ini_file);
	//config.time_top = GetPrivateProfileInt("Time", "Top", 0, ini_file);

	FontDesc.Height = config.time_font_size;
	sprintf(FontDesc.FaceName, config.time_font_name);

	D3DXCreateFontIndirect((LPDIRECT3DDEVICE9) d3d, &FontDesc, &g_Font);
	assert(g_Font != NULL);

	D3DXCreateTextureFromFile((LPDIRECT3DDEVICE9) d3d, TEXTURE_BACKGROUND, &texture);
	D3DXCreateSprite((LPDIRECT3DDEVICE9) d3d, &bar);

	assert(texture != NULL);
	assert(bar != NULL);
}


// Callback triggered just before the backbuffer is flipped to the front. Original function name: Present()
// Note that this is outside the BeginScene/EndScene block, but it seems to work.
// If you find issues, rename this function to 'EndScene'
extern "C" __declspec(dllexport)
void __cdecl Present( IDirect3DDevice9 *d3d )
{
	double delta;
	double diff;

	RenderPlugin *plugin = RenderPlugin::TheInstance() ;
	if ( plugin )
	{
		delta = plugin->CalculateDeltaBest();
		diff = plugin->CalculateDeltaSpeed();

		HRESULT dxStatus = d3d->TestCooperativeLevel();

		if (dxStatus == D3DERR_DEVICELOST)
		{
			key_switch = false;
			return;
		}

		if (dxStatus == D3DERR_DEVICENOTRESET)
		{
			key_switch = false;
			return;
		}

		if (dxStatus == D3DERR_DRIVERINTERNALERROR)
		{
			key_switch = false;
			return;
		}

		// Are we in the car? Is it time to draw a Delta Bar?
		if ( plugin->CanDisplayBar() )
		{

			const float BAR_WIDTH       = config.bar_width;
			const float BAR_TOP         = config.bar_top;
			const float BAR_HEIGHT      = config.bar_height;
			const float BAR_TIME_GUTTER = config.bar_gutter;
			const float TIME_WIDTH      = config.time_width;
			const float TIME_HEIGHT     = config.time_height;

			D3DXVECTOR3 delta_pos;
			RECT delta_size = { 0, 0, 0, BAR_HEIGHT - 2 };

			// Provide a default centered position in case user
			// disabled drawing of the bar
			delta_pos.x = SCREEN_WIDTH / 2.0;
			delta_pos.y = BAR_TOP + 1;
			delta_pos.z = 0;
			delta_size.right = 1;

			// Computed positions, sizes
			const float BAR_LEFT        = (SCREEN_WIDTH - BAR_WIDTH) / 2.0;
			// The -5 "compensates" for font height vs time box height difference
			const float TIME_TOP        = (BAR_TOP + BAR_HEIGHT + BAR_TIME_GUTTER);
			const D3DCOLOR BAR_COLOR    = D3DCOLOR_RGBA(0x50, 0x50, 0x50, 0xCC);

			D3DCOLOR bar_grey = BAR_COLOR;

			// if (g_Font == NULL)
			// 	return;
			// if (bar == NULL)
			// 	return;

			if(dxStatus == D3D_OK)
			{

				bar->Begin(D3DXSPRITE_ALPHABLEND);

				if (config.bar_enabled) {

					D3DXVECTOR3 bar_pos;
					bar_pos.x = BAR_LEFT;
					bar_pos.y = BAR_TOP;
					bar_pos.z = 0;

					RECT bar_rect = { 0, 0, BAR_WIDTH, BAR_HEIGHT };

					// Draw delta bar
					D3DCOLOR delta_bar_color;

					delta_bar_color = BarColor(delta, diff);
					delta_pos.x = SCREEN_CENTER;

					// Delta is negative: colored bar is in the right-hand half.
					if (delta < 0) {
						delta_size.right = (BAR_WIDTH / 2.0) * (-delta / 2.0);
					}

					// Delta non-negative, colored bar is in the left-hand half
					else if (delta > 0) {
						delta_pos.x -= (BAR_WIDTH / 2.0) * (delta / 2.0);
						delta_size.right = SCREEN_CENTER - delta_pos.x;
					}

					// Don't allow positive (green) bar to start before the -2.0s position
					delta_pos.x = max(delta_pos.x, SCREEN_CENTER - (BAR_WIDTH / 2.0));

					// Max width is always half of bar width (left or right half)
					delta_size.right = min(delta_size.right, BAR_WIDTH / 2.0);

					// Min width is 1, as zero doesn't make sense to draw
					if (delta_size.right < 1)
						delta_size.right = 1;

					if (plugin->CanDisplayData())
					{
#ifdef ENABLE_LOG
						// WriteLog("[DRAW] colored-bar at (%.2f, %.2f) width: %.2f height: %.2f\n", delta_pos.x, delta_pos.y, delta_size.right, delta_size.bottom);
#endif /* ENABLE_LOG */

						bar->Draw(texture, &delta_size, NULL, &delta_pos, delta_bar_color);
					}

#ifdef ENABLE_LOG
					// WriteLog("[DRAW] bar at (%.2f, %.2f) width: %.2f height: %.2f\n", bar_pos.x, bar_pos.y, bar_rect.right, bar_rect.bottom);
#endif /* ENABLE_LOG */

					bar->Draw(texture, &bar_rect,  NULL, &bar_pos,  bar_grey);

				}

				// Draw the time text ("-0.18")
				if (config.time_enabled) {

					D3DCOLOR shadowColor = 0xC0333333;
					D3DCOLOR textColor = TextColor(delta);

					char lp_deltaBest[15] = "";

					float time_rect_center = delta < 0
						? (delta_pos.x + delta_size.right)
						: delta_pos.x;
					float left_edge = (SCREEN_WIDTH - BAR_WIDTH) / 2.0;
					float right_edge = (SCREEN_WIDTH + BAR_WIDTH) / 2.0;
					if (time_rect_center <= left_edge)
						time_rect_center = left_edge + 1;
					else if (time_rect_center >= right_edge)
						time_rect_center = right_edge - 1;

					RECT time_rect = { 0, 0, TIME_WIDTH, TIME_HEIGHT };
					D3DXVECTOR3 time_pos;
					time_pos.x = time_rect_center - TIME_WIDTH / 2.0;
					time_pos.y = TIME_TOP;
					time_pos.z = 0;
					time_rect.right = TIME_WIDTH;

#ifdef ENABLE_LOG
					// WriteLog("[DRAW] delta-box at (%.2f, %.2f) width: %d height: %d value: %.2f\n", time_pos.x, time_pos.y, time_rect.right, time_rect.bottom, delta);
#endif /* ENABLE_LOG */

					if (plugin->CanDisplayData())
					{
						FontPosition.left = time_pos.x;
						FontPosition.top = time_pos.y - 5;   // To vertically align text and box
						FontPosition.right = FontPosition.left + TIME_WIDTH;
						FontPosition.bottom = FontPosition.top + TIME_HEIGHT + 5;

						ShadowPosition.left = FontPosition.left + 2.5;
						ShadowPosition.top = FontPosition.top + 2.5;
						ShadowPosition.right = FontPosition.right;
						ShadowPosition.bottom = FontPosition.bottom;

						bar->Draw(texture, &time_rect, NULL, &time_pos, bar_grey);

						sprintf(lp_deltaBest, "%+2.2f", delta);
						g_Font->DrawText(NULL, (LPCSTR)lp_deltaBest, -1, &FontPosition,   DT_CENTER, textColor);
						g_Font->DrawText(NULL, (LPCSTR)lp_deltaBest, -1, &ShadowPosition, DT_CENTER, shadowColor);
					}
				}

				bar->End();
			}
		}
	}
}

extern "C" __declspec(dllexport)
void __cdecl Release( IDirect3DDevice9 *d3d )
{
#ifdef ENABLE_LOG
	WriteLog("---Release DX9---");
#endif /* ENABLE_LOG */
}

void WriteLog(const char * const format, ...)
{
#ifdef ENABLE_LOG
	if (out_file == NULL)
		out_file = fopen(LOG_FILE, "a");

	if (out_file != NULL)
	{
		char buffer[2048];
		va_list args;
		va_start (args, format);
		vsnprintf (buffer, sizeof(buffer), format, args);
		fprintf(out_file, "%s\n", buffer);
		va_end (args);
	}
#endif /* ENABLE_LOG */
}




// RenderPluginInfo class

RenderPluginInfo::RenderPluginInfo()
{
  // put together a name for this plugin
  sprintf( m_szFullName, "%s - %s", g_szPluginName, RenderPluginInfo::GetName() );
}

const char*    RenderPluginInfo::GetName()     const { return RenderPlugin::GetName(); }
const char*    RenderPluginInfo::GetFullName() const { return m_szFullName; }
const char*    RenderPluginInfo::GetDesc()     const { return "An implementation of delta best display plugin"; }
const unsigned RenderPluginInfo::GetType()     const { return RenderPlugin::GetType(); }
const char*    RenderPluginInfo::GetSubType()  const { return RenderPlugin::GetSubType(); }
const unsigned RenderPluginInfo::GetVersion()  const { return RenderPlugin::GetVersion(); }
void*          RenderPluginInfo::Create()      const { return new RenderPlugin(); }


// RenderPlugin class

const char RenderPlugin::m_szName[] = "rFactorDeltaBestPlugin";
const char RenderPlugin::m_szSubType[] = "Internals";
const unsigned RenderPlugin::m_uID = 1;
const unsigned RenderPlugin::m_uVersion = 3;  // set to 3 for InternalsPluginV3 functionality and added graphical and vehicle info
RenderPlugin *RenderPlugin::ms_the_instance = NULL ;

PluginObjectInfo *RenderPlugin::GetInfo()
{
  return &g_PluginInfo;
}


RenderPlugin::RenderPlugin()
{
	ms_the_instance = this ;
	m_in_real_time = false ;
	m_engine_rpm = 0.0 ;
}

RenderPlugin::~RenderPlugin()
{
	if ( this == ms_the_instance )
	{
		ms_the_instance = NULL ;
	}
}


void RenderPlugin::Startup()
{
	// default HW control enabled to true
	mEnabled = true;
#ifdef ENABLE_LOG
	if (out_file == NULL)
		out_file = fopen(LOG_FILE, "w");

	WriteLog("--STARTUP--");
#endif /* ENABLE_LOG */
}


void RenderPlugin::Shutdown()
{
}


void RenderPlugin::StartSession()
{
#ifdef ENABLE_LOG
	WriteLog("--STARTSESSION--");
#endif /* ENABLE_LOG */
	session_started = true;
	loaded_best_in_session = false;
	shown_best_in_session = false;
	lap_was_timed = false;
	player_in_pits = false;
	ResetLap(&last_lap);
	ResetLap(&best_lap);
	ResetLap(&previous_lap);
	ResetLap(&optimal_lap);
}


void RenderPlugin::EndSession()
{
	mET = 0.0f;
	session_started = false;
#ifdef ENABLE_LOG
	WriteLog("--ENDSESSION--");
	if (out_file) {
		fclose(out_file);
		out_file = NULL;
	}
#endif /* ENABLE_LOG */
}

void RenderPlugin::Load()
{
#ifdef ENABLE_LOG
	WriteLog("--LOAD--");
#endif /* ENABLE_LOG */
}

void LoadConfig(struct PluginConfig &config, const char *ini_file, float SCREEN_WIDTH, float SCREEN_HEIGHT)
{

	// #define DEFAULT_INFO_WIDTH       80
	// #define DEFAULT_INFO_HEIGHT      40
	// #define DEFAULT_INFO_TOP         430
	// #define DEFAULT_INFO_LEFT        30

	float default_bar_width       = SCREEN_WIDTH * 0.30208333333333333333333333333333;
	float default_bar_height      = SCREEN_HEIGHT * 0.01851851851851851851851851851852;
	float default_bar_top         = SCREEN_HEIGHT * 0.12037037037037037037037037037037;
	float default_bar_time_gutter = 5;
	float default_time_width      = SCREEN_WIDTH * 0.06666666666666666666666666666667;
	float default_time_height     = SCREEN_HEIGHT * 0.03240740740740740740740740740741;

	float default_font_size       = SCREEN_HEIGHT * 0.04444444444444444444444444444444;

	// [Bar] section
	config.bar_left = GetPrivateProfileInt("Bar", "Left", 0, ini_file);
	config.bar_top = GetPrivateProfileInt("Bar", "Top", default_bar_top, ini_file);
	config.bar_width = GetPrivateProfileInt("Bar", "Width", default_bar_width, ini_file);
	config.bar_height = GetPrivateProfileInt("Bar", "Height", default_bar_height, ini_file);
	config.bar_gutter = GetPrivateProfileInt("Bar", "Gutter", default_bar_time_gutter, ini_file);
	config.bar_enabled = GetPrivateProfileInt("Bar", "Enabled", 1, ini_file) == 1 ? true : false;

	// [Time] section
	config.time_top = GetPrivateProfileInt("Time", "Top", 0, ini_file);
	config.time_width = GetPrivateProfileInt("Time", "Width", default_time_width, ini_file);
	config.time_height = GetPrivateProfileInt("Time", "Height", default_time_height, ini_file);
	config.time_font_size = GetPrivateProfileInt("Time", "FontSize", default_font_size, ini_file);
	config.time_enabled = GetPrivateProfileInt("Time", "Enabled", 1, ini_file) == 1 ? true : false;
	config.hires_updates = GetPrivateProfileInt("Time", "HiresUpdates", DEFAULT_HIRES_UPDATES, ini_file) == 1 ? true : false;
	GetPrivateProfileString("Time", "FontName", DEFAULT_FONT_NAME, config.time_font_name, FONT_NAME_MAXLEN, ini_file);

	// [Keyboard] section
	config.keyboard_magic = GetPrivateProfileInt("Keyboard", "MagicKey", DEFAULT_MAGIC_KEY, ini_file);
	config.keyboard_reset = GetPrivateProfileInt("Keyboard", "ResetKey", DEFAULT_RESET_KEY, ini_file);
}

void RenderPlugin::Unload()
{
	if (g_Font != NULL) {
		g_Font->Release();
		g_Font = NULL;
	}
	if (bar != NULL) {
		bar->Release();
		bar = NULL;
	}
	if (texture != NULL) {
		texture->Release();
		texture = NULL;
	}
#ifdef ENABLE_LOG
	WriteLog("--UNLOAD--");
#endif /* ENABLE_LOG */
}


void RenderPlugin::EnterRealtime()
{
	// start up timer every time we enter realtime
	mET = 0.0f;
	in_realtime = true;
	laps_since_realtime = 0;

#ifdef ENABLE_LOG
	WriteLog("---ENTERREALTIME---");
#endif /* ENABLE_LOG */
}

void RenderPlugin::ExitRealtime()
{
	in_realtime = false;

	/* Reset delta best state */
	last_pos = 0;
	prev_lap_dist = 0;
	current_delta_best = 0;
	prev_delta_best = 0;

#ifdef ENABLE_LOG
	WriteLog("---EXITREALTIME---");
#endif /* ENABLE_LOG */
}

void RenderPlugin::ResetLap(struct LapTime *lap)
{
	if (lap == NULL)
		return;

	lap->ended = 0;
	lap->final = 0;
	lap->started = 0;
	lap->interval_offset = 0;

	unsigned int i = 0, n = MAX_TRACK_LENGTH;
	for (i = 0; i < n; i++)
	{
		lap->intervals[i].sector = 0;
		lap->intervals[i].elapsed = 0.0;
		lap->intervals[i].speed = 0.0;
		// lap->elapsed[i] = 0;
	}
}

bool RenderPlugin::CanDisplayBar()
{
	// If we're in the monitor or replay, or no session has started yet,
	// no delta best should be displayed
	if (! in_realtime)
		return false;

	// Option might be disabled by the user (TAB)
	if (! key_switch)
		return false;

	return true;
}

bool RenderPlugin::CanDisplayData()
{
	// If we're in the monitor or replay, or no session has started yet,
	// no delta best should be displayed
	if (! in_realtime)
		return false;

	// Option might be disabled by the user (TAB)
	if (! key_switch)
		return false;

	// If we are in any race/practice phase that's not
	// green flag, we don't need or want Delta Best displayed
	if (! green_flag)
		return false;

	if (player_in_pits)
		return false;

	// Don't display anything if current lap isn't timed
	if (! lap_was_timed)
		return false;

	// We can't display a delta best until we have a best lap recorded
	if (! best_lap.final)
		return false;

	return true;
}

void RenderPlugin::UpdateScoring(const ScoringInfoV2 &info)
{

	/* No scoring updates should take place if we're
	in the monitor as opposed to the cockpit mode */
	if (!in_realtime || !session_started)
		return;

	/* Toggle shortcut key. Turns off/on the display of delta time */
	if (KEY_DOWN(config.keyboard_magic))
		key_switch = ! key_switch;

	/* Reset the best lap time to none for the session */
	else if (KEY_DOWN(config.keyboard_reset)) {
		ResetLap(&previous_lap);
		ResetLap(&last_lap);
		ResetLap(&best_lap);
		ResetLap(&optimal_lap);
	}

	/* Update plugin context information, used by NeedToDisplay() */
	green_flag = ((info.mGamePhase == GP_GREEN_FLAG)
		       || (info.mGamePhase == GP_YELLOW_FLAG)
		       || (info.mGamePhase == GP_SESSION_OVER));

	for (long i = 0; i < info.mNumVehicles; ++i) {
		VehicleScoringInfoV2 &vinfo = info.mVehicle[i];

		// Player's car? If not, skip
		if ((!vinfo.mIsPlayer) || /*(vinfo.mControl != 0) ||*/ (vinfo.mInPits))
			continue;

		player_in_pits = vinfo.mInPits;

#ifdef ENABLE_LOG
		WriteLog("mLapStartET=%.3f mLastLapTime=%.3f mCurrentET=%.3f Elapsed=%.3f mLapDist=%.3f/%.3f prevLapDist=%.3f prevCurrentET=%.3f deltaBest=%+2.2f lastPos=%d prevPos=%d, speed=%.3f",
			vinfo.mLapStartET,
			vinfo.mLastLapTime,
			info.mCurrentET,
			(info.mCurrentET - vinfo.mLapStartET),
			vinfo.mLapDist,
			info.mLapDist,
			prev_lap_dist,
			previous_elapsed_time,
			current_delta_best,
			last_pos,
			prev_pos,
			magnitude(vinfo.mLocalVel));
#endif /* ENABLE_LOG */

		if (! loaded_best_in_session) {
#ifdef ENABLE_LOG
			WriteLog("Trying to load best lap for this session");
#endif
			LoadBestLap(&best_lap, info, vinfo);
			loaded_best_in_session = true;
		}

		/* Check if we started a new lap just now */
		bool new_lap = (vinfo.mLapStartET != last_lap.started);
		double curr_lap_dist = vinfo.mLapDist >= 0 ? vinfo.mLapDist : 0;
		double time_interval = (info.mCurrentET - previous_elapsed_time);
		double velocity_delta = (magnitude(vinfo.mLocalVel) - previous_velocity);

		if (new_lap) {

			/* mLastLapTime is -1 when lap wasn't timed */
			lap_was_timed = ! (vinfo.mLapStartET == 0.0 && vinfo.mLastLapTime == 0.0);

			if (lap_was_timed) {
				last_lap.final = vinfo.mLastLapTime;
				last_lap.ended = info.mCurrentET;

#ifdef ENABLE_LOG
				WriteLog("New LAP: Last = %.3f, started = %.3f, ended = %.3f interval_offset = %.3f",
					last_lap.final, last_lap.started, last_lap.ended, last_lap.interval_offset);
#endif /* ENABLE_LOG */

				/* Was it the best lap so far? */
				/* .final == -1.0 is the first lap of the session, can't be timed */
				bool valid_timed_lap = last_lap.final > 0.0;
				bool best_so_far = valid_timed_lap && (
						(best_lap.final == NULL)
					 || (best_lap.final != NULL && last_lap.final < best_lap.final));

				if (best_so_far) {
#ifdef ENABLE_LOG
					WriteLog("Last lap was the best so far (final time = %.3f, previous best = %.3f)", last_lap.final, best_lap.final);
#endif /* ENABLE_LOG */

					/**
					 * Complete the mileage of the last lap.
					 * This avoids nasty jumps into empty space (+50.xx) when later comparing with best lap.
					 */
					unsigned int meters = info.mLapDist;
					for (unsigned int i = last_pos + 1 ; i <= (unsigned int) info.mLapDist; i++) {

						// Elapsed time at this position already filled in by UpdateTelemetry()?
						// if (last_lap.intervals[i].elapsed > 0.0)
						// 	continue;

						// Linear interpolation of elapsed time in relation to physical position
						double interval_fraction = meters == last_pos ? 1.0 : (1.0 * i - last_pos) / (1.0 * info.mLapDist - last_pos);
						last_lap.intervals[i].elapsed = previous_elapsed_time + (interval_fraction * time_interval) - vinfo.mLapStartET;
						last_lap.intervals[i].speed = previous_velocity + (interval_fraction * velocity_delta);
						last_lap.intervals[i].sector = vinfo.mSector;
#ifdef ENABLE_LOG
						WriteLog("[DELTAFILL]  elapsed[%d] = %.3f,  speed[%d] = %.3f  (interval_fraction=%.3f)", i, last_lap.intervals[i].elapsed, i, last_lap.intervals[i].speed, interval_fraction);
#endif /* ENABLE_LOG */
					}

					ResetLap(&best_lap);
					best_lap = last_lap;
					SaveBestLap(&best_lap, info, vinfo);
				}

#ifdef ENABLE_LOG
				WriteLog("Best LAP yet  = %.3f, started = %.3f, ended = %.3f",
					best_lap.final, best_lap.started, best_lap.ended);
#endif /* ENABLE_LOG */
			}

			/* Prepare to archive the new lap */
			ResetLap(&last_lap);
			last_lap.started = vinfo.mLapStartET;
			last_lap.final = NULL;
			last_lap.ended = NULL;
			last_lap.interval_offset = info.mCurrentET - vinfo.mLapStartET;
			// last_lap.elapsed[0] = 0;
			last_pos = prev_pos = 0;
			prev_lap_dist = 0;
			// Leave previous_elapsed_time alone, or you have hyper-jumps
			previous_elapsed_time = info.mCurrentET;
		}

		/* If there's a lap in progress, save the delta updates */
		if (last_lap.started > 0.0) {
			unsigned int meters = round(vinfo.mLapDist >= 0 ? vinfo.mLapDist : 0);

			/* It could be that we have stopped our vehicle.
			In that case (same array position), we want to
			overwrite the previous value anyway */
			if (meters >= last_pos) {
				double distance_traveled = (vinfo.mLapDist - prev_lap_dist);
				if (distance_traveled < 0)
					distance_traveled = 0;

				if (meters == last_pos) {
					// last_lap.elapsed[meters] = info.mCurrentET - vinfo.mLapStartET;
#ifdef ENABLE_LOG
					WriteLog("[DELTA]     elapsed[%d] = %.3f [same position]", meters, last_lap.intervals[meters].elapsed);
#endif /* ENABLE_LOG */
				} else {
#ifdef ENABLE_LOG
					WriteLog("[DELTA]  previous_elapsed_time[%d] = %.3f,  time_interval = %.3f, speed = %.3f ", meters, previous_elapsed_time, time_interval, magnitude(vinfo.mLocalVel));
#endif /* ENABLE_LOG */
					for (unsigned int i = last_pos; i < meters; i++) {
						/* Elapsed time at this position already filled in by UpdateTelemetry()? */
						if (last_lap.intervals[i].elapsed > 0.0)
							continue;
						/* Linear interpolation of elapsed time in relation to physical position */
						double interval_fraction = meters == last_pos ? 1.0 : (1.0 * i - last_pos) / (1.0 * meters - last_pos);
						/* Linear interpolation of local velocity in relation to physical position */
						// interval_fraction = meters == last_pos ? 1.0 : (1.0 * i - last_pos) / (1.0 * meters - last_pos);
						last_lap.intervals[i].elapsed = /*last_lap.elapsed[i] =*/ previous_elapsed_time + (interval_fraction * time_interval) - vinfo.mLapStartET;
						last_lap.intervals[i].speed = previous_velocity + (interval_fraction * velocity_delta);
						last_lap.intervals[i].sector = vinfo.mSector;
#ifdef ENABLE_LOG
						WriteLog("[DELTAFILL]  elapsed[%d] = %.3f,  speed[%d] = %.3f  (interval_fraction=%.3f)", i, last_lap.intervals[i].elapsed, i, last_lap.intervals[i].speed, interval_fraction);
#endif /* ENABLE_LOG */
					}
					// last_lap.elapsed[meters] = info.mCurrentET - vinfo.mLapStartET;
					last_lap.intervals[meters].elapsed = info.mCurrentET - vinfo.mLapStartET;
					last_lap.intervals[meters].speed = magnitude(vinfo.mLocalVel);
					last_lap.intervals[meters].sector = vinfo.mSector;
				}

#ifdef ENABLE_LOG
				WriteLog("[DELTA] distance_traveled=%.3f time_interval=%.3f [%d .. %d]",
					distance_traveled, time_interval, last_pos, meters);
#endif /* ENABLE_LOG */
			}

			prev_pos = last_pos;
			last_pos = meters;
		}

		if (curr_lap_dist > prev_lap_dist)
			prev_lap_dist = curr_lap_dist;

		previous_elapsed_time = info.mCurrentET;
		previous_velocity = magnitude(vinfo.mLocalVel);

		inbtw_scoring_traveled = 0;
		inbtw_scoring_elapsed = 0;
	}

}


/* We use UpdateTelemetry() to gain notable precision in position updates.
We assume that LocalVelocity is the speed of the
vehicle, which seems to be confirmed by observed data.

Having forward speed means that with a delta-t we can directly measure
the distance traveled at 15hz instead of 2hz of UpdateScoring().

We use this data to complete information on vehicle lap progress
between successive UpdateScoring() calls.

This behaviour can be disabled by the "HiresUpdates=0" option
in the ini file.

*/
void RenderPlugin::UpdateTelemetry( const TelemInfoV2 &info )
{
	if (! in_realtime)
		return;

	if (! config.hires_updates)
		return;

	// Have 3D position, lap time and speed, but no lap distance in meters
	double dt = info.mDeltaTime;
	double forward_speed = magnitude(info.mLocalVel);

	/* Ignore movement in reverse gear
	   Causes crashes down the line but don't know why :-| */
	if (forward_speed <= 0)
		return;

	double distance = forward_speed * dt;

	inbtw_scoring_traveled += distance;
	inbtw_scoring_elapsed  += dt;

	unsigned int inbtw_pos = round(last_pos + inbtw_scoring_traveled);
	if (inbtw_pos > last_pos) {
		last_lap.intervals[inbtw_pos].elapsed = last_lap.intervals[last_pos].elapsed + inbtw_scoring_elapsed;
		last_lap.intervals[inbtw_pos].speed = forward_speed;
#ifdef ENABLE_LOG
		WriteLog("\tNEW inbtw pos=%d elapsed=%.3f (last_pos=%d, t=%.3f, acc_t=%.3f)\n", inbtw_pos, inbtw_scoring_elapsed, last_pos, last_lap.intervals[last_pos].elapsed, last_lap.intervals[inbtw_pos].elapsed);
#endif /* ENABLE_LOG */
	}

#ifdef ENABLE_LOG
	WriteLog("\tdt=%.3f fwd_speed=%.3f dist=%.3f inbtw_scoring_traveled=%.3f last_pos(m)=%d\n", dt, forward_speed, distance, inbtw_scoring_traveled, last_pos);
#endif /* ENABLE_LOG */
}


bool RenderPlugin::RequestCommentary( CommentaryRequestInfo &info )
{
	// COMMENT OUT TO ENABLE EXAMPLE
	return( false );

	// only if enabled, of course
	if( !mEnabled )
		return( false );

	// Note: function is called twice per second

	strcpy( info.mName, "PlayerLaptime" );
	info.mInput1 = 0.0f;
	info.mInput2 = 0.0f;
	info.mInput3 = 0.0f;
	info.mSkipChecks = true;
	mEnabled = false;
	return( true );
}


double RenderPlugin::CalculateDeltaBest()
{
	// Shouldn't really happen
	if (! best_lap.final)
		return 0;

	// Current position in meters around the track
	int m = round(last_pos + inbtw_scoring_traveled);

	// By using meters, and backfilling all the missing information, the program can be reasonably close to the position in the best lap
	double last_time_at_pos = last_lap.intervals[m].elapsed;
	double best_time_at_pos = best_lap.intervals[m].elapsed;
	double delta_best = last_time_at_pos - best_time_at_pos;

	if (delta_best > 99.0)
		delta_best = 99.0;
	else if (delta_best < -99)
		delta_best = -99.0;

	return delta_best;
}

double RenderPlugin::CalculateDeltaSpeed()
{
	// Shouldn't really happen
	if (! best_lap.final)
		return 0;

	// Current position in meters around the track
	int m = round(last_pos + inbtw_scoring_traveled);

	double last_speed_at_pos = last_lap.intervals[m].speed;
	double best_speed_at_pos = best_lap.intervals[m].speed;
	double delta_speed = best_speed_at_pos - last_speed_at_pos;

	return delta_speed;
}

bool RenderPlugin::SaveBestLap(const struct LapTime *lap, const ScoringInfoV2 &scoring, const VehicleScoringInfoV2 &veh)
{

#ifdef ENABLE_LOG
	WriteLog("[SAVE] Saving best lap of %.2f", lap->final);
#endif /* ENABLE_LOG */

	/* Get file name for the best lap */
	const char *szBestLapFile = GetBestLapFileName(scoring, veh);
	if (szBestLapFile == NULL) {
		return false;
	}

	FILE* fBestLap = fopen(szBestLapFile, "w");
	if (fBestLap) {
		//fprintf(fBestLap, "[Elapsed]\n");

		fprintf(fBestLap, "[LapTime] %f|%f|%f|%f\n", lap->started, lap->ended, lap->interval_offset, lap->final);

		unsigned int i = 0, n = MAX_TRACK_LENGTH;
		for (i = 0; i < n; i++) {
			/* Occasionally, first few meters of the track
			   could set elapsed to 0.0, or even negative. */

			// Don't store meters with no speed data
			if (lap->intervals[i].speed <= 0.0) {
				break;
			}

			fprintf(fBestLap, "%d=S%d|%f|%f\n", i, lap->intervals[i].sector, lap->intervals[i].elapsed, lap->intervals[i].speed);
		}
		fclose(fBestLap);
#ifdef ENABLE_LOG
		WriteLog("[SAVE] Write to file completed '%s'", szBestLapFile);
#endif /* ENABLE_LOG */
		return true;
	}

	else {
#ifdef ENABLE_LOG
		WriteLog("[SAVE] Couldn't save to file '%s'", szBestLapFile);
#endif /* ENABLE_LOG */
		return false;
	}

}

void RenderPlugin::LoadBestLap(struct LapTime *lap, const ScoringInfoV2 &scoring, const VehicleScoringInfoV2 &veh)
{
#ifdef ENABLE_LOG
	WriteLog("[LOAD] Loading best lap");
#endif /* ENABLE_LOG */

	/* Get file name for the best lap */
	const char *szBestLapFile = GetBestLapFileName(scoring, veh);
	if (szBestLapFile == NULL) {
		return;
	}

	int sector;
	double started;
	double ended;
	double interval_offset;
	double final;
	double elapsed;
	double speed;

	FILE* fBestLap = fopen(szBestLapFile, "r");
	if (fBestLap) {

		ResetLap(lap);

		fscanf(fBestLap, "[LapTime] %lf|%lf|%lf|%lf\n", &started, &ended, &interval_offset, &final);

		lap->started = started;
		lap->ended = ended;
		lap->interval_offset = interval_offset;
		lap->final = final;

		int i = 0;
		while (! feof(fBestLap)) {
			unsigned int meters = -1;
			double elapsed = 0.0;
			// fscanf(fBestLap, "%u=%lf\n", &meters, &elapsed);
			fscanf(fBestLap, "%d=S%d|%lf|%lf\n", &i, &sector, &elapsed, &speed);

			lap->intervals[i].sector = sector;
			lap->intervals[i].elapsed = elapsed;
			lap->intervals[i].speed = speed;

#ifdef ENABLE_LOG
			WriteLog("[LOAD]   read value from file %d=S%d|%f|%f", i, sector, elapsed, speed);
#endif /* ENABLE_LOG */
		}

		fclose(fBestLap);

#ifdef ENABLE_LOG
		WriteLog("[LOAD] Load from file completed");
#endif /* ENABLE_LOG */
	}

	else {
#ifdef ENABLE_LOG
		WriteLog("[LOAD] No file to load or couldn't load from '%s'", szBestLapFile);
#endif /* ENABLE_LOG */
	}
}

const char * RenderPlugin::GetBestLapFileName(const ScoringInfoV2 &scoring, const VehicleScoringInfoV2 &veh)
{
	sprintf(bestlap_dir, BEST_LAP_DIR, GetUserDataPath());

	DWORD ftyp = GetFileAttributesA(bestlap_dir);
	if (ftyp == INVALID_FILE_ATTRIBUTES)
	{
		CreateDirectory(bestlap_dir, NULL);
	}

	sprintf(bestlap_filename, BEST_LAP_FILE, bestlap_dir, scoring.mTrackName, veh.mVehicleClass);
	return bestlap_filename;
}

const char * RenderPlugin::GetUserDataPath()
{
	char* APPDATA = getenv("APPDATA");

	sprintf(userappdata, "%s\\%s", APPDATA, DATA_PATH_FILE);

	DWORD ftyp = GetFileAttributesA(userappdata);
	if (ftyp == INVALID_FILE_ATTRIBUTES)
	{
		CreateDirectory (userappdata, NULL);
	}

	return userappdata;
}
