// Answers three questions, logging each as "[MDT-TEST]" (script log) and on the map screen:
//  1. Does the HTTP client (RestApi) work on a client, and on the server?
//  2. Can an image downloaded into $profile be loaded by an ImageWidget?
//  3. Does the map screen use karta_co.paa? (the test karta has a big red X; look for it)
// It also draws the downloaded image and a canvas polyline pinned to Chernogorsk on the map screen.

const string MDTT_URL = "http://127.0.0.1:8640/";
const string MDTT_FILE = "static/test/overlay.paa";
const string MDTT_DIR = "$profile:MDTTest";

class MDTTest
{
	static ref array<string> s_Results = new array<string>;
	static string s_ImagePath;      // the path variant that loaded, if any
	static ref MDTTestCallback s_GetCb;
	static ref array<ref MDTTestCallback> s_FileCbs = new array<ref MDTTestCallback>;

	static void Log(string line)
	{
		string side = "server";
		if (GetGame().IsClient() || !GetGame().IsMultiplayer())
			side = "client";
		line = side + ": " + line;
		s_Results.Insert(line);
		Print("[MDT-TEST] " + line);
	}

	static void Run()
	{
		if (!FileExist(MDTT_DIR))
			MakeDirectory(MDTT_DIR);
		RestApi api = GetRestApi();
		if (!api)
			api = CreateRestApi();
		if (!api)
		{
			Log("1 FAIL: no RestApi");
			return;
		}
		Log("1 RestApi created");
		RestContext ctx = api.GetRestContext(MDTT_URL);
		s_GetCb = new MDTTestCallback("GET");
		ctx.GET(s_GetCb, "api/worlds");
		// Server run 1+2: FILE reports success but writes 0 bytes for every path tried; kept once for the client.
		MDTTestCallback cb = new MDTTestCallback("FILE");
		cb.m_Target = MDTT_DIR + "/overlay_file.paa";
		s_FileCbs.Insert(cb);
		ctx.FILE(cb, MDTT_FILE, cb.m_Target);
		// The fallback: GET the PAA as hex text, decode, write raw bytes with FileSerializer.
		MDTTestCallback hex = new MDTTestCallback("HEX");
		hex.m_Target = MDTT_DIR + "/overlay.paa";
		s_FileCbs.Insert(hex);
		ctx.GET(hex, MDTT_FILE + ".hex");
	}

	// FileSerializer.Write(int) writes exactly 4 little-endian bytes, no header (server run 2: "ABCDEFGH").
	static bool WriteHex(string hex, string path)
	{
		int started = TickCount(0);
		string digits = "0123456789abcdef";
		int bytes = hex.Length() / 2;
		FileSerializer s = new FileSerializer();
		if (!s.Open(path, FileMode.WRITE))
			return false;
		int word = 0;
		int shift = 0;
		for (int i = 0; i < bytes; i++)
		{
			int b = digits.IndexOf(hex.Get(i * 2)) * 16 + digits.IndexOf(hex.Get(i * 2 + 1));
			word = word | (b << shift);
			shift += 8;
			if (shift == 32)
			{
				s.Write(word);
				word = 0;
				shift = 0;
			}
		}
		if (shift > 0)
			s.Write(word);      // pads the last word with zero bytes
		s.Close();
		Log(string.Format("3 wrote %1 bytes from hex, %2 ticks", bytes, TickCount(started)));
		return true;
	}

	// Called on the client once the file is there; tries the path spellings an ImageWidget might accept.
	static void TryLoadImage(string path)
	{
		{
			Widget w = GetGame().GetWorkspace().CreateWidget(ImageWidgetTypeID, 0, 0, 1, 1, 0, ARGB(255, 255, 255, 255), 0, null);
			ImageWidget img = ImageWidget.Cast(w);
			if (!img)
			{
				Log("2 FAIL: could not create ImageWidget");
				return;
			}
			bool ok = img.LoadImageFile(0, path, true);
			int sx, sy;
			img.GetImageSize(0, sx, sy);
			Log(string.Format("2 LoadImageFile(%1) returned %2, size %3x%4", path, ok, sx, sy));
			img.Unlink();
			if (ok && sx > 0 && s_ImagePath == "")
				s_ImagePath = path;
		}
	}
}

class MDTTestCallback : RestCallback
{
	string m_Kind;
	string m_Target;

	void MDTTestCallback(string kind)
	{
		m_Kind = kind;
	}

	override void OnError(int errorCode)
	{
		MDTTest.Log("1 " + m_Kind + " FAIL error " + errorCode);
	}

	override void OnTimeout()
	{
		MDTTest.Log("1 " + m_Kind + " FAIL timeout");
	}

	override void OnSuccess(string data, int dataSize)
	{
		MDTTest.Log("1 " + m_Kind + " OK, " + dataSize + " bytes (string length " + data.Length() + "): " + data.Substring(0, Math.Min(60, data.Length())));
		if (m_Kind != "HEX")
			return;
		if (MDTTest.WriteHex(data, m_Target) && (GetGame().IsClient() || !GetGame().IsMultiplayer()))
			MDTTest.TryLoadImage(m_Target);
	}

	override void OnFileCreated(string fileName, int dataSize)
	{
		bool exists = FileExist(m_Target);
		MDTTest.Log("1 " + m_Kind + ": " + dataSize + " bytes, reported " + fileName + ", FileExist=" + exists);
		if (exists && (GetGame().IsClient() || !GetGame().IsMultiplayer()))
			MDTTest.TryLoadImage(m_Target);
	}
}

// With use3DMap (test mission cfggameplay.json) unfolding a map only shows the paper in hand.
// M then toggles the full-screen map with the test overlay. Read as a raw key: vanilla only
// handles M with ignoreMapOwnership, and the map screen excludes the "map" input group.
modded class MissionGameplay
{
	override void OnInit()
	{
		super.OnInit();
		MDTTest.Run();
	}

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);
		if (!KeyState(KeyCode.KC_M))
			return;
		ClearKey(KeyCode.KC_M);

		UIManager um = GetGame().GetUIManager();
		if (um.IsMenuOpen(MENU_MAP))
		{
			MapMenu open = MapMenu.Cast(um.FindMenu(MENU_MAP));
			if (open)
				open.MDTT_CloseOverlay();
			return;
		}
		PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
		if (!player || !player.IsMapOpen() || um.GetMenu())
			return;
		ItemMap paper = ItemMap.Cast(player.GetItemInHands());
		UIScriptedMenu menu = um.EnterScriptedMenu(MENU_MAP, null);
		if (menu && paper)
		{
			menu.InitMapItem(paper);
			menu.LoadMapMarkers();
		}
		// as vanilla does for the 2D map: no walking, looking or actions while the screen is up
		AddActiveInputExcludes({"map"});
		AddActiveInputRestriction(EInputRestrictors.MAP);
	}
}

modded class MissionServer
{
	override void OnInit()
	{
		super.OnInit();
		MDTTest.Run();
	}

	// every test character gets a paper map, to check the in-hand texture too
	override void InvokeOnConnect(PlayerBase player, PlayerIdentity identity)
	{
		super.InvokeOnConnect(player, identity);
		if (player)
			player.GetInventory().CreateInInventory("ChernarusMap");
	}
}

// Map screen: the overlay layout lives inside the Map widget, like vanilla's GPS marker
// (as siblings of the map's window they were drawn underneath it). MDTFixed is a reference
// copy of the downloaded image at a fixed spot, to tell "cannot render" from "wrong place".
modded class MapMenu
{
	protected Widget m_MDTT_Root;
	protected ImageWidget m_MDTT_Image;
	protected CanvasWidget m_MDTT_Canvas;

	override Widget Init()
	{
		Widget root = super.Init();
		if (!m_MapWidgetInstance)
			return root;
		m_MDTT_Root = GetGame().GetWorkspace().CreateWidgets("MapDrawingToolTest/gui/mdt_overlay.layout", m_MapWidgetInstance);
		if (!m_MDTT_Root)
		{
			Print("[MDT-TEST] overlay layout FAILED to load");
			return root;
		}
		m_MDTT_Canvas = CanvasWidget.Cast(m_MDTT_Root.FindAnyWidget("MDTCanvas"));
		m_MDTT_Image = ImageWidget.Cast(m_MDTT_Root.FindAnyWidget("MDTImage"));
		ImageWidget fixedImg = ImageWidget.Cast(m_MDTT_Root.FindAnyWidget("MDTFixed"));
		TextWidget text = TextWidget.Cast(m_MDTT_Root.FindAnyWidget("MDTText"));
		bool loaded;
		if (MDTTest.s_ImagePath != "")
		{
			loaded = m_MDTT_Image.LoadImageFile(0, MDTTest.s_ImagePath, true);
			fixedImg.LoadImageFile(0, MDTTest.s_ImagePath, true);
			m_MDTT_Image.SetImage(0);
			fixedImg.SetImage(0);
		}
		string lines = "MDT TEST (M closes)\n";
		foreach (string line : MDTTest.s_Results)
			lines += line + "\n";
		text.SetText(lines);
		Print("[MDT-TEST] map screen opened; overlay inside Map widget, canvas=" + (m_MDTT_Canvas != null) + " image loaded=" + loaded);
		return root;
	}

	// Leaves the paper map unfolded in hand; vanilla CloseMapMenu tells the server the map is closing.
	void MDTT_CloseOverlay()
	{
		if (m_Hud)
		{
			m_Hud.ShowHudUI(true);
			m_Hud.ShowQuickbarUI(true);
		}
		GetGame().GetMission().RemoveActiveInputExcludes({"map"});
		GetGame().GetMission().RemoveActiveInputRestriction(EInputRestrictors.MAP);
		Close();
	}

	// Esc on the map screen: only close the screen, keep the paper unfolded
	override void CloseMapMenu()
	{
		if (CfgGameplayHandler.GetUse3DMap())
			MDTT_CloseOverlay();
		else
			super.CloseMapMenu();
	}

	override void Update(float timeslice)
	{
		super.Update(timeslice);
		if (!m_MapWidgetInstance || !m_MDTT_Root)
			return;
		// MapToScreen gives screen pixels; children of the Map widget are placed relative to it
		float mx, my;
		m_MapWidgetInstance.GetScreenPos(mx, my);
		vector tl = m_MapWidgetInstance.MapToScreen(Vector(5800, 0, 3800));
		vector br = m_MapWidgetInstance.MapToScreen(Vector(7800, 0, 1800));
		m_MDTT_Image.SetPos(tl[0] - mx, tl[1] - my);
		m_MDTT_Image.SetSize(br[0] - tl[0], br[1] - tl[1]);

		// magenta triangle Chernogorsk - Elektro - Stary Sobor, the "live shapes" option
		m_MDTT_Canvas.Clear();
		array<vector> pts = {Vector(6500, 0, 2500), Vector(10300, 0, 2200), Vector(6100, 0, 7700), Vector(6500, 0, 2500)};
		for (int i = 0; i < pts.Count() - 1; i++)
		{
			vector a = m_MapWidgetInstance.MapToScreen(pts[i]);
			vector b = m_MapWidgetInstance.MapToScreen(pts[i + 1]);
			m_MDTT_Canvas.DrawLine(a[0] - mx, a[1] - my, b[0] - mx, b[1] - my, 4, ARGB(255, 255, 0, 255));
		}
	}
}
