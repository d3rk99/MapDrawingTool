// The item actions box (bottom left) says "Fold map" while the paper map is unfolded. Under it, on the
// free "hold" row (ia_continuous, day_z_hud.layout), this adds "M  Full screen & drawing": the key in
// the row's controller-icon text widget, the mouse icon hidden. Vanilla resets the row every frame
// (BuildCursor), so it is set again each frame and given back when the map folds.
modded class ItemActionsWidget
{
	protected bool m_MDT_Hint;

	override protected void BuildCursor()
	{
		super.BuildCursor();
		PlayerBase player = PlayerBase.Cast(m_Player);
		bool show = !m_Continuous && m_EntityInHands && m_EntityInHands.IsInherited(ItemMap) && player && player.IsMapOpen();
		Widget row = m_Root.FindAnyWidget("ia_continuous");
		Widget mouse = m_Root.FindAnyWidget("ia_continuous_btn_icon");
		RichTextWidget key = RichTextWidget.Cast(m_Root.FindAnyWidget("ia_continuous_btn_icon_xbox"));
		TextWidget name = TextWidget.Cast(m_Root.FindAnyWidget("ia_continuous_action_name"));
		if (!row || !mouse || !key || !name)
			return;
		if (show)
		{
			name.SetText("Full screen & drawing");
			key.SetText("M");
			mouse.Show(false);
			key.Show(true);
			row.Show(true);
			m_MDT_Hint = true;
		}
		else if (m_MDT_Hint)
		{
			m_MDT_Hint = false;                     // vanilla's own icons again (its refreshers are private)
			SetControllerIcon("ia_continuous", "UADefaultAction");
			OnInputDeviceChanged(g_Game.GetInput().GetCurrentInputDevice());
		}
	}
}
