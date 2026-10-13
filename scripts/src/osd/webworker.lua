-- license:BSD-3-Clause
-- copyright-holders:MAMEdev Team

---------------------------------------------------------------------------
--
--   webworker.lua
--
--   Rules for building a headless OSD that runs in an Emscripten Web
--   Worker and talks to the page through JavaScript hooks
--
---------------------------------------------------------------------------

if _OPTIONS["targetos"]~="asmjs" then
	error("OSD=webworker requires an Emscripten build")
end

if not _OPTIONS["NO_OPENGL"] then
	_OPTIONS["NO_OPENGL"] = "1"
end

dofile("modules.lua")


function maintargetosdoptions(_target,_subtarget)
	osdmodulestargetconf()
end

BASE_TARGETOS = "unix"

project ("qtdbg_" .. _OPTIONS["osd"])
	uuid (os.uuid("qtdbg_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	dofile("webworker_cfg.lua")
	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/devices", -- accessing imagedev from debugger
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/modules/render",
		MAME_DIR .. "3rdparty",
	}

	qtdebuggerbuild()

project ("osd_" .. _OPTIONS["osd"])
	uuid (os.uuid("osd_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	dofile("webworker_cfg.lua")
	osdmodulesbuild()

	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/devices", -- accessing imagedev from debugger
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/modules/file",
		MAME_DIR .. "src/osd/modules/render",
		MAME_DIR .. "3rdparty",
	}

	-- bgfx needs a native window or a DOM canvas, and a worker has neither
	removefiles {
		MAME_DIR .. "src/osd/modules/render/drawbgfx.cpp",
		MAME_DIR .. "src/osd/modules/render/bgfx/*.cpp",
	}

	files {
		MAME_DIR .. "src/osd/osdepend.h",
		MAME_DIR .. "src/osd/modules/osdwindow.cpp",
		MAME_DIR .. "src/osd/modules/osdwindow.h",
		MAME_DIR .. "src/osd/webworker/webworkermain.cpp",
	}

project ("ocore_" .. _OPTIONS["osd"])
	uuid (os.uuid("ocore_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	removeflags {
		"SingleOutputDir",
	}

	dofile("webworker_cfg.lua")

	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		ext_includedir("asio"),
	}

	files {
		MAME_DIR .. "src/osd/asio.cpp",
		MAME_DIR .. "src/osd/asio.h",
		MAME_DIR .. "src/osd/osdcore.cpp",
		MAME_DIR .. "src/osd/osdcore.h",
		MAME_DIR .. "src/osd/osdfile.h",
		MAME_DIR .. "src/osd/strconv.cpp",
		MAME_DIR .. "src/osd/strconv.h",
		MAME_DIR .. "src/osd/osdsync.cpp",
		MAME_DIR .. "src/osd/osdsync.h",
		MAME_DIR .. "src/osd/modules/osdmodule.cpp",
		MAME_DIR .. "src/osd/modules/osdmodule.h",
		MAME_DIR .. "src/osd/modules/lib/osdlib_unix.cpp",
		MAME_DIR .. "src/osd/modules/lib/osdlib.h",
		MAME_DIR .. "src/osd/modules/file/posixdir.cpp",
		MAME_DIR .. "src/osd/modules/file/posixfile.cpp",
		MAME_DIR .. "src/osd/modules/file/posixfile.h",
		MAME_DIR .. "src/osd/modules/file/posixptty.cpp",
		MAME_DIR .. "src/osd/modules/file/posixsocket.cpp",
	}
