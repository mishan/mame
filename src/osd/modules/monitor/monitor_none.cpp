// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
/*
 * monitor_none.cpp
 *
 */
#include "monitor_module.h"


namespace osd {

namespace {

class monitor_none : public monitor_module
{
public:
	monitor_none() : monitor_module(OSD_MONITOR_PROVIDER, "none") { }

	virtual int init(osd_interface &osd, const osd_options &options) override { return 0; }

	virtual std::shared_ptr<osd_monitor_info> monitor_from_handle(std::uint64_t handle) override { return nullptr; }
	virtual std::shared_ptr<osd_monitor_info> monitor_from_rect(const osd_rect &rect) override { return nullptr; }
	virtual std::shared_ptr<osd_monitor_info> monitor_from_window(const osd_window &window) override { return nullptr; }
	virtual std::shared_ptr<osd_monitor_info> pick_monitor(osd_options &options, int index) override { return nullptr; }
};

} // anonymous namespace

} // namespace osd


MODULE_DEFINITION(MONITOR_NONE, osd::monitor_none)
