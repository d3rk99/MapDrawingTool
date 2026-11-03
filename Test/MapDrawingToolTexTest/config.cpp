// In-game test: can the paper map in hand show a texture from $profile (downloaded at runtime)?
// Never ships; see Test\README.md.
class CfgPatches
{
	class MapDrawingToolTexTest
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 0.1;
		requiredAddons[] = {"DZ_Data", "DZ_Scripts", "DZ_Gear_Navigation"};
	};
};

class CfgMods
{
	class MapDrawingToolTexTest
	{
		dir = "MapDrawingToolTexTest";
		name = "Map Drawing Tool texture test";
		author = "Derk";
		type = "mod";
		dependencies[] = {"World", "Mission"};
		class defs
		{
			class worldScriptModule
			{
				value = "";
				files[] = {"MapDrawingToolTexTest/Scripts/4_World"};
			};
			class missionScriptModule
			{
				value = "";
				files[] = {"MapDrawingToolTexTest/Scripts/5_Mission"};
			};
		};
	};
};
