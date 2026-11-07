// RestApi access and callback lifetimes, for the client and the server.
//
// The engine holds only a raw pointer to a RestCallback: if script drops its last ref while the
// request is still in flight, the answer calls into freed memory and the game crashes with an
// access violation and no script error. OnError "may be called multiple times" (restapi.c), and a
// timeout is not known to cancel the native request, so an answered callback may still be called
// again. Every callback is therefore kept here while it waits, and for KEEP_S after its first
// answer; one that never answers is never released.
// 2026-11-06: a player's client crashed on every join while downloading the 79-part paper texture
// over a slow link; a retry used to release the previous part's callback (MDT_Live.s_Callback).
class MDT_Rest
{
	static const int CONNECT_S = 10;            // engine limits: 3..120 s
	static const int READ_S = 30;
	static const float KEEP_S = 300;            // after the first answer

	protected static ref array<ref RestCallback> s_Waiting = new array<ref RestCallback>;
	protected static ref array<ref RestCallback> s_Answered = new array<ref RestCallback>;
	protected static ref array<float> s_AnsweredAt = new array<float>;
	protected static bool s_Options;

	static RestApi Api()
	{
		RestApi api = GetRestApi();
		if (!api)
			api = CreateRestApi();
		if (api && !s_Options)
		{
			s_Options = true;
			api.SetOption(ERestOption.ERESTOPTION_CONNECTION, CONNECT_S);
			api.SetOption(ERestOption.ERESTOPTION_READOPERATION, READ_S);
			Print(string.Format("[MapDrawingTool] web requests: connect timeout %1 s, read timeout %2 s", CONNECT_S, READ_S));
		}
		return api;
	}

	// Call just before GET/POST with this callback.
	static void Keep(RestCallback cb)
	{
		if (s_Waiting.Find(cb) < 0)
			s_Waiting.Insert(cb);
	}

	// Call from each OnSuccess/OnError/OnTimeout. True the first time only: later calls are repeats
	// for an answered request and are to be ignored.
	static bool Answer(RestCallback cb)
	{
		Sweep();
		int i = s_Waiting.Find(cb);
		if (i < 0)
			return false;
		s_Waiting.Remove(i);
		s_Answered.Insert(cb);
		s_AnsweredAt.Insert(GetGame().GetTickTime());
		return true;
	}

	protected static void Sweep()
	{
		float now = GetGame().GetTickTime();
		while (s_Answered.Count() > 0 && (now - s_AnsweredAt[0] > KEEP_S || now < s_AnsweredAt[0]))
		{
			s_Answered.RemoveOrdered(0);
			s_AnsweredAt.RemoveOrdered(0);
		}
	}
}

// Client-side log that survives a crash: each line is opened, written and closed (the script log
// is buffered and loses its tail). $profile:MapDrawingTool/progress.log, started over each game.
class MDT_Trace
{
	static bool s_On = true;
	protected static bool s_Started;

	static void Line(string text)
	{
		if (!s_On || GetGame().IsDedicatedServer())
			return;
		if (!FileExist(MDT_PROFILE_DIR))
			MakeDirectory(MDT_PROFILE_DIR);
		FileMode mode = FileMode.APPEND;
		if (!s_Started)
			mode = FileMode.WRITE;
		s_Started = true;
		FileHandle fh = OpenFile(MDT_PROFILE_DIR + "/progress.log", mode);
		if (!fh)
			return;
		int y, mo, d, h, mi, s;
		GetYearMonthDay(y, mo, d);
		GetHourMinuteSecond(h, mi, s);
		FPrintln(fh, string.Format("%1-%2-%3 %4:%5:%6 %7", y, mo.ToStringLen(2), d.ToStringLen(2), h.ToStringLen(2), mi.ToStringLen(2), s.ToStringLen(2), text));
		CloseFile(fh);
	}
}

// Client settings: $profile:MapDrawingTool/client.json (%LOCALAPPDATA%\DayZ\MapDrawingTool).
class MDT_ClientConfig
{
	bool paper = true;      // false: never download the server's paper map texture (the vanilla map stays)
	bool progressLog = true;

	static ref MDT_ClientConfig s_Config;
	static const string BUSY_FILE = "$profile:MapDrawingTool/paper.busy";   // exists while a paper texture downloads

	static MDT_ClientConfig Get()
	{
		if (s_Config)
			return s_Config;
		string file = MDT_PROFILE_DIR + "/client.json";
		string error;
		if (!FileExist(MDT_PROFILE_DIR))
			MakeDirectory(MDT_PROFILE_DIR);
		// (a field added later loads as false from an older file: add new fields as "off" switches)
		if (!FileExist(file) || !JsonFileLoader<MDT_ClientConfig>.LoadFile(file, s_Config, error) || !s_Config)
			s_Config = new MDT_ClientConfig();
		MDT_Trace.s_On = s_Config.progressLog;
		// Crash guard: the marker is only left behind when the game ended during a paper download
		// without quitting normally (MDT_Live.OnQuit). Then this player gets no paper texture any
		// more, so a download that crashes their game does it once (2026-11-06, not reproduced).
		if (FileExist(BUSY_FILE))
		{
			DeleteFile(BUSY_FILE);
			if (s_Config.paper)
			{
				s_Config.paper = false;
				string why = "paper texture turned off: the game closed during its download last time. To try again, set \"paper\": 1 in %LOCALAPPDATA%\\DayZ\\MapDrawingTool\\client.json";
				Print("[MapDrawingTool] " + why);
				MDT_Trace.Line(why);
			}
		}
		JsonFileLoader<MDT_ClientConfig>.SaveFile(file, s_Config, error);   // shows the fields
		return s_Config;
	}
}
