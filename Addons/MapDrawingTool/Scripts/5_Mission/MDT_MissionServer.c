// Server side of live updates: polls the web app's manifest (or reads markers.json when no
// liveUrl is set), and relays it to every player on connect and whenever it changes. Each player
// gets the global work plus their own personal work (by SteamID), never anyone else's.
// It also relays drawings players make in game (MDT_RPC_SUBMIT) to the web app.
class MDT_ManifestCallback : RestCallback
{
	override void OnSuccess(string data, int dataSize)
	{
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_OnManifest(data);
	}

	override void OnError(int errorCode)
	{
		Print("[MapDrawingTool] live manifest request failed: error " + errorCode);
	}

	override void OnTimeout()
	{
		Print("[MapDrawingTool] live manifest request timed out");
	}
}

// The web app's answer to one player's in-game drawing.
class MDT_SubmitCallback : RestCallback
{
	string m_Uid;

	void MDT_SubmitCallback(string uid)
	{
		m_Uid = uid;
	}

	override void OnSuccess(string data, int dataSize)
	{
		MDT_SubmitResult result;
		string error;
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (!mission)
			return;
		if (!JsonFileLoader<MDT_SubmitResult>.LoadData(data, result, error) || !result)
		{
			mission.MDT_SubmitDone(this, false, "The map web app gave an unexpected answer");
			return;
		}
		if (!result.ok)
		{
			mission.MDT_SubmitDone(this, false, result.error);
			return;
		}
		string text = "Sent for approval";
		if (result.status == "approved")
			text = "Added to your map";
		mission.MDT_SubmitDone(this, true, text);
	}

	override void OnError(int errorCode)
	{
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_SubmitDone(this, false, "The map web app could not be reached (error " + errorCode + ")");
	}

	override void OnTimeout()
	{
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_SubmitDone(this, false, "The map web app did not answer");
	}
}

modded class MissionServer
{
	static const int MDT_SUBMIT_COOLDOWN_MS = 10000;
	static const int MDT_SUBMIT_MAX_POINTS = 3000;

	protected ref MDT_Config m_MDT_Config;
	protected ref MDT_ManifestCallback m_MDT_Callback;
	protected ref MDT_Manifest m_MDT_Global;               // as the web app sent it, personal included
	protected string m_MDT_Version;
	protected string m_MDT_Rejected;       // last manifest refused for being another world's (logged once)
	protected string m_MDT_MarkersText;    // markers.json content last read (fallback mode)
	protected ref map<string, string> m_MDT_Sent = new map<string, string>;               // SteamID -> what it got
	protected ref map<string, ref array<string>> m_MDT_Pieces = new map<string, ref array<string>>;
	protected ref map<string, int> m_MDT_LastSubmit = new map<string, int>;
	protected ref array<ref MDT_SubmitCallback> m_MDT_Waiting = new array<ref MDT_SubmitCallback>;

	override void OnInit()
	{
		super.OnInit();
		if (!FileExist(MDT_PROFILE_DIR))
			MakeDirectory(MDT_PROFILE_DIR);
		string error;
		if (!FileExist(MDT_CONFIG_FILE))
			JsonFileLoader<MDT_Config>.SaveFile(MDT_CONFIG_FILE, new MDT_Config(), error);
		if (!JsonFileLoader<MDT_Config>.LoadFile(MDT_CONFIG_FILE, m_MDT_Config, error) || !m_MDT_Config)
		{
			Print("[MapDrawingTool] config.json not loaded: " + error);
			m_MDT_Config = new MDT_Config();
		}
		if (m_MDT_Config.liveUrl != "" && m_MDT_Config.liveUrl.Substring(m_MDT_Config.liveUrl.Length() - 1, 1) != "/")
			m_MDT_Config.liveUrl += "/";
		if (m_MDT_Config.mapId == "")
		{
			m_MDT_Config.mapId = GetGame().GetWorldName();
			m_MDT_Config.mapId.ToLower();
		}
		int seconds = Math.Max(15, m_MDT_Config.pollSeconds);
		Print("[MapDrawingTool] live url '" + m_MDT_Config.liveUrl + "', map '" + m_MDT_Config.mapId + "', every " + seconds + " s, in-game drawing " + (m_MDT_Config.serverKey != ""));
		MDT_Poll();
		GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(MDT_Poll, seconds * 1000, true);
		MDT_Events.SubmitPiece.Insert(MDT_OnSubmitPiece);
	}

	override void InvokeOnConnect(PlayerBase player, PlayerIdentity identity)
	{
		super.InvokeOnConnect(player, identity);
		if (identity)
			m_MDT_Sent.Remove(identity.GetPlainId());      // a fresh client has nothing yet
		MDT_Send(player, identity);
	}

	protected void MDT_Poll()
	{
		if (m_MDT_Config.liveUrl == "")
		{
			MDT_ReadMarkersFile();
			return;
		}
		RestApi api = GetRestApi();
		if (!api)
			api = CreateRestApi();
		if (!api)
			return;
		if (!m_MDT_Callback)
			m_MDT_Callback = new MDT_ManifestCallback();
		api.GetRestContext(m_MDT_Config.liveUrl).GET(m_MDT_Callback, "api/" + m_MDT_Config.mapId + "/live");
	}

	void MDT_OnManifest(string json)
	{
		MDT_Manifest manifest;
		string error;
		if (!JsonFileLoader<MDT_Manifest>.LoadData(json, manifest, error) || !manifest)
		{
			Print("[MapDrawingTool] bad live manifest: " + error);
			return;
		}
		// a map set up for another world would put its drawings in the wrong places
		string world = GetGame().GetWorldName();
		world.ToLower();
		if (manifest.world != world)
		{
			if (manifest.version != m_MDT_Rejected)
				Print("[MapDrawingTool] map '" + m_MDT_Config.mapId + "' is for " + manifest.world + ", this server runs " + world + "; not shown");
			m_MDT_Rejected = manifest.version;
			return;
		}
		MDT_SetManifest(manifest, json.Length());
	}

	protected void MDT_ReadMarkersFile()
	{
		string text = MDT_ReadText(MDT_MARKERS_FILE);
		if (text == m_MDT_MarkersText && m_MDT_Global)
			return;
		m_MDT_MarkersText = text;
		MDT_Manifest manifest = new MDT_Manifest();
		MDT_MarkerFile file;
		string error;
		if (text != "" && JsonFileLoader<MDT_MarkerFile>.LoadData(text, file, error) && file && file.markers)
			manifest.markers = file.markers;
		else if (error != "")
			Print("[MapDrawingTool] markers.json not loaded: " + error);
		string world = GetGame().GetWorldName();
		world.ToLower();
		manifest.world = world;
		manifest.version = "file-" + text.Hash();
		MDT_SetManifest(manifest, text.Length());
	}

	protected void MDT_SetManifest(MDT_Manifest manifest, int size)
	{
		if (manifest.version == m_MDT_Version)
			return;
		m_MDT_Global = manifest;
		m_MDT_Version = manifest.version;
		int personal = 0;
		if (manifest.personal)
			personal = manifest.personal.Count();
		Print(string.Format("[MapDrawingTool] manifest %1 (%2 chars, %3 players with personal work), sending to players", manifest.version, size, personal));
		array<Man> players = new array<Man>;
		GetGame().GetPlayers(players);
		foreach (Man man : players)
		{
			PlayerBase pb = PlayerBase.Cast(man);
			if (pb && pb.GetIdentity())
				MDT_Send(pb, pb.GetIdentity());
		}
	}

	// The manifest one player gets: global work plus their own personal work, as JSON.
	protected string MDT_JsonFor(string uid)
	{
		MDT_Manifest g = m_MDT_Global;
		MDT_Manifest mine = new MDT_Manifest();
		mine.mapId = g.mapId;
		mine.world = g.world;
		mine.options = g.options;
		mine.paper = g.paper;
		MDT_AddWork(mine, g.markers, g.layers);
		if (g.personal)
		{
			foreach (MDT_Personal p : g.personal)
			{
				if (p.owner != uid)
					continue;
				MDT_AddWork(mine, p.markers, p.layers);
				if (p.paper)
					mine.paper = p.paper;            // global + personal texture
			}
		}
		mine.version = "";
		string json, error;
		JsonFileLoader<MDT_Manifest>.MakeData(mine, json, error, false);
		return json;
	}

	// (InsertAll does not take arrays of ref items)
	protected void MDT_AddWork(MDT_Manifest into, array<ref MDT_Marker> markers, array<ref MDT_Layer> layers)
	{
		if (markers)
		{
			foreach (MDT_Marker marker : markers)
				into.markers.Insert(marker);
		}
		if (layers)
		{
			foreach (MDT_Layer layer : layers)
				into.layers.Insert(layer);
		}
	}

	protected void MDT_Send(PlayerBase player, PlayerIdentity identity)
	{
		if (!player || !identity || !m_MDT_Global)
			return;
		string uid = identity.GetPlainId();
		string json = MDT_JsonFor(uid);
		string version = json.Hash().ToString();
		string sent;
		if (m_MDT_Sent.Find(uid, sent) && sent == version)
			return;                                         // nothing changed for this player
		m_MDT_Sent.Set(uid, version);
		int len = json.Length();
		int count = Math.Max(1, (len + MDT_RPC_CHUNK - 1) / MDT_RPC_CHUNK);
		for (int i = 0; i < count; i++)
		{
			ScriptRPC rpc = new ScriptRPC();
			rpc.Write(version);
			rpc.Write(i);
			rpc.Write(count);
			rpc.Write(json.Substring(i * MDT_RPC_CHUNK, Math.Min(MDT_RPC_CHUNK, len - i * MDT_RPC_CHUNK)));
			rpc.Write(m_MDT_Config.liveUrl);
			rpc.Send(player, MDT_RPC_MANIFEST, true, identity);
		}
	}

	// ---------------------------------------------------------------- drawings made in game

	// One piece of a player's drawing (PlayerBase.OnRPC). Pieces of one drawing arrive in order.
	void MDT_OnSubmitPiece(Man man, PlayerIdentity sender, int index, int count, string text)
	{
		PlayerBase player = PlayerBase.Cast(man);
		if (!player || !sender || count < 1 || count > 200 || index < 0 || index >= count)
			return;
		string uid = sender.GetPlainId();
		array<string> pieces;
		if (index == 0 || !m_MDT_Pieces.Find(uid, pieces))
		{
			pieces = new array<string>;
			m_MDT_Pieces.Set(uid, pieces);
		}
		pieces.Insert(text);
		if (pieces.Count() < count)
			return;
		m_MDT_Pieces.Remove(uid);
		string json;
		foreach (string piece : pieces)
			json += piece;

		if (m_MDT_Config.liveUrl == "" || m_MDT_Config.serverKey == "")
		{
			MDT_Reply(player, sender, false, "This server is not set up for in-game drawing (serverKey)");
			return;
		}
		int now = GetGame().GetTime();
		int last;
		if (m_MDT_LastSubmit.Find(uid, last) && now - last < MDT_SUBMIT_COOLDOWN_MS)
		{
			MDT_Reply(player, sender, false, "Wait a few seconds before sending another drawing");
			return;
		}
		MDT_Submit submit;
		string error;
		if (!JsonFileLoader<MDT_Submit>.LoadData(json, submit, error) || !submit || !submit.strokes || submit.strokes.Count() == 0)
		{
			MDT_Reply(player, sender, false, "The drawing could not be read");
			return;
		}
		int points = 0;
		foreach (MDT_Stroke s : submit.strokes)
		{
			if (s.points)
				points += s.points.Count() / 2;
		}
		if (points > MDT_SUBMIT_MAX_POINTS)
		{
			MDT_Reply(player, sender, false, "The drawing is too big; send it in parts");
			return;
		}
		m_MDT_LastSubmit.Set(uid, now);
		// the server vouches for who drew it: the web app trusts serverKey, not the client
		submit.serverKey = m_MDT_Config.serverKey;
		submit.steamId = uid;
		submit.name = sender.GetName();
		string body;
		JsonFileLoader<MDT_Submit>.MakeData(submit, body, error, false);

		RestApi api = GetRestApi();
		if (!api)
			api = CreateRestApi();
		if (!api)
		{
			MDT_Reply(player, sender, false, "The server cannot reach the map web app");
			return;
		}
		MDT_SubmitCallback cb = new MDT_SubmitCallback(uid);
		m_MDT_Waiting.Insert(cb);
		RestContext ctx = api.GetRestContext(m_MDT_Config.liveUrl);
		ctx.SetHeader("application/json");
		ctx.POST(cb, "api/" + m_MDT_Config.mapId + "/ingame", body);
		Print(string.Format("[MapDrawingTool] in-game drawing from %1: %2 strokes, %3 points, %4", sender.GetName(), submit.strokes.Count(), points, submit.scope));
	}

	void MDT_SubmitDone(MDT_SubmitCallback cb, bool ok, string message)
	{
		string uid = cb.m_Uid;
		m_MDT_Waiting.RemoveItem(cb);
		Print("[MapDrawingTool] in-game drawing from " + uid + ": " + message);
		array<Man> players = new array<Man>;
		GetGame().GetPlayers(players);
		foreach (Man man : players)
		{
			PlayerBase pb = PlayerBase.Cast(man);
			if (pb && pb.GetIdentity() && pb.GetIdentity().GetPlainId() == uid)
				MDT_Reply(pb, pb.GetIdentity(), ok, message);
		}
		if (ok)
			MDT_Poll();                                     // personal work without approval shows at once
	}

	protected void MDT_Reply(PlayerBase player, PlayerIdentity identity, bool ok, string message)
	{
		ScriptRPC rpc = new ScriptRPC();
		rpc.Write(ok);
		rpc.Write(message);
		rpc.Send(player, MDT_RPC_RESULT, true, identity);
	}

	protected string MDT_ReadText(string path)
	{
		if (!FileExist(path))
			return "";
		FileHandle f = OpenFile(path, FileMode.READ);
		if (!f)
			return "";
		string text, line;
		while (FGets(f, line) >= 0)
			text += line + "\n";
		CloseFile(f);
		return text;
	}
}
