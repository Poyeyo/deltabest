#ifndef RENDERPLUGIN_HPP
#define RENDERPLUGIN_HPP

#include "InternalsPlugin.hpp"

#include "InternalsPlugin.hpp"
#include <assert.h>
#include <math.h>               /* for rand() */
#include <stdio.h>              /* for sample output */
#include <d3dx9.h>              /* DirectX9 main header */
#include <cmath>

#define PLUGIN_NAME             "rF Delta Best"
#define DELTA_BEST_VERSION      "v1/Sila"

#undef ENABLE_LOG              /* To enable file logging */

#define LOG_FILE                "Plugins\\DeltaBest.log"
#define CONFIG_FILE             "Plugins\\DeltaBest.ini"
#define TEXTURE_BACKGROUND      "Plugins\\DeltaBestBackground.png"

/* Maximum length of a track in meters */
#define MAX_TRACK_LENGTH        100000

#define DATA_PATH_FILE          "DeltaBest"
#define BEST_LAP_DIR            "%s\\Laps"
#define BEST_LAP_FILE           "%s\\%s_%s.lap"

/* Game phases -> info.mGamePhase */
#define GP_GREEN_FLAG           5
#define GP_YELLOW_FLAG          6
#define GP_SESSION_OVER         8

#define COLOR_INTENSITY         0xF0

#define DEFAULT_FONT_SIZE       48
#define DEFAULT_FONT_NAME       "Arial Black"

#define INFO_FONT_SIZE       24
#define INFO_FONT_NAME       "Arial Black"

#define DEFAULT_BAR_WIDTH       0 //580
#define DEFAULT_BAR_HEIGHT      0 //20
#define DEFAULT_BAR_TOP         0 //130
#define DEFAULT_BAR_TIME_GUTTER 0 //5

#define DEFAULT_INFO_WIDTH       80
#define DEFAULT_INFO_HEIGHT      40
#define DEFAULT_INFO_TOP         430
#define DEFAULT_INFO_LEFT        30

#define DEFAULT_TIME_WIDTH      0 //128
#define DEFAULT_TIME_HEIGHT     35

/* Whether to use UpdateTelemetry() to achieve a better precision and
   faster updates to the delta time instead of every 0.2s that
   UpdateScoring() allows */
#define DEFAULT_HIRES_UPDATES   0


/* Toggle plugin with CTRL + a magic key. Reference:
http://msdn.microsoft.com/en-us/library/windows/desktop/dd375731%28v=vs.85%29.aspx */
#define DEFAULT_MAGIC_KEY       (0x44)      /* "D" */
#define DEFAULT_RESET_KEY       (0x5A)      /* "Z" */
#define KEY_DOWN(k)             ((GetAsyncKeyState(k) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000))

#define FONT_NAME_MAXLEN 32

#ifdef ENABLE_LOG
	void WriteLog(const char * const msg, ...);
#endif /* ENABLE_LOG */

void LoadConfig(struct PluginConfig &config, const char *ini_file, float SCREEN_WIDTH, float SCREEN_HEIGHT);

D3DCOLOR TextColor(double delta);
D3DCOLOR BarColor(double delta, double delta_diff);

float inline magnitude(TelemVect3 vect)
{
	return sqrt((vect.x * vect.x) + (vect.y * vect.y) + (vect.z * vect.z));
}

// This is used for app to find out information about the plugin
class RenderPluginInfo : public PluginObjectInfo
{
public:

	// Constructor/destructor
	RenderPluginInfo();
	~RenderPluginInfo() {}

	// Derived from base class PluginObjectInfo
	virtual const char*    GetName()     const;
	virtual const char*    GetFullName() const;
	virtual const char*    GetDesc()     const;
	virtual const unsigned GetType()     const;
	virtual const char*    GetSubType()  const;
	virtual const unsigned GetVersion()  const;
	virtual void*          Create()      const;

private:

	char m_szFullName[128];
};


// This is used for the app to use the plugin for its intended purpose
class RenderPlugin : public InternalsPluginV3
{
protected:

	const static char m_szName[];
	const static char m_szSubType[];
	const static unsigned m_uID;
	const static unsigned m_uVersion;

	bool m_in_real_time ;
	static RenderPlugin *ms_the_instance ;
	float m_engine_rpm ; // factor of 0 to 1 (1=rev limiter)

public:

	// Constructor/destructor
	RenderPlugin();
	~RenderPlugin();

	static RenderPlugin *TheInstance(void)              { return ms_the_instance ; }

	// Called from class InternalsPluginInfo to return specific information about plugin
	static const char *   GetName()                     { return m_szName; }
	static const unsigned GetType()                     { return PO_INTERNALS; }
	static const char *   GetSubType()                  { return m_szSubType; }
	static const unsigned GetVersion()                  { return m_uVersion; }

	// Derived from base class PluginObject
	void                  Destroy()                     { Shutdown(); }  // poorly named ... doesn't destroy anything
	PluginObjectInfo *    GetInfo();
	unsigned              GetPropertyCount() const      { return 0; }
	PluginObjectProperty *GetProperty( const char * )   { return 0; }
	PluginObjectProperty *GetProperty( const unsigned ) { return 0; }

	// These are the functions derived from base class InternalsPlugin
	// that can be implemented.
	void Startup();         // game startup
	void Shutdown();        // game shutdown

	void StartSession();    // session has started
	void EndSession();      // session has ended

	// WHAT THIS PLUGIN WANTS
	virtual bool WantsScoringUpdates() { return( true ); }
	virtual void UpdateScoring( const ScoringInfoV2 &info );

	virtual bool WantsTelemetryUpdates() { return( false ); }
	virtual void UpdateTelemetry( const TelemInfoV2 &info );

	virtual bool RequestCommentary( CommentaryRequestInfo &info );

	void Load();                   // when a new track/car is loaded
	void Unload();                 // back to the selection screen

	// REAL-TIME DETECTION (when in-car)
	virtual void EnterRealtime();       // entering realtime (where the vehicle can be driven)
	virtual void ExitRealtime();        // exiting realtime

	// bool inRealTime(void) const       { return in_realtime ; }

	double CalculateDeltaBest();
	double CalculateDeltaSpeed();
	bool CanDisplayBar();
	bool CanDisplayData();
	void ResetLap(struct LapTime *lap);

private:

	// void DrawDeltaBar(const ScreenInfoV01 &info, double delta, double delta_diff);
	const char * GetUserDataPath();
	const char * GetBestLapFileName(const ScoringInfoV2 &scoring, const VehicleScoringInfoV2 &veh);
	void LoadBestLap(struct LapTime *lap, const ScoringInfoV2 &scoring, const VehicleScoringInfoV2 &veh);
	bool SaveBestLap(const struct LapTime *lap, const ScoringInfoV2 &scoring, const VehicleScoringInfoV2 &veh);

	//
	// Current status
	//

	float mET;                          /* needed for the hardware example */
	bool mEnabled;                      /* needed for the hardware example */

};


#endif

