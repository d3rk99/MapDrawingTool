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
	protected TextWidget m_MDT_BarStatus;
	protected Widget m_MDT_BarKeys;
	protected int m_MDT_BarMode = -1;                           // which key list the bar shows
	protected Widget m_MDT_Catch;                               // over the map while drawing: it gets no left clicks
	protected bool m_MDT_Panning;
	protected int m_MDT_PanX, m_MDT_PanY;
	protected vector m_MDT_PanFrom;
	protected float m_MDT_LastScale;
	// X: the player's own submissions, one at a time (map moves to it, outline and line preview)
	protected bool m_MDT_Browse;
	protected int m_MDT_BrowseIdx;
	protected int m_MDT_BrowseSeen = -1;
	protected float m_MDT_DeleteArmed;                          // seconds left to press Delete again
	// Tools while drawing: 1 pen, 2 marker, 3 text, 4 stamp
	static const string MDT_TOOLS[4] = {"Pen", "Marker", "Text", "Stamp"};
	static const string MDT_FONTS[5] = {"plain", "bold", "hand", "marker", "comic"};
	static const string MDT_FONT_NAMES[5] = {"Plain", "Bold", "Handwritten", "Marker", "Comic"};
	static const float MDT_TEXT_SIZES[4] = {60, 120, 250, 500};     // letter height, metres
	static const float MDT_STAMP_SIZES[4] = {100, 200, 400, 800};   // width, metres
	static const string MDT_STAMPS[9] = {"flag", "skull", "swords", "tent", "star", "cross", "ring", "arrow", "danger"};
	static const string MDT_ICONS[20] = {"Border crossing", "Broadleaf", "Camp", "Factory", "Fir", "Fire station",
		"Government", "Hill", "Monument", "Palm", "Police", "Station", "Store", "Tourism", "Transmitter", "Shelter",
		"Trail sign", "Viewpoint", "Vineyard", "Water pump"};
	protected int m_MDT_Tool;
	protected int m_MDT_Icon = 2;
	protected int m_MDT_Font;
	protected int m_MDT_TextSize = 1;
	protected int m_MDT_StampSize = 1;
	protected int m_MDT_StampIdx;
	protected float m_MDT_Rot;
	protected ref array<string> m_MDT_StampList = new array<string>;   // built-in names, then "file:<name>"
	protected bool m_MDT_LeftWas;
	protected Widget m_MDT_Edit;
	protected EditBoxWidget m_MDT_EditBox;
	protected TextWidget m_MDT_EditLabel;
	protected int m_MDT_EditKind;                               // 2 text, 4 marker: what the box is for
	protected float m_MDT_EditX, m_MDT_EditZ;
	// previews of what is placed but not sent
	protected int m_MDT_PrevVersion = -1;
	protected ref array<Widget> m_MDT_PrevTexts = new array<Widget>;
	protected ref array<Widget> m_MDT_PrevStamps = new array<Widget>;
	protected ref array<Widget> m_MDT_PrevMarkers = new array<Widget>;
	protected ref array<Widget> m_MDT_PrevLabels = new array<Widget>;
	protected ImageWidget m_MDT_Ghost;                          // the stamp under the mouse
	protected string m_MDT_GhostStamp;
	protected ref set<string> m_MDT_NoPreview = new set<string>;   // PNGs DayZ would not show

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
		m_MDT_Catch = GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_drawcatch.layout", layoutRoot);
		if (m_MDT_Catch)
		{
			m_MDT_Catch.SetSort(999);
			m_MDT_Catch.Show(false);
		}
		m_MDT_Edit = GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_drawedit.layout", layoutRoot);
		if (m_MDT_Edit)
		{
			m_MDT_Edit.SetSort(1002);
			m_MDT_EditBox = EditBoxWidget.Cast(m_MDT_Edit.FindAnyWidget("MDTDrawEditBox"));
			m_MDT_EditLabel = TextWidget.Cast(m_MDT_Edit.FindAnyWidget("MDTDrawEditLabel"));
			m_MDT_Edit.Show(false);
		}
		MDT_Draft.s_Typing = false;
		m_MDT_Bar = GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_drawbar.layout", layoutRoot);
		if (m_MDT_Bar)
		{
			m_MDT_Bar.SetSort(1001);
			m_MDT_BarStatus = TextWidget.Cast(m_MDT_Bar.FindAnyWidget("MDTDrawStatus"));
			m_MDT_BarKeys = m_MDT_Bar.FindAnyWidget("MDTDrawKeys");
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
	// D draw on/off, left mouse draws, right mouse drags the map, wheel or +/- zooms, Z undo,
	// Backspace clear, C pen colour, W line width, G everyone / only me, L lifespan, Enter send.
	// While drawing, a see-through panel covers the map so its own left-drag panning never starts;
	// the map is also held still while the left button is down, in case a drag still reaches it.

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

		if (m_MDT_Catch)
			m_MDT_Catch.Show(m_MDT_Drawing);
		if (MDT_Key(KeyCode.KC_X))
		{
			m_MDT_Browse = !m_MDT_Browse;
			if (m_MDT_Browse)
				MDT_AskMine();
			else
				MDT_Draft.s_Status = "";
		}
		if (m_MDT_Browse)
		{
			m_MDT_Drawing = false;
			MDT_Browse(mx, my, pxPerMetre);
			MDT_ShowBar(scopes);
			return;
		}
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
		if (MDT_Draft.s_Typing)
		{
			MDT_UpdateTyping();
			MDT_DrawPreviews(mx, my, pxPerMetre);
			MDT_ShowBar(scopes);
			return;
		}
		if (m_MDT_Drawing)
		{
			if (MDT_Key(KeyCode.KC_1))
				m_MDT_Tool = 0;
			if (MDT_Key(KeyCode.KC_2))
				m_MDT_Tool = 1;
			if (MDT_Key(KeyCode.KC_3))
				m_MDT_Tool = 2;
			if (MDT_Key(KeyCode.KC_4))
			{
				m_MDT_Tool = 3;
				MDT_ListStamps();
			}
			if (MDT_Key(KeyCode.KC_F))
				m_MDT_Font = (m_MDT_Font + 1) % 5;
			if (MDT_Key(KeyCode.KC_Q))
				m_MDT_Rot = Math.ModFloat(m_MDT_Rot + 345, 360);
			if (MDT_Key(KeyCode.KC_E))
				m_MDT_Rot = Math.ModFloat(m_MDT_Rot + 15, 360);
			bool next = MDT_Key(KeyCode.KC_RBRACKET);
			bool prev = MDT_Key(KeyCode.KC_LBRACKET);
			if (m_MDT_Tool == 1 && (next || prev))
			{
				if (next)
					m_MDT_Icon = (m_MDT_Icon + 1) % 20;
				else
					m_MDT_Icon = (m_MDT_Icon + 19) % 20;
			}
			if (m_MDT_Tool == 3 && m_MDT_StampList.Count() > 0 && (next || prev))
			{
				int n = m_MDT_StampList.Count();
				if (next)
					m_MDT_StampIdx = (m_MDT_StampIdx + 1) % n;
				else
					m_MDT_StampIdx = (m_MDT_StampIdx + n - 1) % n;
				string picked = m_MDT_StampList[m_MDT_StampIdx];
				if (picked.IndexOf("file:") == 0)       // upload now, so the preview is back before placing
					MDT_Draft.Upload(picked.Substring(5, picked.Length() - 5));
			}
			if (MDT_Key(KeyCode.KC_C))
			{
				MDT_FindPens();
				if (m_MDT_Pens.Count() > 0)
					m_MDT_Pen = (m_MDT_Pen + 1) % m_MDT_Pens.Count();
			}
			if (MDT_Key(KeyCode.KC_W))
			{
				if (m_MDT_Tool == 2)
					m_MDT_TextSize = (m_MDT_TextSize + 1) % 4;
				else if (m_MDT_Tool == 3)
					m_MDT_StampSize = (m_MDT_StampSize + 1) % 4;
				else
					m_MDT_Width = (m_MDT_Width + 1) % 4;
			}
			if (MDT_Key(KeyCode.KC_G) && scopes.Count() > 0)
				m_MDT_Scope = (m_MDT_Scope + 1) % scopes.Count();
			if (MDT_Key(KeyCode.KC_L) && o.lifespans && o.lifespans.Count() > 0)
				m_MDT_Life = (m_MDT_Life + 1) % o.lifespans.Count();
			if (MDT_Key(KeyCode.KC_Z))
				MDT_Draft.Undo();
			if (MDT_Key(KeyCode.KC_BACK))
				MDT_Draft.Clear();
			if (MDT_Key(KeyCode.KC_RETURN))
				MDT_SendDraft(scopes);
			if (m_MDT_Tool == 0)
				MDT_TrackMouse();
			else
				MDT_PlaceOnClick();
			MDT_PanAndZoom(pxPerMetre);
		}
		else
			m_MDT_Panning = false;
		if (m_MDT_Scope >= scopes.Count())
			m_MDT_Scope = 0;

		foreach (MDT_Stroke s : MDT_Draft.s_Strokes)
			MDT_DrawStroke(s, mx, my, pxPerMetre);
		if (m_MDT_Current)
			MDT_DrawStroke(m_MDT_Current, mx, my, pxPerMetre);
		MDT_DrawPreviews(mx, my, pxPerMetre);
		MDT_ShowBar(scopes);
	}

	// ---------------------------------------------------------------- markers, text, stamps

	protected void MDT_ListStamps()
	{
		m_MDT_StampList.Clear();
		foreach (string builtin : MDT_STAMPS)
			m_MDT_StampList.Insert(builtin);
		foreach (string own : MDT_Draft.OwnStamps())
			m_MDT_StampList.Insert("file:" + own);
		if (m_MDT_StampIdx >= m_MDT_StampList.Count())
			m_MDT_StampIdx = 0;
	}

	protected string MDT_StampTitle(string stamp)
	{
		if (stamp.IndexOf("file:") == 0)
			return stamp.Substring(5, stamp.Length() - 5) + " (yours)";
		return stamp;
	}

	// The texture a stamp preview shows: the mod's PAA, or the player's PNG straight from the folder.
	protected string MDT_StampTexture(string stamp)
	{
		if (stamp.IndexOf("file:") == 0)
		{
			string file = stamp.Substring(5, stamp.Length() - 5);
			string preview;
			if (MDT_Live.s_StampPaths.Find(file, preview))
				return preview;                     // the web app's PAA of it
			return MDT_STAMPS_DIR + "/" + file;     // never loaded (see MDT_StampWidget): an outline
		}
		return "MapDrawingTool/data/stamps/" + stamp + ".paa";
	}

	protected void MDT_PlaceOnClick()
	{
		bool down = (GetMouseState(MouseState.LEFT) & MB_PRESSED_MASK) != 0;
		bool click = down && !m_MDT_LeftWas;
		m_MDT_LeftWas = down;
		if (!click)
			return;
		int x, y;
		GetMousePos(x, y);
		vector w = m_MapWidgetInstance.ScreenToMap(Vector(x, y, 0));
		if (m_MDT_Tool == 1 || m_MDT_Tool == 2)
		{
			m_MDT_EditKind = 4;
			string label = "Marker:";
			if (m_MDT_Tool == 2)
			{
				m_MDT_EditKind = 2;
				label = "Text:";
			}
			m_MDT_EditX = w[0];
			m_MDT_EditZ = w[2];
			if (m_MDT_EditLabel)
				m_MDT_EditLabel.SetText(label);
			if (m_MDT_EditBox)
				m_MDT_EditBox.SetText("");
			if (m_MDT_Edit)
				m_MDT_Edit.Show(true);
			SetFocus(m_MDT_EditBox);
			MDT_Draft.s_Typing = true;
			return;
		}
		if (m_MDT_Tool == 3 && m_MDT_StampList.Count() > 0)
		{
			string stamp = m_MDT_StampList[m_MDT_StampIdx];
			MDT_SubmitStamp st = new MDT_SubmitStamp();
			st.x = Math.Round(w[0]);
			st.z = Math.Round(w[2]);
			st.size = MDT_STAMP_SIZES[m_MDT_StampSize];
			st.rot = m_MDT_Rot;
			st.stamp = stamp;
			if (stamp.IndexOf("file:") == 0)
			{
				st.file = stamp.Substring(5, stamp.Length() - 5);
				if (!MDT_Draft.Upload(st.file))
					return;
			}
			MDT_Draft.s_Stamps.Insert(st);
			MDT_Draft.Add(3);
		}
	}

	// The text box has the keyboard: Enter keeps what was typed, Tab (or Enter on nothing) cancels.
	protected void MDT_UpdateTyping()
	{
		bool done = MDT_Key(KeyCode.KC_RETURN) || MDT_Key(KeyCode.KC_NUMPADENTER);
		bool cancel = MDT_Key(KeyCode.KC_TAB);
		if (!done && !cancel)
			return;
		string text;
		if (m_MDT_EditBox)
			text = m_MDT_EditBox.GetText();
		text = text.Trim();
		if (done && text != "")
		{
			if (m_MDT_EditKind == 4)
			{
				MDT_Marker mk = new MDT_Marker();
				mk.x = Math.Round(m_MDT_EditX);
				mk.z = Math.Round(m_MDT_EditZ);
				mk.text = text.Substring(0, Math.Min(64, text.Length()));
				mk.icon = m_MDT_Icon;
				mk.color = MDT_PenColor();
				MDT_Draft.s_Markers.Insert(mk);
				MDT_Draft.Add(4);
			}
			else
			{
				MDT_SubmitText t = new MDT_SubmitText();
				t.x = Math.Round(m_MDT_EditX);
				t.z = Math.Round(m_MDT_EditZ);
				t.text = text.Substring(0, Math.Min(60, text.Length()));
				t.size = MDT_TEXT_SIZES[m_MDT_TextSize];
				t.color = MDT_PenColor();
				t.font = MDT_FONTS[m_MDT_Font];
				t.rot = m_MDT_Rot;
				MDT_Draft.s_Texts.Insert(t);
				MDT_Draft.Add(2);
			}
		}
		if (m_MDT_Edit)
			m_MDT_Edit.Show(false);
		SetFocus(null);
		MDT_Draft.s_Typing = false;
		m_MDT_LeftWas = true;                       // the click that follows is not a new placement
	}

	// Widgets showing placed text, stamps and markers (rebuilt when the draft changes, moved every
	// frame), and the stamp under the mouse.
	protected void MDT_DrawPreviews(float mx, float my, float pxPerMetre)
	{
		if (m_MDT_PrevVersion != MDT_Draft.s_Version)
		{
			m_MDT_PrevVersion = MDT_Draft.s_Version;
			MDT_ClearPreviews();
			foreach (MDT_SubmitText t : MDT_Draft.s_Texts)
			{
				TextWidget tw = TextWidget.Cast(GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_ptext_" + t.font + ".layout", m_MDT_Root));
				if (tw)
				{
					tw.SetText(t.text);
					int argb = t.color;
					tw.SetColor(argb);
				}
				m_MDT_PrevTexts.Insert(tw);
			}
			foreach (MDT_SubmitStamp st : MDT_Draft.s_Stamps)
				m_MDT_PrevStamps.Insert(MDT_StampWidget(st.stamp));
			foreach (MDT_Marker mk : MDT_Draft.s_Markers)
			{
				ImageWidget icon = ImageWidget.Cast(GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_image.layout", m_MDT_Root));
				if (icon && icon.LoadImageFile(0, MapMarkerTypes.GetMarkerTypeFromID(mk.icon), true))
				{
					icon.SetImage(0);
					icon.SetColor(mk.color);
				}
				m_MDT_PrevMarkers.Insert(icon);
				TextWidget label = TextWidget.Cast(GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_ptext_bold.layout", m_MDT_Root));
				if (label)
				{
					label.SetText(mk.text);
					label.SetColor(mk.color);
				}
				m_MDT_PrevLabels.Insert(label);
			}
		}
		for (int i = 0; i < MDT_Draft.s_Texts.Count() && i < m_MDT_PrevTexts.Count(); i++)
		{
			TextWidget w = TextWidget.Cast(m_MDT_PrevTexts[i]);
			if (!w)
				continue;
			MDT_SubmitText txt = MDT_Draft.s_Texts[i];
			w.SetTextExactSize(Math.Max(6, txt.size * pxPerMetre));
			float tw0, th0;
			w.GetScreenSize(tw0, th0);
			vector at = m_MapWidgetInstance.MapToScreen(Vector(txt.x, 0, txt.z));
			w.SetPos(at[0] - mx - tw0 / 2, at[1] - my - th0 / 2);
		}
		for (int j = 0; j < MDT_Draft.s_Stamps.Count() && j < m_MDT_PrevStamps.Count(); j++)
			MDT_PlaceStamp(m_MDT_PrevStamps[j], MDT_Draft.s_Stamps[j].stamp, MDT_Draft.s_Stamps[j].x, MDT_Draft.s_Stamps[j].z, MDT_Draft.s_Stamps[j].size, MDT_Draft.s_Stamps[j].rot, mx, my, pxPerMetre);
		for (int k = 0; k < MDT_Draft.s_Markers.Count() && k < m_MDT_PrevMarkers.Count(); k++)
		{
			vector m = m_MapWidgetInstance.MapToScreen(Vector(MDT_Draft.s_Markers[k].x, 0, MDT_Draft.s_Markers[k].z));
			if (m_MDT_PrevMarkers[k])
			{
				m_MDT_PrevMarkers[k].SetSize(28, 28);
				m_MDT_PrevMarkers[k].SetPos(m[0] - mx - 14, m[1] - my - 14);
			}
			if (m_MDT_PrevLabels[k])
				m_MDT_PrevLabels[k].SetPos(m[0] - mx + 16, m[1] - my - 12);
		}
		// the stamp under the mouse while the stamp tool is on
		bool ghost = m_MDT_Drawing && m_MDT_Tool == 3 && m_MDT_StampList.Count() > 0 && !MDT_Draft.s_Typing;
		string want;
		if (ghost)
			want = m_MDT_StampList[m_MDT_StampIdx];
		string wantKey = want;
		if (want != "")
			wantKey = want + "|" + MDT_StampTexture(want);   // rebuilt when a stamp's preview arrives
		if (wantKey != m_MDT_GhostStamp)
		{
			if (m_MDT_Ghost)
				m_MDT_Ghost.Unlink();
			m_MDT_Ghost = null;
			m_MDT_GhostStamp = wantKey;
			if (want != "")
				m_MDT_Ghost = MDT_StampWidget(want);
		}
		if (m_MDT_Ghost)
		{
			int gx, gy;
			GetMousePos(gx, gy);
			vector g = m_MapWidgetInstance.ScreenToMap(Vector(gx, gy, 0));
			MDT_PlaceStamp(m_MDT_Ghost, want, g[0], g[2], MDT_STAMP_SIZES[m_MDT_StampSize], m_MDT_Rot, mx, my, pxPerMetre);
			m_MDT_Ghost.SetAlpha(0.6);
		}
	}

	protected ImageWidget MDT_StampWidget(string stamp)
	{
		ImageWidget w = ImageWidget.Cast(GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_image.layout", m_MDT_Root));
		if (!w)
			return null;
		string texture = MDT_StampTexture(stamp);
		// Never hand a player's PNG to LoadImageFile (DayZ cannot show PNGs); its PAA preview from the
		// web app is fine. Until that arrives, an outline.
		if (stamp.IndexOf("file:") == 0 && texture.IndexOf(".paa") != texture.Length() - 4)
		{
			if (m_MDT_NoPreview.Find(texture) < 0)
				m_MDT_NoPreview.Insert(texture);
			return w;
		}
		if (w.LoadImageFile(0, texture, true))
			w.SetImage(0);
		else if (m_MDT_NoPreview.Find(texture) < 0)
		{
			m_MDT_NoPreview.Insert(texture);
			Print("[MapDrawingTool] no preview for " + texture + " (DayZ cannot show it; the web app will)");
		}
		return w;
	}

	// Centre, size and rotation of a stamp preview; stamps DayZ cannot show get an outline instead.
	protected void MDT_PlaceStamp(Widget w, string stamp, float x, float z, float size, float rot, float mx, float my, float pxPerMetre)
	{
		if (!w)
			return;
		float side = Math.Max(6, size * pxPerMetre);
		vector at = m_MapWidgetInstance.MapToScreen(Vector(x, 0, z));
		w.SetSize(side, side);
		w.SetPos(at[0] - mx - side / 2, at[1] - my - side / 2);
		w.SetRotation(0, 0, rot);
		if (m_MDT_NoPreview.Find(MDT_StampTexture(stamp)) < 0)
			return;
		CanvasWidget canvas = MDT_Canvas(255);
		if (!canvas)
			return;
		float x0 = at[0] - mx - side / 2;
		float y0 = at[1] - my - side / 2;
		int col = 0xFFFFC107;
		canvas.DrawLine(x0, y0, x0 + side, y0, 2, col);
		canvas.DrawLine(x0 + side, y0, x0 + side, y0 + side, 2, col);
		canvas.DrawLine(x0 + side, y0 + side, x0, y0 + side, 2, col);
		canvas.DrawLine(x0, y0 + side, x0, y0, 2, col);
	}

	protected void MDT_ClearPreviews()
	{
		foreach (Widget a : m_MDT_PrevTexts)
		{
			if (a)
				a.Unlink();
		}
		foreach (Widget b : m_MDT_PrevStamps)
		{
			if (b)
				b.Unlink();
		}
		foreach (Widget c : m_MDT_PrevMarkers)
		{
			if (c)
				c.Unlink();
		}
		foreach (Widget d : m_MDT_PrevLabels)
		{
			if (d)
				d.Unlink();
		}
		m_MDT_PrevTexts.Clear();
		m_MDT_PrevStamps.Clear();
		m_MDT_PrevMarkers.Clear();
		m_MDT_PrevLabels.Clear();
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
		if (MDT_Draft.IsEmpty())
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
		foreach (MDT_SubmitText t : MDT_Draft.s_Texts)
			submit.texts.Insert(t);
		foreach (MDT_Marker mk : MDT_Draft.s_Markers)
			submit.markers.Insert(mk);
		foreach (MDT_SubmitStamp st : MDT_Draft.s_Stamps)
		{
			MDT_SubmitStamp send = new MDT_SubmitStamp();
			send.x = st.x;
			send.z = st.z;
			send.size = st.size;
			send.rot = st.rot;
			send.stamp = st.stamp;
			if (st.file != "")
			{
				string id;
				MDT_Draft.s_Uploads.Find(st.file, id);
				if (id == "")
				{
					MDT_Draft.s_Status = "Wait until stamp " + st.file + " has uploaded";
					return;
				}
				send.stamp = "upload:" + id;
			}
			submit.stamps.Insert(send);
		}
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

	// Right mouse drags the map (the cover panel keeps the map's own dragging off); wheel or +/- zoom
	// around the mouse.
	protected void MDT_PanAndZoom(float pxPerMetre)
	{
		int x, y;
		GetMousePos(x, y);
		bool right = (GetMouseState(MouseState.RIGHT) & MB_PRESSED_MASK) != 0;
		if (right && !m_MDT_Current)
		{
			if (!m_MDT_Panning)
			{
				m_MDT_Panning = true;
				m_MDT_PanX = x;
				m_MDT_PanY = y;
				m_MDT_PanFrom = m_MapWidgetInstance.GetMapPos();
			}
			else if (pxPerMetre > 0)
			{
				// screen right = world east (+x), screen down = world south (-z)
				float east = (m_MDT_PanX - x) / pxPerMetre;
				float north = (y - m_MDT_PanY) / pxPerMetre;
				m_MapWidgetInstance.SetMapPos(m_MDT_PanFrom + Vector(east, 0, north));
			}
		}
		else
			m_MDT_Panning = false;

		int wheel = GetMouseState(MouseState.WHEEL);
		float zoom = 1;
		if (MDT_Key(KeyCode.KC_ADD) || MDT_Key(KeyCode.KC_EQUALS))
			zoom = 0.8;
		else if (MDT_Key(KeyCode.KC_SUBTRACT) || MDT_Key(KeyCode.KC_MINUS))
			zoom = 1.25;
		else if (wheel != 0 && m_MapWidgetInstance.GetScale() == m_MDT_LastScale)
		{
			// only when the map did not zoom itself this frame (the cover panel may or may not stop it)
			if (wheel > 0)
				zoom = 0.8;
			else
				zoom = 1.25;
		}
		if (zoom != 1)
		{
			vector before = m_MapWidgetInstance.ScreenToMap(Vector(x, y, 0));
			m_MapWidgetInstance.SetScale(Math.Clamp(m_MapWidgetInstance.GetScale() * zoom, 0.01, 1));
			vector after = m_MapWidgetInstance.ScreenToMap(Vector(x, y, 0));
			m_MapWidgetInstance.SetMapPos(m_MapWidgetInstance.GetMapPos() + before - after);
		}
		m_MDT_LastScale = m_MapWidgetInstance.GetScale();
	}

	protected void MDT_AskMine()
	{
		PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
		if (!player)
			return;
		ScriptRPC rpc = new ScriptRPC();
		rpc.Send(player, MDT_RPC_MINE_ASK, true, null);
		MDT_Draft.s_Status = "Loading your drawings...";
		m_MDT_DeleteArmed = 0;
	}

	protected MDT_MineItem MDT_BrowseItem()
	{
		MDT_MineList list = MDT_Mine.s_List;
		if (!list || !list.items || list.items.Count() == 0)
			return null;
		m_MDT_BrowseIdx = Math.Clamp(m_MDT_BrowseIdx, 0, list.items.Count() - 1);
		return list.items[m_MDT_BrowseIdx];
	}

	// Moves the map so the item fills about a third of the screen.
	protected void MDT_FocusItem(MDT_MineItem it, float pxPerMetre)
	{
		if (!it || !it.rect || it.rect.Count() != 4 || pxPerMetre <= 0)
			return;
		float w, h;
		m_MapWidgetInstance.GetScreenSize(w, h);
		float size = Math.Max(it.rect[2] - it.rect[0], it.rect[3] - it.rect[1]);
		float scale = m_MapWidgetInstance.GetScale() * size * pxPerMetre / (0.35 * Math.Max(w, 1));
		m_MapWidgetInstance.SetScale(Math.Clamp(scale, 0.01, 1));
		m_MapWidgetInstance.SetMapPos(Vector((it.rect[0] + it.rect[2]) / 2, 0, (it.rect[1] + it.rect[3]) / 2));
	}

	protected void MDT_Browse(float mx, float my, float pxPerMetre)
	{
		MDT_MineList list = MDT_Mine.s_List;
		if (m_MDT_BrowseSeen != MDT_Mine.s_Received)
		{
			m_MDT_BrowseSeen = MDT_Mine.s_Received;
			if (MDT_Draft.s_Status == "Loading your drawings...")
				MDT_Draft.s_Status = "";
			if (list && !list.ok)
				MDT_Draft.s_Status = list.error;
			MDT_FocusItem(MDT_BrowseItem(), pxPerMetre);
		}
		int count = 0;
		if (list && list.items)
			count = list.items.Count();
		if (count > 0 && (MDT_Key(KeyCode.KC_RIGHT) || MDT_Key(KeyCode.KC_DOWN)))
		{
			m_MDT_BrowseIdx = (m_MDT_BrowseIdx + 1) % count;
			m_MDT_DeleteArmed = 0;
			MDT_FocusItem(MDT_BrowseItem(), pxPerMetre);
		}
		if (count > 0 && (MDT_Key(KeyCode.KC_LEFT) || MDT_Key(KeyCode.KC_UP)))
		{
			m_MDT_BrowseIdx = (m_MDT_BrowseIdx + count - 1) % count;
			m_MDT_DeleteArmed = 0;
			MDT_FocusItem(MDT_BrowseItem(), pxPerMetre);
		}
		MDT_MineItem it = MDT_BrowseItem();
		if (m_MDT_DeleteArmed > 0)
			m_MDT_DeleteArmed -= 0.016;                         // about a frame; only needs to be rough
		if (it && MDT_Key(KeyCode.KC_DELETE))
		{
			if (m_MDT_DeleteArmed > 0)
			{
				PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
				if (player)
				{
					ScriptRPC rpc = new ScriptRPC();
					rpc.Write(it.id);
					rpc.Send(player, MDT_RPC_WITHDRAW, true, null);
					MDT_Draft.s_Status = "Removing...";
				}
				m_MDT_DeleteArmed = 0;
			}
			else
			{
				m_MDT_DeleteArmed = 3;
				MDT_Draft.s_Status = "Press Delete again to remove it for good";
			}
		}
		MDT_PanAndZoom(pxPerMetre);
		if (!it || !it.rect || it.rect.Count() != 4)
			return;
		// line drawings: their lines (also the ones still waiting for approval); then an outline
		if (it.strokes)
		{
			foreach (MDT_Stroke s : it.strokes)
				MDT_DrawStroke(s, mx, my, pxPerMetre);
		}
		CanvasWidget canvas = MDT_Canvas(255);
		if (!canvas)
			return;
		vector a = m_MapWidgetInstance.MapToScreen(Vector(it.rect[0], 0, it.rect[3]));
		vector b = m_MapWidgetInstance.MapToScreen(Vector(it.rect[2], 0, it.rect[1]));
		int yellow = 0xFFFFC107;
		float x0 = a[0] - mx - 6;
		float y0 = a[1] - my - 6;
		float x1 = b[0] - mx + 6;
		float y1 = b[1] - my + 6;
		canvas.DrawLine(x0, y0, x1, y0, 3, yellow);
		canvas.DrawLine(x1, y0, x1, y1, 3, yellow);
		canvas.DrawLine(x1, y1, x0, y1, 3, yellow);
		canvas.DrawLine(x0, y1, x0, y0, 3, yellow);
	}

	protected void MDT_ShowBar(array<string> scopes)
	{
		if (!m_MDT_BarStatus || !m_MDT_BarKeys)
			return;
		MDT_Options o = MDT_Opts();
		string text;
		if (m_MDT_Browse)
		{
			MDT_MineList list = MDT_Mine.s_List;
			MDT_MineItem it = MDT_BrowseItem();
			if (!it)
				text = "MY DRAWINGS    none on this map";
			else
			{
				string name = it.title;
				if (it.kind == "marker")
					name = "Marker " + it.text;
				if (name == "")
					name = "Untitled";
				string state = "on the map";
				if (it.status == "pending")
					state = "waiting for approval (not on the map yet)";
				else if (it.status == "rejected")
					state = "rejected";
				string seen = "everyone";
				if (it.scope == "personal")
					seen = "only you";
				string until = "permanent";
				if (it.expires != "")
					until = "until " + it.expires.Substring(0, 10);
				text = "MY DRAWINGS    " + (m_MDT_BrowseIdx + 1).ToString() + " / " + list.items.Count().ToString() + "    " + name + "    " + state + "    " + seen + "    " + until;
			}
		}
		else if (!m_MDT_Drawing)
			text = "Draw on this map with a pen from your inventory";
		else
		{
			string who = "everyone sees it";
			if (scopes.Count() > 0 && scopes[m_MDT_Scope] == "personal")
				who = "only you see it";
			string keep = "kept permanently";
			if (o.lifespans && m_MDT_Life >= 0 && m_MDT_Life < o.lifespans.Count() && o.lifespans[m_MDT_Life] > 0)
				keep = "kept " + o.lifespans[m_MDT_Life].ToString() + " days";
			text = "DRAWING    " + MDT_TOOLS[m_MDT_Tool] + "    " + MDT_PenName() + " colour    ";
			if (m_MDT_Tool == 0)
				text += MDT_WIDTHS[m_MDT_Width].ToString() + " m line    ";
			else if (m_MDT_Tool == 1)
				text += MDT_ICONS[m_MDT_Icon] + " icon    ";
			else if (m_MDT_Tool == 2)
				text += MDT_FONT_NAMES[m_MDT_Font] + " " + MDT_TEXT_SIZES[m_MDT_TextSize].ToString() + " m, turned " + m_MDT_Rot.ToString() + "°    ";
			else if (m_MDT_StampList.Count() > 0)
				text += MDT_StampTitle(m_MDT_StampList[m_MDT_StampIdx]) + " " + MDT_STAMP_SIZES[m_MDT_StampSize].ToString() + " m, turned " + m_MDT_Rot.ToString() + "°    ";
			text += who + "    " + keep;
			int items = MDT_Draft.s_Strokes.Count() + MDT_Draft.s_Texts.Count() + MDT_Draft.s_Stamps.Count() + MDT_Draft.s_Markers.Count();
			text += "    " + items.ToString() + " items";
		}
		if (MDT_Draft.s_Status != "")
			text += "      " + MDT_Draft.s_Status;
		m_MDT_BarStatus.SetText(text);

		int mode = 0;
		if (m_MDT_Drawing)
			mode = 10 + m_MDT_Tool;
		if (m_MDT_Browse)
			mode = 2;
		if (MDT_Draft.s_Typing)
			mode = 3;
		if (mode == m_MDT_BarMode)
			return;
		m_MDT_BarMode = mode;
		Widget child = m_MDT_BarKeys.GetChildren();
		while (child)
		{
			Widget next = child.GetSibling();
			child.Unlink();
			child = next;
		}
		if (mode == 0)
		{
			MDT_AddKey("D", "start drawing");
			MDT_AddKey("X", "my drawings");
		}
		else if (mode == 3)
		{
			MDT_AddKey("Enter", "keep it");
			MDT_AddKey("Tab", "cancel");
		}
		else if (mode == 2)
		{
			MDT_AddKey("Left  Right", "previous / next");
			MDT_AddKey("Delete", "remove (press twice)");
			MDT_AddKey("Right mouse", "move map");
			MDT_AddKey("Wheel  + -", "zoom");
			MDT_AddKey("X", "back");
		}
		else
		{
			MDT_AddKey("1-4", "pen marker text stamp");
			if (m_MDT_Tool == 0)
				MDT_AddKey("Left mouse", "draw");
			else
				MDT_AddKey("Left mouse", "place");
			MDT_AddKey("Right mouse", "move map");
			MDT_AddKey("Wheel  + -", "zoom");
			if (m_MDT_Tool == 0)
				MDT_AddKey("W", "line width");
			if (m_MDT_Tool == 1)
				MDT_AddKey("[ ]", "icon");
			if (m_MDT_Tool == 2)
			{
				MDT_AddKey("F", "font");
				MDT_AddKey("W", "size");
				MDT_AddKey("Q E", "turn");
			}
			if (m_MDT_Tool == 3)
			{
				MDT_AddKey("[ ]", "stamp");
				MDT_AddKey("W", "size");
				MDT_AddKey("Q E", "turn");
			}
			MDT_AddKey("C", "colour");
			MDT_AddKey("Z", "undo");
			MDT_AddKey("Backspace", "clear");
			MDT_AddKey("G", "who sees it");
			MDT_AddKey("L", "how long");
			MDT_AddKey("Enter", "send");
			MDT_AddKey("X", "mine");
			MDT_AddKey("D", "stop");
		}
		m_MDT_BarKeys.Update();
	}

	// One hotkey in the bar: the key in bold gold, its description after it.
	protected void MDT_AddKey(string key, string what)
	{
		TextWidget k = TextWidget.Cast(GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_drawkey.layout", m_MDT_BarKeys));
		TextWidget d = TextWidget.Cast(GetGame().GetWorkspace().CreateWidgets("MapDrawingTool/gui/mdt_drawdesc.layout", m_MDT_BarKeys));
		if (k)
			k.SetText(key + " ");
		if (d)
			d.SetText(what + "      ");
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
		MDT_Watch.Update(timeslice);

		MDT_Draft.Update();
		if (MDT_Draft.s_Typing || !CfgGameplayHandler.GetUse3DMap() || !KeyState(KeyCode.KC_M))
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
