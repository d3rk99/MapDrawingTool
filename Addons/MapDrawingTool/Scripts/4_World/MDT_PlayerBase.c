modded class PlayerBase
{
	override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
	{
		super.OnRPC(sender, rpc_type, ctx);

		if (rpc_type == MDT_RPC_SUBMIT && GetGame().IsServer())
		{
			int pIndex, pCount;
			string piece;
			if (ctx.Read(pIndex) && ctx.Read(pCount) && ctx.Read(piece))
				MDT_Events.SubmitPiece.Invoke(this, sender, pIndex, pCount, piece);
			return;
		}
		if (rpc_type == MDT_RPC_UPLOAD && GetGame().IsServer())
		{
			int uIndex, uCount, uBytes;
			string uName;
			array<int> uWords = new array<int>;
			if (ctx.Read(uIndex) && ctx.Read(uCount) && ctx.Read(uName) && ctx.Read(uBytes) && ctx.Read(uWords))
				MDT_Events.UploadPiece.Invoke(this, sender, uIndex, uCount, uName, uBytes, uWords);
			return;
		}
		if (rpc_type == MDT_RPC_UPLOADED && GetGame().IsClient())
		{
			string upName, upId, upError, upPreview;
			bool upOk;
			int upParts;
			if (ctx.Read(upName) && ctx.Read(upOk) && ctx.Read(upId) && ctx.Read(upError) && ctx.Read(upPreview) && ctx.Read(upParts))
				MDT_Draft.OnUploaded(upName, upOk, upId, upError, upPreview, upParts);
			return;
		}
		if (rpc_type == MDT_RPC_MINE_ASK && GetGame().IsServer())
		{
			MDT_Events.MineAsk.Invoke(this, sender);
			return;
		}
		if (rpc_type == MDT_RPC_WITHDRAW && GetGame().IsServer())
		{
			string withdrawId;
			if (ctx.Read(withdrawId))
				MDT_Events.Withdraw.Invoke(this, sender, withdrawId);
			return;
		}
		if (rpc_type == MDT_RPC_PAPER_ASK && GetGame().IsServer())
		{
			MDT_Events.PaperAsk.Invoke(this, sender);
			return;
		}
		if (rpc_type == MDT_RPC_PAPER && GetGame().IsClient())
		{
			string paperJson;
			if (ctx.Read(paperJson))
				MDT_Watch.OnPaper(this, paperJson);
			return;
		}
		if (rpc_type == MDT_RPC_MINE && GetGame().IsClient())
		{
			int mIndex, mCount;
			string mText;
			if (ctx.Read(mIndex) && ctx.Read(mCount) && ctx.Read(mText) && mCount > 0 && mCount <= 500)
				MDT_Mine.OnPiece(mIndex, mCount, mText);
			return;
		}
		if (rpc_type == MDT_RPC_RESULT && GetGame().IsClient())
		{
			bool ok;
			string message;
			if (ctx.Read(ok) && ctx.Read(message))
				MDT_Draft.OnResult(ok, message);
			return;
		}
		if (rpc_type != MDT_RPC_MANIFEST || !GetGame().IsClient())
			return;

		string version, text, base;
		int index, count;
		if (!ctx.Read(version) || !ctx.Read(index) || !ctx.Read(count) || !ctx.Read(text) || !ctx.Read(base))
			return;
		if (count < 1 || count > 500 || index < 0 || index >= count)
			return;
		MDT_Live.OnChunk(version, index, count, text, base);
	}

	// With use3DMap the unfolded paper folds on a click, Esc, inventory or raised hands. While the
	// full-screen map is open over it (M), those inputs belong to the map screen, not the paper.
	override bool IsMapCallbackCancelInput()
	{
		if (MDT_MapScreenOpen())
			return false;
		return super.IsMapCallbackCancelInput();
	}

	override bool IsMapCallbackEndInput()
	{
		if (MDT_MapScreenOpen())
			return false;
		return super.IsMapCallbackEndInput();
	}

	// ItemMap.SetMapStateOpen calls this just before it shows the unfolded paper, so the server's
	// texture (MDT_Live.s_PaperPath) is on it before the first frame; set later, the vanilla map
	// flashed. MissionGameplay.MDT_UpdatePaper keeps it there after that.
	override void SetMapOpen(bool state)
	{
		super.SetMapOpen(state);
		if (!state || g_Game.IsDedicatedServer() || this != g_Game.GetPlayer() || MDT_Live.s_PaperPath == "")
			return;
		ItemMap paper = ItemMap.Cast(GetItemInHands());
		if (paper)
			paper.SetObjectTexture(1, MDT_Live.s_PaperPath);
	}

	protected bool MDT_MapScreenOpen()
	{
		return !g_Game.IsDedicatedServer() && g_Game.GetUIManager() && g_Game.GetUIManager().IsMenuOpen(MENU_MAP);
	}
}
