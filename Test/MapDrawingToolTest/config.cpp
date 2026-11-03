// In-game feasibility test for live map updates. Never ships; see Test\README.md.
class CfgPatches
{
	class MapDrawingToolTest
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 0.1;
		requiredAddons[] = {"DZ_Data", "DZ_Scripts", "DZ_Gear_Navigation"};
	};
};

class CfgMods
{
	class MapDrawingToolTest
	{
		dir = "MapDrawingToolTest";
		name = "Map Drawing Tool test";
		author = "Derk";
		type = "mod";
		dependencies[] = {"World", "Mission"};
		class defs
		{
			class worldScriptModule
			{
				value = "";
				files[] = {"MapDrawingToolTest/Scripts/4_World"};
			};
			class missionScriptModule
			{
				value = "";
				files[] = {"MapDrawingToolTest/Scripts/5_Mission"};
			};
		};
	};
};
