// ItemMap is an engine class and cannot be modded, so the map in hand gets the $profile
// texture from PlayerBase (see 5_Mission/MDTTexTest.c for the J key).
static const string MDTX_PROFILE = "$profile:MapDrawingTool/textest_karta.paa";
static const string MDTX_ABSOLUTE = "E:/DayZMods/MapDrawingTool/Test/textest/textest_karta.paa";

modded class PlayerBase
{
	override void OnItemInHandsChanged()
	{
		super.OnItemInHandsChanged();
		if (GetGame().IsDedicatedServer() || this != GetGame().GetPlayer())
			return;
		ItemMap paper = ItemMap.Cast(GetItemInHands());
		if (!paper)
			return;
		paper.SetObjectTexture(1, MDTX_PROFILE);
		Print("[MDT-TEX] map in hands: opened texture -> " + MDTX_PROFILE + " (file exists " + FileExist(MDTX_PROFILE) + ")");
	}
}
