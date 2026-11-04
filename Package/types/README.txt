Map Drawing Tool: pen spawns for your server's loot economy
============================================================

Drawing on the map in game needs a pen. Vanilla DayZ lists all four pens with nominal 0, so they
never spawn. mdt_types.xml makes Pen_Black, Pen_Red, Pen_Green and Pen_Blue spawn on shelves in
towns, villages and offices: about 6 of each on the map (at least 3). Works on Chernarus, Livonia,
Sakhal and Nasdara. Change <nominal> and <min> to taste.

Install it one of two ways (both in your mission folder, e.g. mpmissions\dayzOffline.chernarusplus):

A) Its own file (recommended: survives DayZ updates to types.xml)
   1. Make a folder "mapdrawingtool" in the mission folder and copy mdt_types.xml into it.
   2. In cfgeconomycore.xml, inside <economycore>, add:

        <ce folder="mapdrawingtool">
            <file name="mdt_types.xml" type="types" />
        </ce>

   The entries here replace the vanilla pen entries in db\types.xml.

B) Paste into types.xml
   In db\types.xml, find the four <type name="Pen_..."> blocks and replace them with the four
   blocks from mdt_types.xml (do not keep both).

Restart the server. New pens appear as the economy respawns loot; a wipe or storage reset shows
them at once. Players can also be given a pen at spawn (init.c / your spawn gear mod).
