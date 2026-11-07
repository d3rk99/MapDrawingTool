// Server side of live updates: polls the web app's manifest (or reads markers.json when no
// liveUrl is set), and relays it to every player on connect and whenever it changes. Each player
// gets the global work plus their own personal work (by SteamID), never anyone else's.
// It also relays drawings players make in game (MDT_RPC_SUBMIT) to the web app.
class MDT_ManifestCallback : RestCallback
{
	override void OnSuccess(string data, int dataSize)
	{
		if (!MDT_Rest.Answer(this))
			return;
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_OnManifest(data);
	}

	override void OnError(int errorCode)
	{
		if (!MDT_Rest.Answer(this))
			return;
		Print("[MapDrawingTool] live manifest request failed: error " + errorCode);
	}

	override void OnTimeout()
	{
		if (!MDT_Rest.Answer(this))
			return;
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
		if (!MDT_Rest.Answer(this))
			return;
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
		if (!MDT_Rest.Answer(this))
			return;
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_SubmitDone(this, false, "The map web app could not be reached (error " + errorCode + ")");
	}

	override void OnTimeout()
	{
		if (!MDT_Rest.Answer(this))
			return;
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_SubmitDone(this, false, "The map web app did not answer");
	}
}

// The web app's answer about one player's list (MDT_RPC_MINE_ASK) or a removal (MDT_RPC_WITHDRAW).
class MDT_MineCallback : RestCallback
{
	string m_Uid;
	bool m_Withdraw;

	void MDT_MineCallback(string uid, bool withdraw)
	{
		m_Uid = uid;
		m_Withdraw = withdraw;
	}

	override void OnSuccess(string data, int dataSize)
	{
		if (!MDT_Rest.Answer(this))
			return;
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_MineDone(this, data);
	}

	override void OnError(int errorCode)
	{
		if (!MDT_Rest.Answer(this))
			return;
		Failed();
	}

	void Failed()
	{
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_MineDone(this, "");
	}

	override void OnTimeout()
	{
		if (!MDT_Rest.Answer(this))
			return;
		Failed();
	}
}

// Online players' personal versions (live/players) or one player's personal work (live/player).
class MDT_PersonalCallback : RestCallback
{
	string m_Uid;                  // "" = the versions of everyone online

	void MDT_PersonalCallback(string uid)
	{
		m_Uid = uid;
	}

	override void OnSuccess(string data, int dataSize)
	{
		if (!MDT_Rest.Answer(this))
			return;
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_PersonalDone(this, data);
	}

	override void OnError(int errorCode)
	{
		if (!MDT_Rest.Answer(this))
			return;
		Failed();
	}

	void Failed()
	{
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_PersonalDone(this, "");
	}

	override void OnTimeout()
	{
		if (!MDT_Rest.Answer(this))
			return;
		Failed();
	}
}

// The web app's answer to a stamp PNG upload.
class MDT_UploadCallback : RestCallback
{
	string m_Uid;
	string m_Name;

	void MDT_UploadCallback(string uid, string name)
	{
		m_Uid = uid;
		m_Name = name;
	}

	override void OnSuccess(string data, int dataSize)
	{
		if (!MDT_Rest.Answer(this))
			return;
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_UploadDone(this, data);
	}

	override void OnError(int errorCode)
	{
		if (!MDT_Rest.Answer(this))
			return;
		Failed();
	}

	void Failed()
	{
		MissionServer mission = MissionServer.Cast(GetGame().GetMission());
		if (mission)
			mission.MDT_UploadDone(this, "");
	}

	override void OnTimeout()
	{
		if (!MDT_Rest.Answer(this))
			return;
		Failed();
	}
}

modded class MissionServer
{
	static const int MDT_SUBMIT_COOLDOWN_MS = 10000;
	static const int MDT_SUBMIT_MAX_POINTS = 3000;

	protected ref MDT_Config m_MDT_Config;
	protected string m_MDT_Url;            // what this server talks to: lanUrl if set, else liveUrl
	protected ref MDT_Manifest m_MDT_Global;               // everyone's work, as the web app sent it
	// Personal work only of players who are online, fetched when its version changes (live/players):
	// a poll stays small however many players ever drew something.
	protected ref map<string, ref MDT_Personal> m_MDT_Personal = new map<string, ref MDT_Personal>;
	protected ref map<string, string> m_MDT_PersonalVer = new map<string, string>;
	protected ref array<ref MDT_PersonalCallback> m_MDT_PersonalWaiting = new array<ref MDT_PersonalCallback>;
	protected ref map<string, ref MDT_Upload> m_MDT_UpPieces = new map<string, ref MDT_Upload>;   // SteamID -> upload being received
	protected ref map<string, int> m_MDT_UpNext = new map<string, int>;
	protected ref array<ref MDT_UploadCallback> m_MDT_UpWaiting = new array<ref MDT_UploadCallback>;
	protected string m_MDT_Version;
	protected string m_MDT_Rejected;       // last manifest refused for being another world's (logged once)
	protected string m_MDT_MarkersText;    // markers.json content last read (fallback mode)
	protected ref map<string, string> m_MDT_Sent = new map<string, string>;               // SteamID -> what it got
	protected ref map<string, ref array<string>> m_MDT_Pieces = new map<string, ref array<string>>;
	protected ref map<string, int> m_MDT_LastSubmit = new map<string, int>;
	protected ref array<ref MDT_SubmitCallback> m_MDT_Waiting = new array<ref MDT_SubmitCallback>;
	protected ref array<ref MDT_MineCallback> m_MDT_MineWaiting = new array<ref MDT_MineCallback>;

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
		// a field missing from an older config.json loads as false, not as its default
		FileHandle cfgFile = OpenFile(MDT_CONFIG_FILE, FileMode.READ);
		if (cfgFile)
		{
			string cfgLine, cfgText;
			while (FGets(cfgFile, cfgLine) >= 0)
				cfgText += cfgLine;
			CloseFile(cfgFile);
			if (!cfgText.Contains("paperTexture"))
				m_MDT_Config.paperTexture = true;
		}
		m_MDT_Config.liveUrl = MDT_Slash(m_MDT_Config.liveUrl);
		m_MDT_Config.lanUrl = MDT_Slash(m_MDT_Config.lanUrl);
		JsonFileLoader<MDT_Config>.SaveFile(MDT_CONFIG_FILE, m_MDT_Config, error);   // shows fields added since
		m_MDT_Url = m_MDT_Config.lanUrl;
		if (m_MDT_Url == "")
			m_MDT_Url = m_MDT_Config.liveUrl;
		if (m_MDT_Config.mapId == "")
		{
			m_MDT_Config.mapId = GetGame().GetWorldName();
			m_MDT_Config.mapId.ToLower();
		}
		int seconds = Math.Max(15, m_MDT_Config.pollSeconds);
		Print("[MapDrawingTool] live url '" + m_MDT_Config.liveUrl + "', lan url '" + m_MDT_Config.lanUrl + "', map '" + m_MDT_Config.mapId + "', every " + seconds + " s, in-game drawing " + (m_MDT_Config.serverKey != "") + ", paper texture " + m_MDT_Config.paperTexture);
		MDT_Poll();
		GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(MDT_Poll, seconds * 1000, true);
		MDT_Events.SubmitPiece.Insert(MDT_OnSubmitPiece);
		MDT_Events.MineAsk.Insert(MDT_OnMineAsk);
		MDT_Events.Withdraw.Insert(MDT_OnWithdraw);
		MDT_Events.PaperAsk.Insert(MDT_OnPaperAsk);
		MDT_Events.UploadPiece.Insert(MDT_OnUploadPiece);
	}

	override void InvokeOnConnect(PlayerBase player, PlayerIdentity identity)
	{
		super.InvokeOnConnect(player, identity);
		if (identity)
			m_MDT_Sent.Remove(identity.GetPlainId());      // a fresh client has nothing yet
		MDT_Send(player, identity);
		GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(MDT_PollPlayers, 3000, false);   // their personal work
	}

	protected static string MDT_Slash(string url)
	{
		url.TrimInPlace();
		if (url != "" && url.Substring(url.Length() - 1, 1) != "/")
			url += "/";
		return url;
	}

	protected void MDT_Poll()
	{
		if (m_MDT_Url == "")
		{
			MDT_ReadMarkersFile();
			return;
		}
		RestApi api = MDT_Rest.Api();
		if (!api)
			return;
		MDT_ManifestCallback cb = new MDT_ManifestCallback();   // one per request (MDT_Rest keeps it)
		MDT_Rest.Keep(cb);
		api.GetRestContext(m_MDT_Url).GET(cb, "api/" + m_MDT_Config.mapId + "/live");
		MDT_PollPlayers();
	}

	// Asks which online players have personal work and its version; fetches what changed.
	protected void MDT_PollPlayers()
	{
		if (m_MDT_Url == "" || m_MDT_Config.serverKey == "")
			return;
		MDT_PlayersAsk ask = new MDT_PlayersAsk();
		ask.serverKey = m_MDT_Config.serverKey;
		array<Man> players = new array<Man>;
		GetGame().GetPlayers(players);
		foreach (Man man : players)
		{
			if (man.GetIdentity())
				ask.players.Insert(man.GetIdentity().GetPlainId());
		}
		if (ask.players.Count() == 0)
			return;
		MDT_PostPersonal("/live/players", "", ask);
	}

	protected void MDT_PostPersonal(string path, string uid, Class body)
	{
		RestApi api = MDT_Rest.Api();
		if (!api)
			return;
		string json, error;
		if (uid == "")
			JsonFileLoader<MDT_PlayersAsk>.MakeData(MDT_PlayersAsk.Cast(body), json, error, false);
		else
			JsonFileLoader<MDT_PlayerAsk>.MakeData(MDT_PlayerAsk.Cast(body), json, error, false);
		MDT_PersonalCallback cb = new MDT_PersonalCallback(uid);
		m_MDT_PersonalWaiting.Insert(cb);
		RestContext ctx = api.GetRestContext(m_MDT_Url);
		ctx.SetHeader("application/json");
		MDT_Rest.Keep(cb);
		ctx.POST(cb, "api/" + m_MDT_Config.mapId + path, json);
	}

	void MDT_PersonalDone(MDT_PersonalCallback cb, string data)
	{
		m_MDT_PersonalWaiting.RemoveItem(cb);
		if (data == "")
			return;
		string error;
		if (cb.m_Uid == "")
		{
			MDT_Versions answer;
			if (!JsonFileLoader<MDT_Versions>.LoadData(data, answer, error) || !answer || !answer.ok)
				return;
			array<Man> players = new array<Man>;
			GetGame().GetPlayers(players);
			foreach (Man man : players)
			{
				if (!man.GetIdentity())
					continue;
				string uid = man.GetIdentity().GetPlainId();
				string now, had;
				if (answer.versions)
					answer.versions.Find(uid, now);
				m_MDT_PersonalVer.Find(uid, had);
				if (now == had)
					continue;
				if (now == "")
				{
					// their last personal work was removed or ran out
					m_MDT_Personal.Remove(uid);
					m_MDT_PersonalVer.Remove(uid);
					MDT_Send(PlayerBase.Cast(man), man.GetIdentity());
					continue;
				}
				MDT_PlayerAsk ask = new MDT_PlayerAsk();
				ask.serverKey = m_MDT_Config.serverKey;
				ask.steamId = uid;
				MDT_PostPersonal("/live/player", uid, ask);
			}
			return;
		}
		MDT_PersonalAnswer mine;
		if (!JsonFileLoader<MDT_PersonalAnswer>.LoadData(data, mine, error) || !mine || !mine.ok)
			return;
		m_MDT_Personal.Set(cb.m_Uid, mine);
		m_MDT_PersonalVer.Set(cb.m_Uid, mine.version);
		PlayerBase player;
		PlayerIdentity identity;
		if (MDT_FindPlayer(cb.m_Uid, player, identity))
			MDT_Send(player, identity);
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
		Print(string.Format("[MapDrawingTool] manifest %1 (%2 chars, %3 online players with personal work), sending to players", manifest.version, size, m_MDT_Personal.Count()));
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
		if (m_MDT_Config.paperTexture)
			mine.paper = g.paper;
		MDT_AddWork(mine, g.markers, g.layers);
		MDT_Personal p;
		if (m_MDT_Personal.Find(uid, p))
		{
			MDT_AddWork(mine, p.markers, p.layers);
			if (p.paper && m_MDT_Config.paperTexture)
				mine.paper = p.paper;                // global + personal texture
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
			rpc.Write(m_MDT_Config.lanUrl);                 // the player's game picks one (MDT_Live.PickBase)
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

		if (m_MDT_Url == "" || m_MDT_Config.serverKey == "")
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
		if (!JsonFileLoader<MDT_Submit>.LoadData(json, submit, error) || !submit)
		{
			MDT_Reply(player, sender, false, "The drawing could not be read");
			return;
		}
		int points = 0;
		int items = 0;
		if (submit.strokes)
		{
			items += submit.strokes.Count();
			foreach (MDT_Stroke s : submit.strokes)
			{
				if (s.points)
					points += s.points.Count() / 2;
			}
		}
		if (submit.texts)
			items += submit.texts.Count();
		if (submit.stamps)
			items += submit.stamps.Count();
		if (submit.markers)
			items += submit.markers.Count();
		if (items == 0)
		{
			MDT_Reply(player, sender, false, "Nothing drawn");
			return;
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

		RestApi api = MDT_Rest.Api();
		if (!api)
		{
			MDT_Reply(player, sender, false, "The server cannot reach the map web app");
			return;
		}
		MDT_SubmitCallback cb = new MDT_SubmitCallback(uid);
		m_MDT_Waiting.Insert(cb);
		RestContext ctx = api.GetRestContext(m_MDT_Url);
		ctx.SetHeader("application/json");
		MDT_Rest.Keep(cb);
		ctx.POST(cb, "api/" + m_MDT_Config.mapId + "/ingame", body);
		Print(string.Format("[MapDrawingTool] in-game drawing from %1: %2 items, %3 points, %4", sender.GetName(), items, points, submit.scope));
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

	// A player looks at the map another player holds: tell them which texture is on it (the holder's
	// own: global + personal; or the global one). Only the texture's download entry is sent.
	void MDT_OnPaperAsk(Man holderMan, PlayerIdentity viewer)
	{
		PlayerBase holder = PlayerBase.Cast(holderMan);
		if (!holder || !viewer || !holder.GetIdentity() || !m_MDT_Global)
			return;
		MDT_Layer paper = m_MDT_Global.paper;
		MDT_Personal p;
		if (m_MDT_Personal.Find(holder.GetIdentity().GetPlainId(), p) && p.paper)
			paper = p.paper;
		string json, error;
		if (paper && m_MDT_Config.paperTexture)
			JsonFileLoader<MDT_Layer>.MakeData(paper, json, error, false);
		ScriptRPC rpc = new ScriptRPC();
		rpc.Write(json);
		rpc.Send(holder, MDT_RPC_PAPER, true, viewer);
	}

	// A piece of a stamp PNG from the player's folder (raw ints); the whole file goes to the web app.
	void MDT_OnUploadPiece(Man man, PlayerIdentity sender, int index, int count, string name, int bytes, array<int> words)
	{
		if (!sender || !words)
			return;
		string uid = sender.GetPlainId();
		int maxPieces = (MDT_UPLOAD_MAX_WORDS + MDT_UPLOAD_PIECE - 1) / MDT_UPLOAD_PIECE;
		if (count < 1 || count > maxPieces || index < 0 || index >= count || words.Count() > MDT_UPLOAD_PIECE)
		{
			MDT_UploadReply(uid, name, false, "", "", 0, "the stamp arrived damaged");
			return;
		}
		MDT_Upload up;
		int next;
		if (index == 0)
		{
			up = new MDT_Upload();
			up.name = name;
			up.bytes = bytes;
			m_MDT_UpPieces.Set(uid, up);
			m_MDT_UpNext.Set(uid, 0);
		}
		if (!m_MDT_UpPieces.Find(uid, up) || !m_MDT_UpNext.Find(uid, next) || next != index || up.name != name)
		{
			m_MDT_UpPieces.Remove(uid);
			m_MDT_UpNext.Remove(uid);
			MDT_UploadReply(uid, name, false, "", "", 0, "pieces of the stamp went missing, try again");
			return;
		}
		foreach (int word : words)
			up.words.Insert(word);
		m_MDT_UpNext.Set(uid, index + 1);
		if (index + 1 < count)
			return;
		m_MDT_UpPieces.Remove(uid);
		m_MDT_UpNext.Remove(uid);
		Print(string.Format("[MapDrawingTool] stamp %1 from %2: %3 bytes received, sending to the web app", name, uid, up.bytes));
		if (m_MDT_Url == "" || m_MDT_Config.serverKey == "")
		{
			MDT_UploadReply(uid, name, false, "", "", 0, "this server has no map web app set up");
			return;
		}
		up.serverKey = m_MDT_Config.serverKey;
		up.steamId = uid;
		string body, error;
		JsonFileLoader<MDT_Upload>.MakeData(up, body, error, false);
		RestApi api = MDT_Rest.Api();
		if (!api)
		{
			MDT_UploadReply(uid, name, false, "", "", 0, "the server cannot reach the map web app");
			return;
		}
		MDT_UploadCallback cb = new MDT_UploadCallback(uid, up.name);
		m_MDT_UpWaiting.Insert(cb);
		RestContext ctx = api.GetRestContext(m_MDT_Url);
		ctx.SetHeader("application/json");
		MDT_Rest.Keep(cb);
		ctx.POST(cb, "api/" + m_MDT_Config.mapId + "/ingame/upload", body);
	}

	void MDT_UploadReply(string uid, string name, bool ok, string id, string preview, int parts, string why)
	{
		PlayerBase player;
		PlayerIdentity identity;
		if (!MDT_FindPlayer(uid, player, identity))
			return;
		ScriptRPC rpc = new ScriptRPC();
		rpc.Write(name);
		rpc.Write(ok);
		rpc.Write(id);
		rpc.Write(why);
		rpc.Write(preview);
		rpc.Write(parts);
		rpc.Send(player, MDT_RPC_UPLOADED, true, identity);
	}

	void MDT_UploadDone(MDT_UploadCallback cb, string data)
	{
		m_MDT_UpWaiting.RemoveItem(cb);
		MDT_UploadResult result;
		string error;
		bool ok = data != "" && JsonFileLoader<MDT_UploadResult>.LoadData(data, result, error) && result && result.ok;
		string why = "the map web app could not be reached";
		if (result && result.error != "")
			why = result.error;
		string id, preview;
		int parts;
		if (ok)
		{
			id = result.id;
			preview = result.preview;
			parts = result.parts;
		}
		Print(string.Format("[MapDrawingTool] stamp %1: web app ok %2 (%3)", cb.m_Name, ok, why));
		MDT_UploadReply(cb.m_Uid, cb.m_Name, ok, id, preview, parts, why);
	}

	// ---------------------------------------------------------------- a player's own submissions

	void MDT_OnMineAsk(Man man, PlayerIdentity sender)
	{
		if (sender)
			MDT_AskWebApp(sender.GetPlainId(), "", false);
	}

	void MDT_OnWithdraw(Man man, PlayerIdentity sender, string id)
	{
		if (sender && id != "")
			MDT_AskWebApp(sender.GetPlainId(), id, true);
	}

	protected void MDT_AskWebApp(string uid, string id, bool withdraw)
	{
		if (m_MDT_Url == "" || m_MDT_Config.serverKey == "")
		{
			MDT_ReplyTo(uid, false, "This server is not set up for in-game drawing (serverKey)");
			return;
		}
		RestApi api = MDT_Rest.Api();
		if (!api)
			return;
		MDT_PlayerAsk ask = new MDT_PlayerAsk();
		ask.serverKey = m_MDT_Config.serverKey;
		ask.steamId = uid;
		ask.id = id;
		string body, error;
		JsonFileLoader<MDT_PlayerAsk>.MakeData(ask, body, error, false);
		MDT_MineCallback cb = new MDT_MineCallback(uid, withdraw);
		m_MDT_MineWaiting.Insert(cb);
		RestContext ctx = api.GetRestContext(m_MDT_Url);
		ctx.SetHeader("application/json");
		string path = "/ingame/mine";
		if (withdraw)
			path = "/ingame/withdraw";
		MDT_Rest.Keep(cb);
		ctx.POST(cb, "api/" + m_MDT_Config.mapId + path, body);
	}

	void MDT_MineDone(MDT_MineCallback cb, string data)
	{
		m_MDT_MineWaiting.RemoveItem(cb);
		if (data == "")
		{
			MDT_ReplyTo(cb.m_Uid, false, "The map web app could not be reached");
			return;
		}
		if (cb.m_Withdraw)
		{
			MDT_SubmitResult result;
			string error;
			if (!JsonFileLoader<MDT_SubmitResult>.LoadData(data, result, error) || !result || !result.ok)
			{
				string why = "Could not remove it";
				if (result && result.error != "")
					why = result.error;
				MDT_ReplyTo(cb.m_Uid, false, why);
				return;
			}
			MDT_ReplyTo(cb.m_Uid, true, "Removed");
			MDT_Poll();                                      // it leaves everyone's map
			MDT_AskWebApp(cb.m_Uid, "", false);              // and the player's list
			return;
		}
		// the list goes to the player as the web app wrote it
		PlayerBase player;
		PlayerIdentity identity;
		if (!MDT_FindPlayer(cb.m_Uid, player, identity))
			return;
		int len = data.Length();
		int count = Math.Max(1, (len + MDT_RPC_CHUNK - 1) / MDT_RPC_CHUNK);
		if (count > 500)
			return;
		for (int i = 0; i < count; i++)
		{
			ScriptRPC rpc = new ScriptRPC();
			rpc.Write(i);
			rpc.Write(count);
			rpc.Write(data.Substring(i * MDT_RPC_CHUNK, Math.Min(MDT_RPC_CHUNK, len - i * MDT_RPC_CHUNK)));
			rpc.Send(player, MDT_RPC_MINE, true, identity);
		}
	}

	protected bool MDT_FindPlayer(string uid, out PlayerBase player, out PlayerIdentity identity)
	{
		array<Man> players = new array<Man>;
		GetGame().GetPlayers(players);
		foreach (Man man : players)
		{
			PlayerBase pb = PlayerBase.Cast(man);
			if (pb && pb.GetIdentity() && pb.GetIdentity().GetPlainId() == uid)
			{
				player = pb;
				identity = pb.GetIdentity();
				return true;
			}
		}
		return false;
	}

	protected void MDT_ReplyTo(string uid, bool ok, string message)
	{
		PlayerBase player;
		PlayerIdentity identity;
		if (MDT_FindPlayer(uid, player, identity))
			MDT_Reply(player, identity, ok, message);
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
