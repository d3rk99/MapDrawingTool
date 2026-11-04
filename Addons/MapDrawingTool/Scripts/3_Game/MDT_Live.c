// Client side of live updates: assembles the manifest the server relays, downloads image layers and
// the paper map texture once into $profile:MapDrawingTool/cache, and says which are ready to draw.
//
// RestContext.FILE writes nothing in DayZ 1.30, so parts arrive as JSON {"d": [int, ...]} (each int
// is 4 bytes, little-endian), parsed natively and written with FileSerializer.Write(int), which
// writes exactly those 4 bytes. Writing stops after BUDGET_MS each frame.
// Parts used to be hex decoded with Substring: script strings are values, so each word copied the
// whole part, and a 2.4 MB download held the game at a few FPS for 40 s (2026-11-03).

// One per request, holding its download: a manifest that arrives mid-download replaces the queue,
// and the cancelled request's answer must not be written into the next file (2026-11-03: a paper
// texture part ended up in a layer's PAA).
class MDT_PartCallback : RestCallback
{
	ref MDT_Download m_Job;

	void MDT_PartCallback(MDT_Download job)
	{
		m_Job = job;
	}

	override void OnSuccess(string data, int dataSize)
	{
		MDT_Live.OnPart(m_Job, data);
	}

	override void OnError(int errorCode)
	{
		MDT_Live.OnPartFailed(m_Job, "error " + errorCode);
	}

	override void OnTimeout()
	{
		MDT_Live.OnPartFailed(m_Job, "timeout");
	}
}

class MDT_Part
{
	ref array<int> d;
}

class MDT_Download
{
	ref MDT_Layer layer;
	string temp;
	string path;
	int part;
	int tries;
	int started;            // GetGame().GetTime() at the first request
	ref FileSerializer file;
	ref array<int> words;   // current part, being written
	int pos;                // next word in words
	bool waiting;
	bool other;             // another player's paper texture (watching their map), not ours
}

class MDT_Live
{
	static const float BUDGET_MS = 2;            // writing time per frame

	static ref MDT_Manifest s_Manifest;
	static string s_Base;
	static int s_ReadyCount;                     // bumps whenever an image becomes drawable
	static ref map<string, string> s_ImagePaths = new map<string, string>;   // layer id -> cached PAA
	static string s_PaperPath;                   // cached paper map texture, "" until there is one
	static ref map<string, string> s_StampPaths = new map<string, string>;   // player's stamp file name -> cached PAA preview
	static ref array<string> s_Others = new array<string>;   // other players' paper textures in use (kept by CleanCache)

	protected static string s_Version;
	protected static ref array<string> s_Chunks;
	protected static ref array<ref MDT_Download> s_Queue = new array<ref MDT_Download>;
	protected static ref MDT_PartCallback s_Callback;   // the request in flight (kept alive until it answers)
	protected static ref JsonSerializer s_Json = new JsonSerializer();
	protected static bool s_CleanPending;          // tidy the cache once this manifest's downloads are done
	protected static int s_LastTick;
	protected static float s_TicksPerMs;         // TickCount is CPU ticks; measured against frame time

	// One RPC piece of the manifest. All pieces of one version arrive in order.
	static void OnChunk(string version, int index, int count, string text, string base)
	{
		if (index == 0 || version != s_Version || !s_Chunks)
		{
			s_Version = version;
			s_Chunks = new array<string>;
		}
		s_Chunks.Insert(text);
		if (s_Chunks.Count() < count)
			return;
		string json;
		foreach (string piece : s_Chunks)
			json += piece;
		s_Chunks = null;
		Apply(json, base);
	}

	static void Apply(string json, string base)
	{
		MDT_Manifest manifest;
		string error;
		if (!JsonFileLoader<MDT_Manifest>.LoadData(json, manifest, error) || !manifest)
		{
			Print("[MapDrawingTool] bad manifest: " + error);
			return;
		}
		s_Manifest = manifest;
		s_Base = base;
		array<ref MDT_Download> again = new array<ref MDT_Download>;   // not from the manifest: start over
		foreach (MDT_Download dropped : s_Queue)
		{
			if (dropped.file)
				dropped.file.Close();
			DeleteFile(dropped.temp);
			if (dropped.other || dropped.layer.kind == "stamp")
				again.Insert(dropped);
		}
		s_Queue.Clear();
		s_ImagePaths.Clear();
		if (!FileExist(MDT_PROFILE_DIR))
			MakeDirectory(MDT_PROFILE_DIR);
		if (!FileExist(MDT_CACHE_DIR))
			MakeDirectory(MDT_CACHE_DIR);
		foreach (MDT_Layer layer : manifest.layers)
		{
			if (layer.kind != "image" || !layer.rect || layer.rect.Count() != 4)
				continue;
			string path = MDT_CACHE_DIR + "/" + layer.id + "_" + layer.hash + ".paa";
			if (FileExist(path))
			{
				s_ImagePaths.Set(layer.id, path);
				continue;
			}
			if (base != "")
				MDT_Enqueue(layer, path);
		}
		// queued last: the full-screen map's layers are small and come first.
		// A new hash means a new file name, so the engine never shows an old texture it cached.
		MDT_Layer paper = manifest.paper;
		if (paper && paper.parts > 0 && paper.hash != "")
		{
			string paperPath = MDT_CACHE_DIR + "/paper_" + paper.hash + ".paa";
			if (FileExist(paperPath))
				s_PaperPath = paperPath;
			else if (base != "")
				MDT_Enqueue(paper, paperPath);    // the old texture stays on the paper until this one is in
		}
		else
			s_PaperPath = "";
		foreach (MDT_Download redo : again)
		{
			MDT_Enqueue(redo.layer, redo.path);
			s_Queue[s_Queue.Count() - 1].other = redo.other;
		}
		s_ReadyCount++;
		s_CleanPending = true;
		Print(string.Format("[MapDrawingTool] manifest %1: %2 markers, %3 layers, %4 to download", manifest.version, manifest.markers.Count(), manifest.layers.Count(), s_Queue.Count()));
	}

	// Deletes cached files the current manifest does not use: old paper textures (2-10 MB each),
	// revoked layers, unfinished parts. Runs after the downloads, so the texture on the paper is only
	// replaced, never removed. One server's files at a time: another server's map downloads again.
	protected static void CleanCache()
	{
		s_CleanPending = false;
		set<string> keep = new set<string>;
		foreach (string kept : s_ImagePaths)
			keep.Insert(kept);
		if (s_PaperPath != "")
			keep.Insert(s_PaperPath);
		foreach (string other : s_Others)
			keep.Insert(other);
		foreach (MDT_Download busy : s_Queue)
		{
			keep.Insert(busy.path);
			keep.Insert(busy.temp);
		}
		foreach (string stampPath : s_StampPaths)
			keep.Insert(stampPath);
		string name;
		FileAttr attr;
		array<string> drop = new array<string>;
		FindFileHandle h = FindFile(MDT_CACHE_DIR + "/*", name, attr, FindFileFlags.DIRECTORIES);
		if (!h)
			return;
		bool more = true;
		while (more)
		{
			// only our own file types (FileAttr is not a bit set, so it cannot tell folders apart)
			bool ours = name.IndexOf(".paa") == name.Length() - 4 || name.IndexOf(".part") == name.Length() - 5;
			if (name != "" && ours && keep.Find(MDT_CACHE_DIR + "/" + name) < 0)
				drop.Insert(MDT_CACHE_DIR + "/" + name);
			more = FindNextFile(h, name, attr);
		}
		CloseFindFile(h);
		int removed = 0;
		foreach (string old : drop)
		{
			if (DeleteFile(old))
				removed++;
		}
		if (removed > 0)
			Print("[MapDrawingTool] cache: removed " + removed + " old files");
	}

	// Another player's paper texture (MDT_Watch): downloaded after our own work, once.
	static string QueueOther(MDT_Layer paper)
	{
		if (!paper || paper.hash == "" || paper.parts <= 0 || s_Base == "")
			return "";
		string path = MDT_CACHE_DIR + "/paper_" + paper.hash + ".paa";
		if (s_Others.Find(path) < 0)
			s_Others.Insert(path);
		if (FileExist(path))
			return path;
		foreach (MDT_Download queued : s_Queue)
		{
			if (queued.path == path)
				return path;
		}
		MDT_Enqueue(paper, path);
		s_Queue[s_Queue.Count() - 1].other = true;
		return path;
	}

	// MDT_Enqueue, not Queue: Dabs Framework has a class Queue<T>, so Queue(a, b) failed to compile
	// next to it (2026-11-03).
	// The preview of the player's own stamp PNG, made by the web app (MDT_Draft.OnUploaded).
	static void QueueStamp(string name, string preview, int parts)
	{
		if (preview == "" || parts <= 0 || s_Base == "" || !s_Manifest)
			return;
		string path = MDT_CACHE_DIR + "/" + preview + ".paa";
		if (FileExist(path))
		{
			s_StampPaths.Set(name, path);
			MDT_Draft.s_Version++;
			return;
		}
		MDT_Layer layer = new MDT_Layer();
		layer.id = preview;
		layer.kind = "stamp";
		layer.parts = parts;
		layer.hash = name;                          // the file name, for s_StampPaths
		MDT_Enqueue(layer, path);
	}

	protected static void MDT_Enqueue(MDT_Layer layer, string path)
	{
		MDT_Download job = new MDT_Download();
		job.layer = layer;
		job.path = path;
		job.temp = path + ".part";
		s_Queue.Insert(job);
	}

	// Called every frame by MissionGameplay.
	static void Update(float timeslice)
	{
		// int ticks can wrap over a long frame, so only normal frames are measured
		if (s_LastTick != 0 && timeslice > 0 && timeslice < 0.1)
		{
			float measured = TickCount(s_LastTick) / (timeslice * 1000);
			if (measured > 0 && s_TicksPerMs <= 0)
				s_TicksPerMs = measured;
			else if (measured > 0)
				s_TicksPerMs = s_TicksPerMs * 0.9 + measured * 0.1;
		}
		s_LastTick = TickCount(0);
		if (s_Queue.Count() == 0)
		{
			if (s_CleanPending)
				CleanCache();
			return;
		}
		MDT_Download job = s_Queue[0];
		if (job.waiting)
			return;
		if (!job.words)
		{
			Request(job);
			return;
		}
		// until the tick rate is known, a small fixed batch
		float budgetF = s_TicksPerMs * BUDGET_MS;
		int budget = budgetF;
		int start = TickCount(0);
		array<int> words = job.words;
		int count = words.Count();
		int i = job.pos;
		while (i < count)
		{
			int stop = Math.Min(count, i + 256);
			while (i < stop)
			{
				job.file.Write(words[i]);
				i++;
			}
			if (budget <= 0 || TickCount(start) >= budget)
				break;
		}
		job.pos = i;
		if (i < count)
			return;
		job.words = null;
		job.part++;
		if (job.part < job.layer.parts)
			return;
		job.file.Close();
		DeleteFile(job.path);
		if (CopyFile(job.temp, job.path))
		{
			// another player's texture: MDT_Watch sees the file appear
			if (job.layer.kind == "stamp")
			{
				s_StampPaths.Set(job.layer.hash, job.path);
				MDT_Draft.s_Version++;             // placed stamps redraw with the picture
			}
			else if (!job.other && job.layer.kind == "paper")
				s_PaperPath = job.path;
			else if (!job.other)
				s_ImagePaths.Set(job.layer.id, job.path);
			s_ReadyCount++;
			Print(string.Format("[MapDrawingTool] downloaded %1 %2 (%3 bytes, %4 s, %5 ticks/ms)", job.layer.kind, job.layer.id, job.layer.bytes, (GetGame().GetTime() - job.started) / 1000.0, s_TicksPerMs));
		}
		DeleteFile(job.temp);
		s_Queue.RemoveOrdered(0);
	}

	protected static void Request(MDT_Download job)
	{
		if (!job.file)
		{
			job.started = GetGame().GetTime();
			job.file = new FileSerializer();
			if (!job.file.Open(job.temp, FileMode.WRITE))
			{
				Print("[MapDrawingTool] cannot write " + job.temp);
				s_Queue.RemoveOrdered(0);
				return;
			}
		}
		RestApi api = GetRestApi();
		if (!api)
			api = CreateRestApi();
		if (!api)
			return;
		job.waiting = true;
		string mapId = s_Manifest.mapId;
		if (mapId == "")
			mapId = s_Manifest.world;     // web apps from before maps
		string url = "api/" + mapId + "/live/" + job.layer.id + "/" + job.part;
		if (job.layer.query != "")
			url += "?" + job.layer.query;               // personal work: the key only its owner gets
		s_Callback = new MDT_PartCallback(job);
		api.GetRestContext(s_Base).GET(s_Callback, url);
	}

	static void OnPart(MDT_Download job, string data)
	{
		if (s_Queue.Count() == 0 || s_Queue[0] != job)
			return;                                    // cancelled by a newer manifest
		job.waiting = false;
		MDT_Part part;
		string error;
		if (!s_Json.ReadFromString(part, data, error) || !part || !part.d || part.d.Count() == 0)
		{
			OnPartFailed(job, "bad part " + error);
			return;
		}
		job.words = part.d;
		job.pos = 0;
	}

	static void OnPartFailed(MDT_Download job, string why)
	{
		if (s_Queue.Count() == 0 || s_Queue[0] != job)
			return;
		job.waiting = false;
		job.tries++;
		Print("[MapDrawingTool] layer " + job.layer.id + " part " + job.part + " failed: " + why);
		if (job.tries < 3)
			return;
		if (job.file)
			job.file.Close();
		DeleteFile(job.temp);
		s_Queue.RemoveOrdered(0);
	}

	static array<ref MDT_Marker> GetMarkers()
	{
		if (s_Manifest)
			return s_Manifest.markers;
		return null;
	}
}
