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

// An in-game drawing. The client fills title, scope, days and strokes; the server adds who drew it
// and its key, then posts it to the web app.
class MDT_Submit
{
	string serverKey;
	string steamId;
	string name;
	string title;
	string scope;           // "global" or "personal"
	int days;
	ref array<ref MDT_Stroke> strokes = new array<ref MDT_Stroke>;
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

// The drawing a player is making in game (client). Kept here so it survives closing the map screen
// and the server's answer can clear it.
class MDT_Draft
{
	static ref array<ref MDT_Stroke> s_Strokes = new array<ref MDT_Stroke>;
	static string s_Status;             // last message for the drawing bar
	static bool s_Sending;

	static void OnResult(bool ok, string message)
	{
		if (ok && s_Sending)
			s_Strokes.Clear();                      // the drawing was sent (not a removal)
		s_Sending = false;
		s_Status = message;
		string title = "Map drawing";
		NotificationSystem.AddNotificationExtended(5, title, message, "set:dayz_gui image:icon_pin");
	}
}
