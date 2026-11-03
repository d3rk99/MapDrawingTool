// Shared data for the Map Drawing Tool.
// Approved player work reaches the game two ways:
//  - live: the server polls the web app's manifest (config.json "liveUrl") and relays it to players;
//    clients draw it on the full-screen map (strokes as lines, other layers as small downloaded images).
//    The full-screen map is drawn by the engine, not from karta_co.paa (tested, Test/README.md).
//    The paper map in hand gets the server's whole map texture ("paper"), downloaded the same way.
//  - baked: the build paints layers into karta_co.paa (boards, and the paper until "paper" arrives);
//    needs a Workshop update.
// Without a liveUrl the server falls back to $profile:MapDrawingTool\markers.json (markers only).

const int MDT_RPC_MANIFEST = 0x4D445402;
const int MDT_RPC_CHUNK = 1000;             // manifest characters per RPC; 4000 arrived "String CORRUPTED" (646 was fine)
const string MDT_PROFILE_DIR = "$profile:MapDrawingTool";
const string MDT_CONFIG_FILE = "$profile:MapDrawingTool\\config.json";
const string MDT_MARKERS_FILE = "$profile:MapDrawingTool\\markers.json";
const string MDT_CACHE_DIR = "$profile:MapDrawingTool/cache";

class MDT_Config
{
	string liveUrl = "";        // web app base URL, e.g. "http://map.example.com:8640/" (players download from it too)
	string mapId = "";          // the web app map this server shows; "" = the world name (one map per world)
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
	ref array<ref MDT_Stroke> strokes;
}

class MDT_Manifest
{
	string mapId;           // web app map id: downloads are api/<mapId>/live/... ("map" is a reserved word)
	string world;
	string version;
	ref array<ref MDT_Marker> markers = new array<ref MDT_Marker>;
	ref array<ref MDT_Layer> layers = new array<ref MDT_Layer>;
	ref MDT_Layer paper;    // the whole map texture for the paper map in hand; none without layers
}

// markers.json (no live server) has the same "markers" list
class MDT_MarkerFile
{
	ref array<ref MDT_Marker> markers = new array<ref MDT_Marker>;
}
