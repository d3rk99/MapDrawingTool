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

	// Drawing in game: with a pen in the inventory, D on the full-screen map (see MDT_UpdateDrawing).
	static const float MDT_WIDTHS[4] = {15, 30, 60, 120};      // line width, metres on the ground
	static const int MDT_MAX_POINTS = 3000;
	protected bool m_MDT_Drawing;
	protected ref MDT_Stroke m_MDT_Current;                     // the line under the mouse
	protected vector m_MDT_LockPos;
	protected float m_MDT_LockScale;
	protected float m_MDT_LastX, m_MDT_LastY;
	protected ref array<string> m_MDT_Pens = new array<string>; // pens carried, by class name
	protected int m_MDT_Pen;
	protected int m_MDT_Width = 1;
	protected int m_MDT_Scope;                                  // index into MDT_Scopes()
	protected int m_MDT_Life = -1;                              // index into options.lifespans
	protected Widget m_MDT_Bar;
	protected TextWidget m_MDT_BarText;

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
		m_MDT_Bar = GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_drawbar.layout", layoutRoot);
		if (m_MDT_Bar)
		{
			m_MDT_Bar.SetSort(1001);
			m_MDT_BarText = TextWidget.Cast(m_MDT_Bar.FindAnyWidget("MDTDrawText"));
			m_MDT_Bar.Show(false);
		}
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
				MDT_DrawStroke(s, mx, my, pxPerMetre);
		}
		MDT_UpdateDrawing(mx, my, pxPerMetre);
	}

	protected void MDT_DrawStroke(MDT_Stroke s, float mx, float my, float pxPerMetre)
	{
		if (!s.points || s.points.Count() < 2)
			return;
		CanvasWidget canvas = MDT_Canvas((s.color >> 24) & 0xFF);
		if (!canvas)
			return;
		int opaque = s.color | 0xFF000000;
		float width = Math.Max(1, s.width * pxPerMetre);
		float half = width * 0.5;
		// zoomed out, points closer than half the line width to the last one are skipped
		float minStep = Math.Max(3, half);
		int last = s.points.Count() - 2;
		vector p = m_MapWidgetInstance.MapToScreen(Vector(s.points[0], 0, s.points[1]));
		MDT_Dot(canvas, p[0] - mx, p[1] - my, Math.Max(width, 4), opaque);
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

	// ---------------------------------------------------------------- drawing in game
	// Keys while the full-screen map is open (raw keys: the "map" input group is excluded here):
	// D draw on/off, left mouse draws, Z undo, Backspace clear, C pen colour, W line width,
	// G everyone / only me, L lifespan, Enter send. The map is held still while the mouse button is down.

	protected MDT_Options MDT_Opts()
	{
		if (MDT_Live.s_Manifest)
			return MDT_Live.s_Manifest.options;
		return null;
	}

	protected array<string> MDT_Scopes()
	{
		array<string> list = new array<string>;
		MDT_Options o = MDT_Opts();
		if (o && o.allowGlobal)
			list.Insert("global");
		if (o && o.allowPersonal)
			list.Insert("personal");
		return list;
	}

	protected void MDT_FindPens()
	{
		m_MDT_Pens.Clear();
		PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
		if (!player)
			return;
		array<EntityAI> items = new array<EntityAI>;
		player.GetInventory().EnumerateInventory(InventoryTraversalType.PREORDER, items);
		foreach (EntityAI item : items)
		{
			if (item && item.IsKindOf("Pen_ColorBase") && m_MDT_Pens.Find(item.GetType()) < 0)
				m_MDT_Pens.Insert(item.GetType());
		}
		if (m_MDT_Pen >= m_MDT_Pens.Count())
			m_MDT_Pen = 0;
	}

	protected int MDT_PenColor()
	{
		if (m_MDT_Pens.Count() == 0)
			return 0xFF1E1E1E;
		string pen = m_MDT_Pens[m_MDT_Pen];
		if (pen == "Pen_Red")
			return 0xFFC62828;
		if (pen == "Pen_Green")
			return 0xFF2E7D32;
		if (pen == "Pen_Blue")
			return 0xFF1F4FB8;
		return 0xFF1E1E1E;
	}

	protected string MDT_PenName()
	{
		if (m_MDT_Pens.Count() == 0)
			return "none";
		string pen = m_MDT_Pens[m_MDT_Pen];
		pen.Replace("Pen_", "");
		return pen;
	}

	protected bool MDT_Key(int key)
	{
		if (!KeyState(key))
			return false;
		ClearKey(key);
		return true;
	}

	protected int MDT_PointCount()
	{
		int n = 0;
		foreach (MDT_Stroke s : MDT_Draft.s_Strokes)
			n += s.points.Count() / 2;
		if (m_MDT_Current)
			n += m_MDT_Current.points.Count() / 2;
		return n;
	}

	protected void MDT_UpdateDrawing(float mx, float my, float pxPerMetre)
	{
		MDT_Options o = MDT_Opts();
		bool allowed = o && o.ingame;
		if (m_MDT_Bar)
			m_MDT_Bar.Show(allowed);
		if (!allowed)
			return;
		array<string> scopes = MDT_Scopes();
		if (m_MDT_Life < 0 && o.lifespans && o.lifespans.Count() > 0)
			m_MDT_Life = Math.Max(0, o.lifespans.Find(o.defaultDays));

		if (MDT_Key(KeyCode.KC_D))
		{
			MDT_FindPens();
			if (m_MDT_Pens.Count() == 0)
			{
				m_MDT_Drawing = false;
				MDT_Draft.s_Status = "You need a pen in your inventory to draw";
			}
			else
				m_MDT_Drawing = !m_MDT_Drawing;
		}
		if (m_MDT_Drawing)
		{
			if (MDT_Key(KeyCode.KC_C))
			{
				MDT_FindPens();
				if (m_MDT_Pens.Count() > 0)
					m_MDT_Pen = (m_MDT_Pen + 1) % m_MDT_Pens.Count();
			}
			if (MDT_Key(KeyCode.KC_W))
				m_MDT_Width = (m_MDT_Width + 1) % 4;
			if (MDT_Key(KeyCode.KC_G) && scopes.Count() > 0)
				m_MDT_Scope = (m_MDT_Scope + 1) % scopes.Count();
			if (MDT_Key(KeyCode.KC_L) && o.lifespans && o.lifespans.Count() > 0)
				m_MDT_Life = (m_MDT_Life + 1) % o.lifespans.Count();
			if (MDT_Key(KeyCode.KC_Z) && MDT_Draft.s_Strokes.Count() > 0)
				MDT_Draft.s_Strokes.Remove(MDT_Draft.s_Strokes.Count() - 1);
			if (MDT_Key(KeyCode.KC_BACK))
				MDT_Draft.s_Strokes.Clear();
			if (MDT_Key(KeyCode.KC_RETURN))
				MDT_SendDraft(scopes);
			MDT_TrackMouse();
		}
		if (m_MDT_Scope >= scopes.Count())
			m_MDT_Scope = 0;

		foreach (MDT_Stroke s : MDT_Draft.s_Strokes)
			MDT_DrawStroke(s, mx, my, pxPerMetre);
		if (m_MDT_Current)
			MDT_DrawStroke(m_MDT_Current, mx, my, pxPerMetre);
		MDT_ShowBar(scopes);
	}

	// Left mouse down: adds points to the current line, and holds the map still (it would pan).
	protected void MDT_TrackMouse()
	{
		int x, y;
		GetMousePos(x, y);
		bool down = (GetMouseState(MouseState.LEFT) & MB_PRESSED_MASK) != 0;
		if (!down)
		{
			if (m_MDT_Current)
			{
				if (m_MDT_Current.points.Count() == 2)             // a click: a dot
				{
					m_MDT_Current.points.Insert(m_MDT_Current.points[0] + 0.5);
					m_MDT_Current.points.Insert(m_MDT_Current.points[1]);
				}
				MDT_Draft.s_Strokes.Insert(m_MDT_Current);
				m_MDT_Current = null;
			}
			return;
		}
		if (!m_MDT_Current)
		{
			if (MDT_PointCount() >= MDT_MAX_POINTS)
			{
				MDT_Draft.s_Status = "The drawing is full: send it, or undo some lines";
				return;
			}
			m_MDT_Current = new MDT_Stroke();
			m_MDT_Current.color = MDT_PenColor();
			m_MDT_Current.width = MDT_WIDTHS[m_MDT_Width];
			m_MDT_Current.points = new array<float>;
			m_MDT_LockPos = m_MapWidgetInstance.GetMapPos();
			m_MDT_LockScale = m_MapWidgetInstance.GetScale();
			m_MDT_LastX = -1000;
		}
		else
		{
			MDT_HoldMap();
		}
		float dx = x - m_MDT_LastX;
		float dy = y - m_MDT_LastY;
		if (dx * dx + dy * dy < 16 || MDT_PointCount() >= MDT_MAX_POINTS)
			return;
		vector w = m_MapWidgetInstance.ScreenToMap(Vector(x, y, 0));
		m_MDT_Current.points.Insert(Math.Round(w[0] * 10) / 10);
		m_MDT_Current.points.Insert(Math.Round(w[2] * 10) / 10);
		m_MDT_LastX = x;
		m_MDT_LastY = y;
	}

	protected void MDT_HoldMap()
	{
		m_MapWidgetInstance.SetScale(m_MDT_LockScale);
		m_MapWidgetInstance.SetMapPos(m_MDT_LockPos);
	}

	protected void MDT_SendDraft(array<string> scopes)
	{
		if (MDT_Draft.s_Sending)
			return;
		if (MDT_Draft.s_Strokes.Count() == 0)
		{
			MDT_Draft.s_Status = "Draw something first";
			return;
		}
		if (scopes.Count() == 0)
			return;
		MDT_Options o = MDT_Opts();
		MDT_Submit submit = new MDT_Submit();
		submit.title = "Drawn in game";
		submit.scope = scopes[m_MDT_Scope];
		if (o.lifespans && m_MDT_Life >= 0 && m_MDT_Life < o.lifespans.Count())
			submit.days = o.lifespans[m_MDT_Life];
		foreach (MDT_Stroke s : MDT_Draft.s_Strokes)
			submit.strokes.Insert(s);
		string json, error;
		JsonFileLoader<MDT_Submit>.MakeData(submit, json, error, false);
		PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
		if (!player)
			return;
		int len = json.Length();
		int count = Math.Max(1, (len + MDT_RPC_CHUNK - 1) / MDT_RPC_CHUNK);
		if (count > 200)
		{
			MDT_Draft.s_Status = "The drawing is too big; send it in parts";
			return;
		}
		for (int i = 0; i < count; i++)
		{
			ScriptRPC rpc = new ScriptRPC();
			rpc.Write(i);
			rpc.Write(count);
			rpc.Write(json.Substring(i * MDT_RPC_CHUNK, Math.Min(MDT_RPC_CHUNK, len - i * MDT_RPC_CHUNK)));
			rpc.Send(player, MDT_RPC_SUBMIT, true, null);
		}
		MDT_Draft.s_Sending = true;
		MDT_Draft.s_Status = "Sending...";
	}

	protected void MDT_ShowBar(array<string> scopes)
	{
		if (!m_MDT_BarText)
			return;
		MDT_Options o = MDT_Opts();
		string text;
		if (!m_MDT_Drawing)
			text = "D: draw on this map (needs a pen)";
		else
		{
			text = "DRAWING with the " + MDT_PenName() + " pen (D: stop)\n";
			text += "Left mouse: draw    Z: undo    Backspace: clear\n";
			text += "C: pen colour    W: line " + MDT_WIDTHS[m_MDT_Width] + " m\n";
			string who = "everyone";
			if (scopes.Count() > 0 && scopes[m_MDT_Scope] == "personal")
				who = "only me";
			string keep = "permanent";
			if (o.lifespans && m_MDT_Life >= 0 && m_MDT_Life < o.lifespans.Count() && o.lifespans[m_MDT_Life] > 0)
				keep = o.lifespans[m_MDT_Life].ToString() + " days";
			text += "G: shown to " + who + "    L: keep " + keep + "\n";
			text += "Enter: send " + MDT_Draft.s_Strokes.Count() + " lines";
			if (who == "everyone" || o.approvePersonal)
				text += " for approval";
		}
		if (MDT_Draft.s_Status != "")
			text += "\n" + MDT_Draft.s_Status;
		m_MDT_BarText.SetText(text);
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
