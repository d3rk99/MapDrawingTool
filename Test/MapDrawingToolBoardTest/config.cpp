// Test only (never ships): puts two wooden map boards in Chernogorsk on the client, to see what the
// boards show. Vanilla has no class for them and the object spawner refuses their model path.
class CfgPatches
{
	class MapDrawingToolBoardTest
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 0.1;
		requiredAddons[] = {"DZ_Data", "DZ_Scripts", "DZ_Structures_Signs"};
	};
};

class CfgMods
{
	class MapDrawingToolBoardTest
	{
		dir = "MapDrawingToolBoardTest";
		name = "Map Drawing Tool board test";
		author = "Derk";
		type = "mod";
		dependencies[] = {"Mission"};
		class defs
		{
			class missionScriptModule
			{
				value = "";
				files[] = {"MapDrawingToolBoardTest/Scripts/5_Mission"};
			};
		};
	};
};
