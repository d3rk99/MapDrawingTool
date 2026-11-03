// J cycles the texture of the paper map in hand: $profile file, absolute path, vanilla.
// The result is in the client script log ([MDT-TEX]) and on the unfolded paper.
modded class MissionGameplay
{
	protected int m_MDTX_Mode;

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);
		if (!KeyState(KeyCode.KC_J) || GetGame().GetUIManager().GetMenu())
			return;
		ClearKey(KeyCode.KC_J);
		PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
		ItemMap paper;
		if (player)
			paper = ItemMap.Cast(player.GetItemInHands());
		if (!paper)
		{
			Print("[MDT-TEX] J: no map in hands");
			return;
		}
		m_MDTX_Mode = (m_MDTX_Mode + 1) % 3;
		string path = paper.m_TextureOpened;
		if (m_MDTX_Mode == 0)
			path = MDTX_PROFILE;
		else if (m_MDTX_Mode == 1)
			path = MDTX_ABSOLUTE;
		paper.SetObjectTexture(1, path);
		Print("[MDT-TEX] J: mode " + m_MDTX_Mode + " opened texture -> " + path + " (file exists " + FileExist(path) + ")");
	}
}
