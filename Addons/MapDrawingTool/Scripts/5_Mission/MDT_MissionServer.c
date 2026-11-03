// Server side of live updates: polls the web app's manifest (or reads markers.json when no
// liveUrl is set), and relays it to every player on connect and whenever it changes.
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

modded class MissionServer
{
	protected ref MDT_Config m_MDT_Config;
	protected ref MDT_ManifestCallback m_MDT_Callback;
	protected string m_MDT_Manifest;       // JSON as sent to clients
	protected string m_MDT_Version;
	protected string m_MDT_Rejected;       // last manifest refused for being another world's (logged once)
	protected string m_MDT_MarkersText;    // markers.json content last read (fallback mode)

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
		Print("[MapDrawingTool] live url '" + m_MDT_Config.liveUrl + "', map '" + m_MDT_Config.mapId + "', every " + seconds + " s");
		MDT_Poll();
		GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(MDT_Poll, seconds * 1000, true);
	}

	override void InvokeOnConnect(PlayerBase player, PlayerIdentity identity)
	{
		super.InvokeOnConnect(player, identity);
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
		MDT_SetManifest(json, manifest.version);
	}

	protected void MDT_ReadMarkersFile()
	{
		string text = MDT_ReadText(MDT_MARKERS_FILE);
		if (text == m_MDT_MarkersText && m_MDT_Manifest != "")
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
		string json;
		JsonFileLoader<MDT_Manifest>.MakeData(manifest, json, error, false);
		MDT_SetManifest(json, manifest.version);
	}

	protected void MDT_SetManifest(string json, string version)
	{
		if (version == m_MDT_Version)
			return;
		m_MDT_Manifest = json;
		m_MDT_Version = version;
		Print("[MapDrawingTool] manifest " + version + " (" + json.Length() + " chars), sending to players");
		array<Man> players = new array<Man>;
		GetGame().GetPlayers(players);
		foreach (Man man : players)
		{
			PlayerBase pb = PlayerBase.Cast(man);
			if (pb && pb.GetIdentity())
				MDT_Send(pb, pb.GetIdentity());
		}
	}

	protected void MDT_Send(PlayerBase player, PlayerIdentity identity)
	{
		if (!player || !identity || m_MDT_Manifest == "")
			return;
		int len = m_MDT_Manifest.Length();
		int count = Math.Max(1, (len + MDT_RPC_CHUNK - 1) / MDT_RPC_CHUNK);
		for (int i = 0; i < count; i++)
		{
			ScriptRPC rpc = new ScriptRPC();
			rpc.Write(m_MDT_Version);
			rpc.Write(i);
			rpc.Write(count);
			rpc.Write(m_MDT_Manifest.Substring(i * MDT_RPC_CHUNK, Math.Min(MDT_RPC_CHUNK, len - i * MDT_RPC_CHUNK)));
			rpc.Write(m_MDT_Config.liveUrl);
			rpc.Send(player, MDT_RPC_MANIFEST, true, identity);
		}
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
