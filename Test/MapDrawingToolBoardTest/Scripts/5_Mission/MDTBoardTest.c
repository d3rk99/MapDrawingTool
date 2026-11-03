// Two wooden map boards (the tourist trail map, which shows karta_co.paa) facing opposite ways in
// Chernogorsk, made on this client only. The test mission teleports players to 6563, 2465.
modded class MissionGameplay
{
	protected bool m_MDTB_Done;

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);
		if (m_MDTB_Done || !GetGame().GetPlayer())
			return;
		m_MDTB_Done = true;
		string model = "DZ\\structures\\signs\\tourist\\trailmap_noarrow.p3d";
		float y = GetGame().SurfaceY(6560, 2470);
		Object a = GetGame().CreateStaticObjectUsingP3D(model, Vector(6560, y, 2470), Vector(0, 0, 0), 1, true);
		Object b = GetGame().CreateStaticObjectUsingP3D(model, Vector(6566, y, 2470), Vector(180, 0, 0), 1, true);
		Print("[MDT-BOARD] boards made: " + (a != null) + ", " + (b != null) + " at height " + y);
	}
}
