--Minetest
--Copyright (C) 2014 sapier
--
--This program is free software; you can redistribute it and/or modify
--it under the terms of the GNU Lesser General Public License as published by
--the Free Software Foundation; either version 3.0 of the License, or
--(at your option) any later version.
--
--This program is distributed in the hope that it will be useful,
--but WITHOUT ANY WARRANTY; without even the implied warranty of
--MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
--GNU Lesser General Public License for more details.
--
--You should have received a copy of the GNU Lesser General Public License along
--with this program; if not, write to the Free Software Foundation, Inc.,
--51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

mt_color_grey  = "#AAAAAA"
mt_color_blue  = "#6389FF"
mt_color_green = "#72FF63"
mt_color_dark_green = "#25C191"

local menupath = core.get_mainmenu_path()
local basepath = core.get_builtin_path()
local mobile = PLATFORM == "Android" or PLATFORM == "iOS"
defaulttexturedir = core.get_texturepath_share() .. DIR_DELIM .. "base" ..
					DIR_DELIM .. "pack" .. DIR_DELIM

dofile(basepath .. "common" .. DIR_DELIM .. "filterlist.lua")
dofile(basepath .. "fstk" .. DIR_DELIM .. "buttonbar.lua")
dofile(basepath .. "fstk" .. DIR_DELIM .. "dialog.lua")
dofile(basepath .. "fstk" .. DIR_DELIM .. "tabview.lua")
dofile(basepath .. "fstk" .. DIR_DELIM .. "ui.lua")
dofile(menupath .. DIR_DELIM .. "async_event.lua")
dofile(menupath .. DIR_DELIM .. "common.lua")
dofile(menupath .. DIR_DELIM .. "pkgmgr.lua")
dofile(menupath .. DIR_DELIM .. "textures.lua")

dofile(menupath .. DIR_DELIM .. "dlg_create_world.lua")
dofile(menupath .. DIR_DELIM .. "dlg_create_world_default.lua")
dofile(menupath .. DIR_DELIM .. "dlg_delete_world.lua")

if not mobile then
	dofile(menupath .. DIR_DELIM .. "dlg_config_world.lua")
	dofile(menupath .. DIR_DELIM .. "dlg_delete_content.lua")
	dofile(menupath .. DIR_DELIM .. "dlg_contentstore.lua")
	dofile(menupath .. DIR_DELIM .. "dlg_rename_modpack.lua")
	dofile(menupath .. DIR_DELIM .. "dlg_settings_advanced.lua")
end

local tabs = {}

tabs.home = dofile(menupath .. DIR_DELIM .. "tab_home.lua")

if not mobile then
	tabs.settings = dofile(menupath .. DIR_DELIM .. "tab_settings.lua")
	tabs.content  = dofile(menupath .. DIR_DELIM .. "tab_content.lua")
end

tabs.credits  = dofile(menupath .. DIR_DELIM .. "tab_credits.lua")
tabs.local_default_game = dofile(menupath .. DIR_DELIM .. "tab_local_default.lua")
tabs.local_game = dofile(menupath .. DIR_DELIM .. "tab_local.lua")
tabs.play_online = dofile(menupath .. DIR_DELIM .. "tab_online.lua")

local htabs = {}
local hpath = menupath .. DIR_DELIM .. "hosting" .. DIR_DELIM .. "init.lua"
local hosting = io.open(hpath, "r")
if hosting then
	htabs = dofile(hpath)
	io.close(hosting)
end

--------------------------------------------------------------------------------
local function main_event_handler(tabview, event)
	if event == "MenuQuit" then
		core.close()
	end
	return true
end

--------------------------------------------------------------------------------
function menudata.init_tabs()
	local tv_main = tabview_create("maintab", {x = 12, y = 5.4}, {x = 0, y = 0})

	tv_main:add(tabs.home)

	for i = 1, #pkgmgr.games do
		if pkgmgr.games[i].id == "default" then
			tv_main:add(tabs.local_default_game)
			break
		end
	end

	for i = 1, #pkgmgr.games do
		if pkgmgr.games[i].id ~= "default" then
			tv_main:add(tabs.local_game)
			break
		end
	end

	tv_main:add(tabs.play_online)
	for _, page in pairs(htabs) do
		tv_main:add(page)
	end

	if not mobile then
		tv_main:set_autosave_tab(true)
		tv_main:add(tabs.content)
		tv_main:add(tabs.settings)
	end
	tv_main:add(tabs.credits)

	tv_main:set_global_event_handler(main_event_handler)
	tv_main:set_fixed_size(false)

	if not mobile then
		local last_tab = core.settings:get("maintab_LAST")
		if last_tab and tv_main.current_tab ~= last_tab then
			tv_main:set_tab(last_tab)
		end
	end

	ui.set_default("maintab")
	tv_main:show()

	ui.update()
end

--------------------------------------------------------------------------------
local function init_globals()
	-- Init gamedata
	gamedata.worldindex = 0

	menudata.worldlist = filterlist.create(
		core.get_worlds,
		compare_worlds,
		-- Unique id comparison function
		function(element, uid)
			return element.name == uid
		end,
		-- Filter function
		function(element, gameid)
			return element.gameid == gameid
		end
	)

	menudata.worldlist:add_sort_mechanism("alphabetic", sort_worlds_alphabetic)
	menudata.worldlist:set_sortmode("alphabetic")

	-- Create main tabview
	core.set_clouds(false)
	mm_texture.set_dirt_bg()
	menudata.init_tabs()
--	core.sound_play("main_menu", true)
end

init_globals()
