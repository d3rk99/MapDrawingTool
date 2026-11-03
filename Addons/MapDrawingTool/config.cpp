class CfgPatches
{
	class MapDrawingTool
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 0.1;
		requiredAddons[] = {"DZ_Data", "DZ_Scripts", "DZ_Gear_Navigation"};
	};
};

class CfgMods
{
	class MapDrawingTool
	{
		dir = "MapDrawingTool";
		name = "Map Drawing Tool";
		author = "Derk";
		type = "mod";
		dependencies[] = {"Game", "World", "Mission"};
		class defs
		{
			class gameScriptModule
			{
				value = "";
				files[] = {"MapDrawingTool/Scripts/3_Game"};
			};
			class worldScriptModule
			{
				value = "";
				files[] = {"MapDrawingTool/Scripts/4_World"};
			};
			class missionScriptModule
			{
				value = "";
				files[] = {"MapDrawingTool/Scripts/5_Mission"};
			};
		};
	};
};
