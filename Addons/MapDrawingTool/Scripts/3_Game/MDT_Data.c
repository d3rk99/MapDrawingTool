// Shared data for the Map Drawing Tool.
// Approved player work reaches the game two ways:
//  - live: the server polls the web app's manifest (config.json "liveUrl") and relays it to players;
//    clients draw it on the full-screen map (strokes as lines, other layers as small downloaded images).
//    The full-screen map is drawn by the engine, not from karta_co.paa (tested, Test/README.md).
//    The paper map in hand gets the server's whole map texture ("paper"), downloaded the same way.
//    Personal work (only its author sees it) comes in the manifest's "personal" list; the server
//    sends each player the global work plus their own (MDT_MissionServer.c).
//  - baked: the build paints layers into karta_co.paa (boards, and the paper until "paper" arrives);
//    needs a Workshop update.
// Without a liveUrl the server falls back to $profile:MapDrawingTool\markers.json (markers only).
// Players with a map and a pen can also draw on the full-screen map; the server posts the drawing to
// the web app (MDT_RPC_SUBMIT, api/<mapId>/ingame) with config.json "serverKey".

const int MDT_RPC_MANIFEST = 0x4D445402;
const int MDT_RPC_SUBMIT = 0x4D445403;      // client -> server: an in-game drawing, in pieces
const int MDT_RPC_RESULT = 0x4D445404;      // server -> client: what the web app said about it
const int MDT_RPC_MINE_ASK = 0x4D445405;    // client -> server: list my submissions
const int MDT_RPC_MINE = 0x4D445406;        // server -> client: that list, in pieces
const int MDT_RPC_WITHDRAW = 0x4D445407;    // client -> server: remove one of mine (id)
const int MDT_RPC_PAPER_ASK = 0x4D445408;   // client -> server, on another player: what is on their map?
const int MDT_RPC_PAPER = 0x4D445409;       // server -> client, on that player: their paper texture
const int MDT_RPC_UPLOAD = 0x4D44540A;      // client -> server: a stamp PNG from the player's folder, in pieces
const int MDT_RPC_UPLOADED = 0x4D44540B;    // server -> client: its id at the web app (or why not)
const string MDT_STAMPS_DIR = "$profile:MapDrawingTool/stamps";   // players' own stamp PNGs (Documents\DayZ\...)
const int MDT_UPLOAD_MAX_WORDS = 24576;     // 96 KB, the web app's limit
const int MDT_RPC_CHUNK = 1000;             // characters per RPC; 4000 arrived "String CORRUPTED" (646 was fine)
const string MDT_PROFILE_DIR = "$profile:MapDrawingTool";
const string MDT_CONFIG_FILE = "$profile:MapDrawingTool\\config.json";
const string MDT_MARKERS_FILE = "$profile:MapDrawingTool\\markers.json";
const string MDT_CACHE_DIR = "$profile:MapDrawingTool/cache";

class MDT_Config
{
	string liveUrl = "";        // web app base URL, e.g. "http://map.example.com:8640/" (players download from it too)
	string mapId = "";          // the web app map this server shows; "" = the world name (one map per world)
	string serverKey = "";      // from the web app's Settings > Game servers; needed for in-game drawings
	int pollSeconds = 60;
}

class MDT_Marker
{
	float x;
	float z;
	string text;
	int color = 0xFFC00000; // ARGB
	int icon;               // MapMarkerTypes id
}

class MDT_Stroke
{
	int color;              // ARGB
	float width;            // metres
	ref array<float> points; // x, z, x, z, ...
}

class MDT_Layer
{
	string id;
	string kind;            // "strokes", "image" or "paper"
	ref array<float> rect;  // image: x0, z0, x1, z1 (metres)
	int bytes;
	int parts;
	string hash;
	int w;
	int h;
	string query;           // personal downloads: appended to the URL (a key only the owner gets)
	ref array<ref MDT_Stroke> strokes;
}

// What the owner allows on this map (web app Settings), for the in-game drawing mode.
class MDT_Options
{
	bool allowGlobal = true;
	bool allowPersonal;
	bool approvePersonal;
	bool ingame;
	ref array<int> lifespans;   // days; 0 = permanent
	int defaultDays;
}

// One player's personal work: only the game server sees all of these, each player gets their own.
class MDT_Personal
{
	string owner;           // SteamID64 (PlayerIdentity.GetPlainId)
	ref array<ref MDT_Marker> markers;
	ref array<ref MDT_Layer> layers;
	ref MDT_Layer paper;    // global + personal paper texture
}

class MDT_Manifest
{
	string mapId;           // web app map id: downloads are api/<mapId>/live/... ("map" is a reserved word)
	string world;
	string version;
	ref MDT_Options options;
	ref array<ref MDT_Marker> markers = new array<ref MDT_Marker>;
	ref array<ref MDT_Layer> layers = new array<ref MDT_Layer>;
	ref MDT_Layer paper;    // the whole map texture for the paper map in hand; none without layers
	ref array<ref MDT_Personal> personal;   // from the web app only; never sent to players
}

// markers.json (no live server) has the same "markers" list
class MDT_MarkerFile
{
	ref array<ref MDT_Marker> markers = new array<ref MDT_Marker>;
}

class MDT_SubmitText
{
	float x;
	float z;
	string text;
	float size;             // letter height, metres
	int color;              // ARGB
	string font;            // "plain", "bold", "hand", "marker", "comic"
	float rot;              // degrees, clockwise
}

class MDT_SubmitStamp
{
	float x;
	float z;
	string stamp;           // built-in name, or "upload:<id>" (the player's own PNG)
	float size;             // width, metres
	float rot;
	string file;            // client only: the player's PNG, until it is uploaded
}

// An in-game drawing. The client fills title, scope, days and what was drawn; the server adds who
// drew it and its key, then posts it to the web app.
class MDT_Submit
{
	string serverKey;
	string steamId;
	string name;
	string title;
	string scope;           // "global" or "personal"
	int days;
	ref array<ref MDT_Stroke> strokes = new array<ref MDT_Stroke>;
	ref array<ref MDT_SubmitText> texts = new array<ref MDT_SubmitText>;
	ref array<ref MDT_SubmitStamp> stamps = new array<ref MDT_SubmitStamp>;
	ref array<ref MDT_Marker> markers = new array<ref MDT_Marker>;
}

// A stamp PNG the player sends (MDT_RPC_UPLOAD): the file as little-endian ints, as it was read.
class MDT_Upload
{
	string serverKey;
	string steamId;
	string name;
	ref array<int> words = new array<int>;
}

class MDT_UploadResult
{
	bool ok;
	string error;
	string id;
}

// The web app's answer to api/<mapId>/ingame
class MDT_SubmitResult
{
	bool ok;
	string error;
	string status;          // "pending" or "approved"
	string scope;
}

// One of the player's own submissions (X on the map): where it is and, for line drawings, its lines.
class MDT_MineItem
{
	string id;
	string kind;            // "layer" or "marker"
	string title;
	string text;            // marker label
	string status;          // "pending", "approved" or "rejected"
	string scope;
	string expires;         // "" = permanent
	string created;
	ref array<float> rect;  // x0, z0, x1, z1 (metres)
	ref array<ref MDT_Stroke> strokes;
}

// Which online players have personal work, and its version (live/players)
class MDT_PlayersAsk
{
	string serverKey;
	ref array<string> players = new array<string>;
}

class MDT_Versions
{
	bool ok;
	string error;
	ref map<string, string> versions;
}

// One player's personal work (live/player)
class MDT_PersonalAnswer : MDT_Personal
{
	bool ok;
	string error;
	string version;
}

// What the game server asks the web app about one player (ingame/mine, ingame/withdraw)
class MDT_PlayerAsk
{
	string serverKey;
	string steamId;
	string id;
}

// The web app's answer to api/<mapId>/ingame/mine
class MDT_MineList
{
	bool ok;
	string error;
	ref array<ref MDT_MineItem> items;
}

// Hooks between modules: PlayerBase (4_World) receives the RPCs, MissionServer (5_Mission) handles them.
class MDT_Events
{
	// (Man player, PlayerIdentity sender, int index, int count, string text): a piece of an in-game drawing
	static ref ScriptInvoker SubmitPiece = new ScriptInvoker();
	// (Man player, PlayerIdentity sender): the player wants their list
	static ref ScriptInvoker MineAsk = new ScriptInvoker();
	// (Man player, PlayerIdentity sender, string id): the player removes one of theirs
	static ref ScriptInvoker Withdraw = new ScriptInvoker();
	// (Man holder, PlayerIdentity viewer): the viewer looks at the map the holder is holding
	static ref ScriptInvoker PaperAsk = new ScriptInvoker();
	// (Man player, PlayerIdentity sender, int index, int count, string text): a piece of a stamp PNG
	static ref ScriptInvoker UploadPiece = new ScriptInvoker();
}

// The player's own submissions as the server last sent them (client).
class MDT_Mine
{
	static ref MDT_MineList s_List;
	static int s_Received;              // bumps on every new list
	protected static ref array<string> s_Pieces;

	static void OnPiece(int index, int count, string text)
	{
		if (index == 0 || !s_Pieces)
			s_Pieces = new array<string>;
		s_Pieces.Insert(text);
		if (s_Pieces.Count() < count)
			return;
		string json;
		foreach (string piece : s_Pieces)
			json += piece;
		s_Pieces = null;
		MDT_MineList list;
		string error;
		if (!JsonFileLoader<MDT_MineList>.LoadData(json, list, error) || !list)
			return;
		s_List = list;
		s_Received++;
	}
}

// The drawing a player is making in game (client): lines, text, stamps and markers not sent yet.
// Kept here so it survives closing the map screen and the server's answer can clear it.
class MDT_Draft
{
	static ref array<ref MDT_Stroke> s_Strokes = new array<ref MDT_Stroke>;
	static ref array<ref MDT_SubmitText> s_Texts = new array<ref MDT_SubmitText>;
	static ref array<ref MDT_SubmitStamp> s_Stamps = new array<ref MDT_SubmitStamp>;
	static ref array<ref MDT_Marker> s_Markers = new array<ref MDT_Marker>;
	static ref array<int> s_Order = new array<int>;   // what was added, in order (for undo): 1 line 2 text 3 stamp 4 marker
	static int s_Version;               // bumps on every change (the map screen rebuilds its previews)
	static string s_Status;             // last message for the drawing bar
	static bool s_Sending;
	static bool s_Typing;               // a text box has the keyboard: hotkeys (and M) are off

	// The player's own stamp PNGs: file name -> id at the web app ("" while uploading).
	static ref map<string, string> s_Uploads = new map<string, string>;
	static ref array<string> s_UploadPieces = new array<string>;     // waiting to be sent, a few per frame
	static int s_UploadCount;

	static void Add(int kind)
	{
		s_Order.Insert(kind);
		s_Version++;
	}

	static bool IsEmpty()
	{
		return s_Strokes.Count() + s_Texts.Count() + s_Stamps.Count() + s_Markers.Count() == 0;
	}

	static void Undo()
	{
		if (s_Order.Count() == 0)
			return;
		int kind = s_Order[s_Order.Count() - 1];
		s_Order.Remove(s_Order.Count() - 1);
		if (kind == 1 && s_Strokes.Count() > 0)
			s_Strokes.Remove(s_Strokes.Count() - 1);
		else if (kind == 2 && s_Texts.Count() > 0)
			s_Texts.Remove(s_Texts.Count() - 1);
		else if (kind == 3 && s_Stamps.Count() > 0)
			s_Stamps.Remove(s_Stamps.Count() - 1);
		else if (kind == 4 && s_Markers.Count() > 0)
			s_Markers.Remove(s_Markers.Count() - 1);
		s_Version++;
	}

	static void Clear()
	{
		s_Strokes.Clear();
		s_Texts.Clear();
		s_Stamps.Clear();
		s_Markers.Clear();
		s_Order.Clear();
		s_Version++;
	}

	static void OnResult(bool ok, string message)
	{
		if (ok && s_Sending)
			Clear();                                // the drawing was sent (not a removal)
		s_Sending = false;
		s_Status = message;
		string title = "Map drawing";
		NotificationSystem.AddNotificationExtended(5, title, message, "set:dayz_gui image:icon_pin");
	}

	// The web app has the player's stamp PNG (or refused it).
	static void OnUploaded(string name, bool ok, string id, string error)
	{
		if (ok)
		{
			s_Uploads.Set(name, id);
			s_Status = "Stamp " + name + " ready";
		}
		else
		{
			s_Uploads.Remove(name);
			s_Status = "Stamp " + name + ": " + error;
		}
	}

	// Sends a few queued upload pieces per frame (a stamp is up to a few hundred).
	static void Update()
	{
		if (s_UploadPieces.Count() == 0)
			return;
		Man player = GetGame().GetPlayer();
		if (!player)
			return;
		for (int n = 0; n < 8 && s_UploadPieces.Count() > 0; n++)
		{
			int index = s_UploadCount - s_UploadPieces.Count();
			ScriptRPC rpc = new ScriptRPC();
			rpc.Write(index);
			rpc.Write(s_UploadCount);
			rpc.Write(s_UploadPieces[0]);
			rpc.Send(player, MDT_RPC_UPLOAD, true, null);
			s_UploadPieces.Remove(0);
		}
	}

	// Reads a PNG from the player's stamps folder and queues it for upload. FileSerializer reads
	// 4 bytes at a time, so up to 3 bytes at the end may be lost: the web app restores the PNG's
	// fixed IEND chunk. Logs whether the file looked like a PNG (first 4 bytes 89 50 4E 47).
	static bool Upload(string name)
	{
		string have;
		if (s_Uploads.Find(name, have))
			return true;                            // sent already, or on its way
		if (s_UploadPieces.Count() > 0)
		{
			s_Status = "Wait for the other stamp to finish uploading";
			return false;
		}
		FileSerializer f = new FileSerializer();
		if (!f.Open(MDT_STAMPS_DIR + "/" + name, FileMode.READ))
		{
			s_Status = "Cannot open " + name;
			return false;
		}
		MDT_Upload up = new MDT_Upload();
		up.name = name;
		int w;
		while (up.words.Count() <= MDT_UPLOAD_MAX_WORDS && f.Read(w))
			up.words.Insert(w);
		f.Close();
		bool png = up.words.Count() > 0 && up.words[0] == 0x474E5089;
		Print(string.Format("[MapDrawingTool] stamp %1: read %2 words, PNG signature %3", name, up.words.Count(), png));
		if (!png)
		{
			s_Status = name + " is not a PNG (or could not be read)";
			return false;
		}
		if (up.words.Count() > MDT_UPLOAD_MAX_WORDS)
		{
			s_Status = name + " is too big (stamps can be up to 96 KB)";
			return false;
		}
		string json, error;
		JsonFileLoader<MDT_Upload>.MakeData(up, json, error, false);
		int len = json.Length();
		s_UploadCount = Math.Max(1, (len + MDT_RPC_CHUNK - 1) / MDT_RPC_CHUNK);
		for (int i = 0; i < s_UploadCount; i++)
			s_UploadPieces.Insert(json.Substring(i * MDT_RPC_CHUNK, Math.Min(MDT_RPC_CHUNK, len - i * MDT_RPC_CHUNK)));
		s_Uploads.Set(name, "");
		s_Status = "Uploading stamp " + name + "...";
		return true;
	}

	// The PNGs in the player's stamps folder (made on first use, so players can find it).
	static array<string> OwnStamps()
	{
		array<string> list = new array<string>;
		if (!FileExist(MDT_PROFILE_DIR))
			MakeDirectory(MDT_PROFILE_DIR);
		if (!FileExist(MDT_STAMPS_DIR))
			MakeDirectory(MDT_STAMPS_DIR);
		string name;
		FileAttr attr;
		FindFileHandle h = FindFile(MDT_STAMPS_DIR + "/*.png", name, attr, FindFileFlags.DIRECTORIES);
		if (!h)
			return list;
		bool more = true;
		while (more)
		{
			if (name != "")
				list.Insert(name);
			more = FindNextFile(h, name, attr);
		}
		CloseFindFile(h);
		return list;
	}
}
