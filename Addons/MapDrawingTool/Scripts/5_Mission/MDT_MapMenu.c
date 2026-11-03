// Draws approved player work on the full-screen map: markers, image layers and strokes.
// The overlay is a child of the map screen's root, sorted on top, and placed with the Map widget's
// MapToScreen. Until DayZ 1.30.164200 it sat inside the Map widget like vanilla's GPS marker; since
// 1.30.164289 (2026-11-03) nothing added inside the Map widget is drawn.
modded class MapMenu
{
	protected Widget m_MDT_Root;
	protected Widget m_MDT_Images;
	// Stroke canvases, keyed by opacity (see MDT_Canvas: for now every stroke draws solid).
	protected ref map<int, CanvasWidget> m_MDT_Canvases = new map<int, CanvasWidget>;
	protected ref array<ImageWidget> m_MDT_ImageWidgets = new array<ImageWidget>;
	protected ref array<ref MDT_Layer> m_MDT_ImageLayers = new array<ref MDT_Layer>;
	protected int m_MDT_Seen = -1;

	override Widget Init()
	{
		Widget root = super.Init();
		if (!m_MapWidgetInstance)
			return root;
		m_MDT_Root = GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_overlay.layout", layoutRoot);
		if (m_MDT_Root)
		{
			m_MDT_Root.SetSort(1000);
			m_MDT_Images = m_MDT_Root.FindAnyWidget("MDTImages");
		}
		array<ref MDT_Marker> markers = MDT_Live.GetMarkers();
		if (markers)
		{
			foreach (MDT_Marker m : markers)
				m_MapWidgetInstance.AddUserMark(Vector(m.x, 0, m.z), m.text, m.color, MapMarkerTypes.GetMarkerTypeFromID(m.icon));
		}
		MDT_Refresh();
		return root;
	}

	// (Re)creates image widgets when the set of downloaded layers changed, oldest at the bottom.
	protected void MDT_Refresh()
	{
		m_MDT_Seen = MDT_Live.s_ReadyCount;
		foreach (ImageWidget old : m_MDT_ImageWidgets)
			old.Unlink();
		m_MDT_ImageWidgets.Clear();
		m_MDT_ImageLayers.Clear();
		if (!m_MDT_Images || !MDT_Live.s_Manifest)
			return;
		foreach (MDT_Layer layer : MDT_Live.s_Manifest.layers)
		{
			string path;
			if (layer.kind != "image" || !MDT_Live.s_ImagePaths.Find(layer.id, path))
				continue;
			ImageWidget w = ImageWidget.Cast(GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_image.layout", m_MDT_Images));
			if (!w || !w.LoadImageFile(0, path, true))
			{
				Print("[MapDrawingTool] map screen: cannot show " + path + " (widget " + (w != null) + ")");
				continue;
			}
			w.SetImage(0);
			m_MDT_ImageWidgets.Insert(w);
			m_MDT_ImageLayers.Insert(layer);
		}
	}

	override void Update(float timeslice)
	{
		super.Update(timeslice);
		if (!m_MapWidgetInstance || !m_MDT_Root)
			return;
		if (m_MDT_Seen != MDT_Live.s_ReadyCount)
			MDT_Refresh();

		// MapToScreen is in screen pixels; overlay children are placed relative to the overlay
		float mx, my;
		m_MDT_Root.GetScreenPos(mx, my);
		for (int i = 0; i < m_MDT_ImageWidgets.Count(); i++)
		{
			array<float> r = m_MDT_ImageLayers[i].rect;
			vector tl = m_MapWidgetInstance.MapToScreen(Vector(r[0], 0, r[3]));
			vector br = m_MapWidgetInstance.MapToScreen(Vector(r[2], 0, r[1]));
			m_MDT_ImageWidgets[i].SetPos(tl[0] - mx, tl[1] - my);
			m_MDT_ImageWidgets[i].SetSize(br[0] - tl[0], br[1] - tl[1]);
		}

		foreach (CanvasWidget c : m_MDT_Canvases)
			c.Clear();
		if (!MDT_Live.s_Manifest)
			return;
		vector a0 = m_MapWidgetInstance.MapToScreen(Vector(0, 0, 0));
		vector a1 = m_MapWidgetInstance.MapToScreen(Vector(1000, 0, 0));
		float pxPerMetre = (a1[0] - a0[0]) / 1000;
		foreach (MDT_Layer layer : MDT_Live.s_Manifest.layers)
		{
			if (layer.kind != "strokes" || !layer.strokes)
				continue;
			foreach (MDT_Stroke s : layer.strokes)
			{
				if (!s.points || s.points.Count() < 4)
					continue;
				CanvasWidget canvas = MDT_Canvas((s.color >> 24) & 0xFF);
				if (!canvas)
					continue;
				int opaque = s.color | 0xFF000000;
				float width = Math.Max(1, s.width * pxPerMetre);
				float half = width * 0.5;
				// zoomed out, points closer than half the line width to the last one are skipped
				float minStep = Math.Max(3, half);
				int last = s.points.Count() - 2;
				vector p = m_MapWidgetInstance.MapToScreen(Vector(s.points[0], 0, s.points[1]));
				MDT_Dot(canvas, p[0] - mx, p[1] - my, width, opaque);
				for (int j = 2; j <= last; j += 2)
				{
					vector q = m_MapWidgetInstance.MapToScreen(Vector(s.points[j], 0, s.points[j + 1]));
					float dx = q[0] - p[0];
					float dy = q[1] - p[1];
					if (j < last && dx * dx + dy * dy < minStep * minStep)
						continue;
					canvas.DrawLine(p[0] - mx, p[1] - my, q[0] - mx, q[1] - my, width, opaque);
					// a round dot on every joint and end fills the corner gap without square tips
					MDT_Dot(canvas, q[0] - mx, q[1] - my, width, opaque);
					p = q;
				}
			}
		}
	}

	// A canvas has no circle: four short bars at 45° steps, each 0.42 of the width thick, make an
	// octagon as wide as the line. Thin lines need none.
	protected void MDT_Dot(CanvasWidget canvas, float x, float y, float width, int color)
	{
		if (width < 4)
			return;
		float r = width * 0.5;
		float d = r * 0.7071;
		float t = width * 0.42;
		canvas.DrawLine(x - r, y, x + r, y, t, color);
		canvas.DrawLine(x, y - r, x, y + r, t, color);
		canvas.DrawLine(x - d, y - d, x + d, y + d, t, color);
		canvas.DrawLine(x - d, y + d, x + d, y - d, t, color);
	}

	// The canvas for strokes of this opacity (0-255), made on first use.
	protected CanvasWidget MDT_Canvas(int alpha)
	{
		CanvasWidget c;
		if (m_MDT_Canvases.Find(alpha, c))
			return c;
		// Tested 2026-10-27: a canvas ignores SetAlpha on itself and on a parent frame, and a
		// layout RTTextureWidget shown through an ImageWidget comes out blank white. So strokes are
		// drawn solid; see-through would need the colour's alpha, which darkens where pieces overlap.
		c = CanvasWidget.Cast(GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_canvas.layout", m_MDT_Root));
		if (c)
			m_MDT_Canvases.Set(alpha, c);
		return c;
	}

	// use3DMap: the screen was opened with M over the unfolded paper; closing it (M or Esc) keeps
	// the paper unfolded. Vanilla CloseMapMenu tells the server the map is closing.
	override void CloseMapMenu()
	{
		if (CfgGameplayHandler.GetUse3DMap())
			MDT_CloseScreen();
		else
			super.CloseMapMenu();
	}

	void MDT_CloseScreen()
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
}

// use3DMap: unfolding a map shows the paper in hand only; M toggles the full-screen map over it.
// M is read as a raw key: the "map" input group is excluded while the screen is up, and vanilla
// only handles UAMapToggle with ignoreMapOwnership. Any open menu (chat, inventory) blocks it.
modded class MissionGameplay
{
	protected ItemMap m_MDT_Paper;
	protected string m_MDT_PaperTexture;
	protected bool m_MDT_PaperOpen;
	protected float m_MDT_PaperAgain;

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);
		MDT_Live.Update(timeslice);
		MDT_UpdatePaper(timeslice);

		if (!CfgGameplayHandler.GetUse3DMap() || !KeyState(KeyCode.KC_M))
			return;
		ClearKey(KeyCode.KC_M);

		UIManager um = GetGame().GetUIManager();
		if (um.IsMenuOpen(MENU_MAP))
		{
			MapMenu open = MapMenu.Cast(um.FindMenu(MENU_MAP));
			if (open)
				open.MDT_CloseScreen();
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

	// Puts the server's map texture (MDT_Live.s_PaperPath) on the paper map in this player's hands.
	// ItemMap is an engine class and cannot be modded, so this watches the hands instead. In the
	// 2026-11-03 test a texture set before the first unfold did not show, so on every unfold it is
	// also set every frame for a second (PlayerBase.SetMapOpen sets it first, as the paper unfolds).
	protected void MDT_UpdatePaper(float timeslice)
	{
		PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
		ItemMap paper;
		if (player)
			paper = ItemMap.Cast(player.GetItemInHands());
		if (!paper)
		{
			m_MDT_Paper = null;
			return;
		}
		string texture = MDT_Live.s_PaperPath;
		if (texture == "")
			texture = paper.m_TextureOpened;
		bool open = player.IsMapOpen();
		bool again = false;
		if (m_MDT_PaperAgain > 0)
		{
			m_MDT_PaperAgain -= timeslice;
			again = true;
		}
		if (!again && paper == m_MDT_Paper && texture == m_MDT_PaperTexture && open == m_MDT_PaperOpen)
			return;
		if (open && !m_MDT_PaperOpen)
			m_MDT_PaperAgain = 1;
		m_MDT_Paper = paper;
		m_MDT_PaperTexture = texture;
		m_MDT_PaperOpen = open;
		paper.SetObjectTexture(1, texture);
	}
}
