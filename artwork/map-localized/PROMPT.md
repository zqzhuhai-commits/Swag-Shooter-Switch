# 地图中文化制作记录

Built-in image_gen tool, text-localization edit. Final asset: minimap-zh.png. Unity encoded preview: minimap-zh-encoded.png. Original 1024×1024 retained at encoding time. Generated terrain is not pixel-identical; alignment must be checked in game.

## Main prompt

Use case: text-localization. Edit target: attached original 1024x1024 game minimap texture. Replace ALL English place labels with the following exact simplified Chinese labels at the EXACT same respective positions. Preserve map coastline, roads, rails, rivers, lakes, terrain, buildings, plane icons, all green station icons, composition, scale, geometry and textures as closely as possible. No added symbols or UI. This is a functional minimap: changing geography is prohibited. Only edit the lettering and tiny area behind old lettering. White bold clean Chinese sans serif with black outline, readable, sized to fit original label footprints.
Translations:
Mystic Hills Lands=秘境丘陵
Wild Borderlands=荒野边境
East Coast Line=东海岸线
Bluffoods Depot=林崖仓库
Foxtail Fields=狐尾田野
Waterless Tundra=干旱苔原
Rocky Grasslands=岩石草原
Sly Hinterland=隐秘腹地
Hungry Barrens=饥荒之地
Twin Hideout=双子藏身处
Sanrom Brook Dam=山溪水坝
Lower East Town=东部下城
Terra Firma Site=坚土地带
Sea Breeze Line=海风线
Sandy Steppes=沙地草原
New North Tracks=北部新铁路
Boulder Valley Farm=巨石谷农场
Burned Fields=焦土田野
Grand Ridge Mines=大岭矿场
Black Scar Outpost=黑疤哨站
Borough of Ruins=废墟镇
Dusk Willow Graveyard=暮柳墓园
Evenna Bridge=伊文纳大桥
Boothbury Lake=布斯伯里湖
Mountainmart Factory=山地工厂
Rise & Grind Restroom=晨光休息站
Ostana Downtown Airport=奥斯塔纳城区机场
High Hill Meadow=高丘草甸
Bluewater Ranch=碧水牧场
Bare Hinterland=荒芜腹地
Thunder Spell Cliff=雷咒悬崖
Thistleberry Lands=蓟莓之地
Westwood Runway=西林跑道
South Bay Line=南湾线
Important: map lettering only, no Latin text left. Use same 1:1 full bleed image.

## Correction prompt

Preserve the map; replace the upper duplicate 荒芜腹地 beside the graveyard with 暮柳墓园; retain the lower 荒芜腹地. Correct bottom berry-land label to 蓟莓之地.
