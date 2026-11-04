// Other players' maps (client): someone holding a map near us shows what is on THEIR map, global
// plus their personal work. A texture change is local to each game, so our game asks the server
// for the holder's paper texture (MDT_RPC_PAPER_ASK on the holder), downloads it once in the
// background, and puts it on their map. Checked once a second for players within 20 m.
class MDT_Watch
{
	static const float RANGE = 20;
	static const int ASK_EVERY_MS = 60000;

	protected static ref map<PlayerBase, string> s_Texture = new map<PlayerBase, string>;   // holder -> file
	protected static ref map<PlayerBase, int> s_Asked = new map<PlayerBase, int>;
	protected static ref map<ItemMap, string> s_Applied = new map<ItemMap, string>;
	protected static float s_Wait;

	static void Update(float timeslice)
	{
		s_Wait -= timeslice;
		if (s_Wait > 0)
			return;
		s_Wait = 1;
		PlayerBase me = PlayerBase.Cast(GetGame().GetPlayer());
		if (!me)
			return;
		array<Object> near = new array<Object>;
		GetGame().GetObjectsAtPosition(me.GetPosition(), RANGE, near, null);
		int now = GetGame().GetTime();
		foreach (Object obj : near)
		{
			PlayerBase holder = PlayerBase.Cast(obj);
			if (!holder || holder == me || !holder.IsAlive())
				continue;
			ItemMap paper = ItemMap.Cast(holder.GetItemInHands());
			if (!paper)
				continue;
			string file;
			if (s_Texture.Find(holder, file) && file != "" && FileExist(file))
			{
				string applied;
				if (!s_Applied.Find(paper, applied) || applied != file)
				{
					paper.SetObjectTexture(1, file);
					s_Applied.Set(paper, file);
				}
			}
			int asked;
			if (!s_Asked.Find(holder, asked) || now - asked > ASK_EVERY_MS)
			{
				s_Asked.Set(holder, now);
				ScriptRPC rpc = new ScriptRPC();
				rpc.Send(holder, MDT_RPC_PAPER_ASK, true, null);
			}
		}
	}

	// The server's answer: the holder's paper texture (an MDT_Layer as JSON, "" = vanilla map).
	static void OnPaper(PlayerBase holder, string json)
	{
		if (!holder || json == "")
			return;
		MDT_Layer layer;
		string error;
		if (!JsonFileLoader<MDT_Layer>.LoadData(json, layer, error) || !layer)
			return;
		s_Texture.Set(holder, MDT_Live.QueueOther(layer));
	}
}
