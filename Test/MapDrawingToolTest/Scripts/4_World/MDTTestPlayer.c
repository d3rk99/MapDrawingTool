// While the full-screen map is up over the unfolded paper map, clicks, Esc, inventory and
// raising hands must not fold the paper (vanilla folds it on any of those with use3DMap).
modded class PlayerBase
{
	override bool IsMapCallbackCancelInput()
	{
		if (MDTT_MapScreenOpen())
			return false;
		return super.IsMapCallbackCancelInput();
	}

	override bool IsMapCallbackEndInput()
	{
		if (MDTT_MapScreenOpen())
			return false;
		return super.IsMapCallbackEndInput();
	}

	protected bool MDTT_MapScreenOpen()
	{
		return !g_Game.IsDedicatedServer() && g_Game.GetUIManager() && g_Game.GetUIManager().IsMenuOpen(MENU_MAP);
	}
}
