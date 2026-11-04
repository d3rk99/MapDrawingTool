class CfgPatches
{
	class MapDrawingTool
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 0.1;
		requiredAddons[] = {"DZ_Data", "DZ_Scripts", "DZ_Gear_Navigation", "DZ_Gear_Tools"};
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

// Vanilla pens all look and read the same. Each gets its own name, description and a coloured
// clip and cap (hiddenSelections on the model's whole-model selection "zbytek").
class CfgVehicles
{
	class Inventory_Base;
	class Pen_ColorBase: Inventory_Base {};
	class Pen_Black: Pen_ColorBase
	{
		displayName = "Black Pen";
		descriptionShort = "A pen with black ink. Carry it to draw on the map in black: open the map, press M, then D.";
		hiddenSelections[] = {"zbytek"};
		hiddenSelectionsTextures[] = {"MapDrawingTool\data\pens\pen_black_co.paa"};
	};
	class Pen_Red: Pen_ColorBase
	{
		displayName = "Red Pen";
		descriptionShort = "A pen with red ink. Carry it to draw on the map in red: open the map, press M, then D.";
		hiddenSelections[] = {"zbytek"};
		hiddenSelectionsTextures[] = {"MapDrawingTool\data\pens\pen_red_co.paa"};
	};
	class Pen_Green: Pen_ColorBase
	{
		displayName = "Green Pen";
		descriptionShort = "A pen with green ink. Carry it to draw on the map in green: open the map, press M, then D.";
		hiddenSelections[] = {"zbytek"};
		hiddenSelectionsTextures[] = {"MapDrawingTool\data\pens\pen_green_co.paa"};
	};
	class Pen_Blue: Pen_ColorBase
	{
		displayName = "Blue Pen";
		descriptionShort = "A pen with blue ink. Carry it to draw on the map in blue: open the map, press M, then D.";
		hiddenSelections[] = {"zbytek"};
		hiddenSelectionsTextures[] = {"MapDrawingTool\data\pens\pen_blue_co.paa"};
	};
};
