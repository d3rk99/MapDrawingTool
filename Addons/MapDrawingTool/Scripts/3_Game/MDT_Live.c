// Client side of live updates: assembles the manifest the server relays, downloads image layers and
// the paper map texture once into $profile:MapDrawingTool/cache, and says which are ready to draw.
//
// RestContext.FILE writes nothing in DayZ 1.30, so parts arrive as JSON {"d": [int, ...]} (each int
// is 4 bytes, little-endian), parsed natively and written with FileSerializer.Write(int), which
// writes exactly those 4 bytes. Writing stops after BUDGET_MS each frame.
// Parts used to be hex decoded with Substring: script strings are values, so each word copied the
// whole part, and a 2.4 MB download held the game at a few FPS for 40 s (2026-11-03).

// One per request, holding its download. MDT_Rest keeps it alive until well after the engine
// answers (freeing it while in flight crashed the game, 2026-11-06). m_Seq ties the answer to the
// request: a manifest that arrives mid-download replaces the queue, a retry or the watchdog sends
// the part again, and a late answer must not be written into the file (2026-11-03: a paper texture
// part ended up in a layer's PAA).
class MDT_PartCallback : RestCallback
{
	ref MDT_Download m_Job;
	int m_Seq;

	void MDT_PartCallback(MDT_Download job)
	{
		m_Job = job;
		m_Seq = job.seq;
	}

	override void OnSuccess(string data, int dataSize)
	{
		if (!MDT_Rest.Answer(this))
			return;
		MDT_Download job = m_Job;
		m_Job = null;
		MDT_Live.OnPart(job, m_Seq, data);
	}

	override void OnError(int errorCode)
	{
		if (!MDT_Rest.Answer(this))
			return;
		MDT_Download job = m_Job;
		m_Job = null;
		MDT_Live.OnPartFailed(job, m_Seq, "error " + errorCode);
	}

	override void OnTimeout()
	{
		if (!MDT_Rest.Answer(this))
			return;
		MDT_Download job = m_Job;
		m_Job = null;
		MDT_Live.OnPartFailed(job, m_Seq, "timeout");
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
	int seq;                // bumps with every request; answers to older ones are ignored
	float sentAt;           // GetTickTime of the request in flight
	float retryAt;          // after a failure: not before this GetTickTime
	int started;            // GetGame().GetTime() at the first request
	int written;            // words written so far
	int first;              // the file's first word (PAA type), for the check
	int pauses;             // times it went to the back of the queue after 6 failed tries
	bool redone;            // downloaded again once after a failed check
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
	protected static string s_Picked;           // last address PickBase chose (logged when it changes)
	static int s_ReadyCount;                     // bumps whenever an image becomes drawable
	static ref map<string, string> s_ImagePaths = new map<string, string>;   // layer id -> cached PAA
	static string s_PaperPath;                   // cached paper map texture, "" until there is one
	static ref map<string, string> s_StampPaths = new map<string, string>;   // player's stamp file name -> cached PAA preview
	static ref array<string> s_Others = new array<string>;   // other players' paper textures in use (kept by CleanCache)

	protected static string s_Version;
	protected static ref array<string> s_Chunks;
	protected static ref array<ref MDT_Download> s_Queue = new array<ref MDT_Download>;
	protected static ref JsonSerializer s_Json = new JsonSerializer();
	protected static ref array<ref MDT_Download> s_Aside;   // during Apply: the old queue
	protected static bool s_PaperWanted;
	protected static bool s_PaperBusy;              // MDT_ClientConfig.BUSY_FILE written          // a paper map was in hand: download its texture
	protected static bool s_CleanPending;          // tidy the cache once this manifest's downloads are done
	protected static int s_LastTick;
	protected static float s_TicksPerMs;         // TickCount is CPU ticks; measured against frame time

	// Private / local addresses (RFC 1918 + loopback), as DriveLink's DL_Net.IsLocalAddress.
	static bool IsLocalAddress(string a)
	{
		if (a == "" || a == "localhost" || a.IndexOf("127.") == 0 || a.IndexOf("10.") == 0 || a.IndexOf("192.168.") == 0)
			return true;
		if (a.IndexOf("172.") == 0)
		{
			TStringArray parts = new TStringArray;
			a.Split(".", parts);
			if (parts.Count() >= 2)
			{
				int second = parts[1].ToInt();
				return second >= 16 && second <= 31;
			}
		}
		return false;
	}

	// Which web app address this player downloads from: the server's lanUrl when the player joined
	// the server over a private address (same network, where the public IP may not loop back),
	// else the public liveUrl. GetHostAddress is the address the player's game joined with.
	static string PickBase(string live, string lan)
	{
		if (lan == "")
			return live;
		if (live == "")
			return lan;
		string joined;
		int port;
		GetGame().GetHostAddress(joined, port);
		string pick = live;
		if (IsLocalAddress(joined))
			pick = lan;
		if (pick != s_Picked)
		{
			s_Picked = pick;
			Print("[MapDrawingTool] joined via " + joined + ": map downloads from " + pick);
		}
		return pick;
	}

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
		// Unfinished downloads go aside; those the new manifest still wants (same file) carry on where
		// they were (MDT_Resume). Starting them over on every manifest meant a slow player never got the
		// 79-part paper texture: the server resends the manifest whenever anyone's drawings change.
		s_Aside = new array<ref MDT_Download>;
		foreach (MDT_Download busy : s_Queue)
			s_Aside.Insert(busy);
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
				MDT_Resume(layer, path);
		}
		// The paper texture (2.5-10 MB) only once a paper map is in hand (WantPaper), queued last: the
		// full-screen map's layers are small and come first. The old texture stays on the paper until
		// the new one is in. A new hash means a new file name, so the engine never shows an old
		// texture it cached.
		MDT_Layer paper = manifest.paper;
		if (!paper || paper.parts <= 0 || paper.hash == "" || !MDT_ClientConfig.Get().paper)
			s_PaperPath = "";
		else
			QueuePaper();
		foreach (MDT_Download left : s_Aside)
		{
			if (left.other || left.layer.kind == "stamp")
				s_Queue.Insert(left);                    // not from the manifest: keep going
			else
			{
				if (left.file)
					left.file.Close();               // its request in flight stays alive in MDT_Rest
				left.file = null;
				DeleteFile(left.temp);
			}
		}
		s_Aside = null;
		s_ReadyCount++;
		s_CleanPending = true;
		string line = string.Format("manifest %1: %2 markers, %3 layers, %4 to download, paper %5", manifest.version, manifest.markers.Count(), manifest.layers.Count(), s_Queue.Count(), PaperState());
		Print("[MapDrawingTool] " + line);
		MDT_Trace.Line(line + ", from " + base);
	}

	protected static string PaperState()
	{
		if (!s_Manifest || !s_Manifest.paper || s_Manifest.paper.parts <= 0)
			return "none";
		if (!MDT_ClientConfig.Get().paper)
			return "off (client.json)";
		if (s_PaperPath != "" && s_PaperPath.Contains(s_Manifest.paper.hash))
			return "cached";
		if (!s_PaperWanted)
			return "when a map is in hand";
		return "queued";
	}

	// A paper map is in this player's hands (MissionGameplay.MDT_UpdatePaper): fetch its texture now.
	static void WantPaper()
	{
		if (s_PaperWanted)
			return;
		s_PaperWanted = true;
		QueuePaper();
	}

	protected static void QueuePaper()
	{
		if (!s_Manifest || !MDT_ClientConfig.Get().paper)
			return;
		MDT_Layer paper = s_Manifest.paper;
		if (!paper || paper.parts <= 0 || paper.hash == "")
			return;
		string path = MDT_CACHE_DIR + "/paper_" + paper.hash + ".paa";
		if (FileExist(path))
		{
			s_PaperPath = path;
			return;
		}
		if (!s_PaperWanted || s_Base == "")
			return;
		foreach (MDT_Download queued : s_Queue)
		{
			if (queued.path == path)
				return;
		}
		MDT_Resume(paper, path);
	}

	// Queues a download, or puts back the unfinished one for the same file that Apply set aside.
	protected static void MDT_Resume(MDT_Layer layer, string path)
	{
		if (s_Aside)
		{
			foreach (int i, MDT_Download kept : s_Aside)
			{
				if (kept.path == path && !kept.other)
				{
					kept.layer = layer;                    // same file; the query may be new
					s_Queue.Insert(kept);
					s_Aside.Remove(i);
					return;
				}
			}
		}
		MDT_Enqueue(layer, path);
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
		if (!paper || paper.hash == "" || paper.parts <= 0 || s_Base == "" || !MDT_ClientConfig.Get().paper)
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
			SyncBusy();
			if (s_CleanPending)
				CleanCache();
			return;
		}
		SyncBusy();
		MDT_Download job = s_Queue[0];
		float now = GetGame().GetTickTime();
		if (job.waiting)
		{
			// no answer at all, well past the engine's timeouts: ask again. The silent request's
			// callback stays alive in MDT_Rest, and its seq no longer matches if it ever answers.
			if (now - job.sentAt > MDT_Rest.CONNECT_S + MDT_Rest.READ_S + 30)
				OnPartFailed(job, job.seq, "no answer");
			return;
		}
		if (now < job.retryAt)
			return;
		if (!job.words)
		{
			Request(job);
			return;
		}
		// budget 0 (tick rate not measured yet): one batch of 256 words per frame
		float budgetF = s_TicksPerMs * BUDGET_MS;
		int budget = budgetF;
		int start = TickCount(0);
		array<int> words = job.words;
		int count = words.Count();
		int i = job.pos;
		if (job.written == 0 && i == 0 && count > 0)
			job.first = words[0];
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
		job.written += i - job.pos;
		job.pos = i;
		if (i < count)
			return;
		job.words = null;
		job.part++;
		job.tries = 0;
		if (job.part < job.layer.parts)
			return;
		job.file.Close();
		job.file = null;
		string bad = Check(job);
		if (bad != "")
		{
			Print("[MapDrawingTool] " + job.layer.kind + " " + job.layer.id + " is bad (" + bad + ")");
			MDT_Trace.Line("bad " + job.layer.kind + " " + job.layer.id + ": " + bad);
			DeleteFile(job.temp);
			if (!job.redone)
			{
				job.redone = true;                     // once more from the start, then give up
				job.part = 0;
				job.written = 0;
				job.first = 0;
				job.retryAt = now + 5;
				return;
			}
			s_Queue.RemoveOrdered(0);
			return;
		}
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
			string done = string.Format("downloaded %1 %2 (%3 bytes, %4 s, %5 ticks/ms)", job.layer.kind, job.layer.id, job.layer.bytes, (GetGame().GetTime() - job.started) / 1000.0, s_TicksPerMs);
			Print("[MapDrawingTool] " + done);
			MDT_Trace.Line(done);
		}
		DeleteFile(job.temp);
		s_Queue.RemoveOrdered(0);
	}

	// Before the file goes anywhere near the engine: every part arrived (the web app pads the last
	// int, so up to 3 bytes over layer.bytes) and it starts like a PAA. "" = fine.
	protected static string Check(MDT_Download job)
	{
		if (job.layer.bytes > 0 && job.written != (job.layer.bytes + 3) / 4)
			return string.Format("%1 bytes written, %2 expected", job.written * 4, job.layer.bytes);
		int type = job.first & 0xFFFF;
		if (type != 0xFF01 && type != 0xFF02 && type != 0xFF03 && type != 0xFF04 && type != 0xFF05 && type != 0x4444 && type != 0x1555 && type != 0x8888 && type != 0x8080)
			return "not a PAA (type " + type + ")";
		return "";
	}

	protected static void Request(MDT_Download job)
	{
		if (!job.file)
		{
			if (job.part == 0 && job.started == 0)
				job.started = GetGame().GetTime();
			job.file = new FileSerializer();
			if (!job.file.Open(job.temp, FileMode.WRITE))
			{
				Print("[MapDrawingTool] cannot write " + job.temp);
				MDT_Trace.Line("cannot write " + job.temp);
				job.file = null;
				s_Queue.RemoveOrdered(0);
				return;
			}
		}
		RestApi api = MDT_Rest.Api();
		if (!api)
			return;
		string mapId = s_Manifest.mapId;
		if (mapId == "")
			mapId = s_Manifest.world;     // web apps from before maps
		string url = "api/" + mapId + "/live/" + job.layer.id + "/" + job.part;
		if (job.layer.query != "")
			url += "?" + job.layer.query;               // personal work: the key only its owner gets
		job.seq++;
		job.waiting = true;
		job.sentAt = GetGame().GetTickTime();
		MDT_PartCallback cb = new MDT_PartCallback(job);
		MDT_Rest.Keep(cb);                              // before GET: an answer may come at once
		SyncBusy();                                     // the marker is on disk before the request
		MDT_Trace.Line(string.Format("get %1 %2 part %3/%4 try %5", job.layer.kind, job.layer.id, job.part + 1, job.layer.parts, job.tries + 1));
		api.GetRestContext(s_Base).GET(cb, url);
	}

	static void OnPart(MDT_Download job, int seq, string data)
	{
		if (!job || s_Queue.Find(job) < 0 || seq != job.seq || !job.waiting)
			return;                                    // cancelled, or answered too late
		job.waiting = false;
		MDT_Part part;
		string error;
		if (!s_Json.ReadFromString(part, data, error) || !part || !part.d || part.d.Count() == 0)
		{
			job.waiting = true;
			OnPartFailed(job, seq, "bad part " + error);
			return;
		}
		job.words = part.d;
		job.pos = 0;
		MDT_Trace.Line(string.Format("got %1 %2 part %3 (%4 bytes)", job.layer.kind, job.layer.id, job.part + 1, part.d.Count() * 4));
	}

	static void OnPartFailed(MDT_Download job, int seq, string why)
	{
		if (!job || s_Queue.Find(job) < 0 || seq != job.seq || !job.waiting)
			return;
		job.waiting = false;
		job.seq++;                                     // whatever the failed request still says is ignored
		job.tries++;
		string line = string.Format("%1 %2 part %3/%4 failed: %5", job.layer.kind, job.layer.id, job.part + 1, job.layer.parts, why);
		Print("[MapDrawingTool] " + line);
		MDT_Trace.Line(line);
		if (job.tries < 6)                             // per part (tries resets after each part)
		{
			job.retryAt = GetGame().GetTickTime() + 2 * job.tries;
			return;
		}
		// others go first and it pauses 30 s (keeps its parts); given up after 10 pauses (a file the
		// web app no longer has), until the next manifest
		job.pauses++;
		if (job.pauses > 10)
		{
			if (job.file)
				job.file.Close();
			job.file = null;
			DeleteFile(job.temp);
			s_Queue.RemoveItem(job);
			MDT_Trace.Line("gave up on " + job.layer.kind + " " + job.layer.id);
			return;
		}
		job.tries = 0;
		job.retryAt = GetGame().GetTickTime() + 30;
		int at = s_Queue.Find(job);
		if (at >= 0 && s_Queue.Count() > 1)
		{
			s_Queue.RemoveOrdered(at);
			s_Queue.Insert(job);
		}
		MDT_Trace.Line("pausing " + job.layer.kind + " " + job.layer.id + " for 30 s");
	}

	// The crash guard's marker (MDT_ClientConfig): on disk while a paper texture download is under way.
	protected static void SyncBusy()
	{
		bool busy = false;
		foreach (MDT_Download job : s_Queue)
		{
			if (job.layer.kind == "paper" && job.file)
				busy = true;
		}
		if (busy == s_PaperBusy)
			return;
		s_PaperBusy = busy;
		if (!busy)
		{
			DeleteFile(MDT_ClientConfig.BUSY_FILE);
			return;
		}
		FileHandle fh = OpenFile(MDT_ClientConfig.BUSY_FILE, FileMode.WRITE);
		if (fh)
		{
			FPrintln(fh, "A paper map texture download is under way. If this file is still here when the game starts, the game closed during it, and the paper texture is turned off.");
			CloseFile(fh);
		}
	}

	// Leaving the server or quitting normally (MissionGameplay.OnMissionFinish): not a crash.
	static void OnQuit()
	{
		s_PaperBusy = false;
		DeleteFile(MDT_ClientConfig.BUSY_FILE);
	}

	static array<ref MDT_Marker> GetMarkers()
	{
		if (s_Manifest)
			return s_Manifest.markers;
		return null;
	}
}
