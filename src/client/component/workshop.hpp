#pragma once

#include <string>
#include <vector>

#include <game/game.hpp>

namespace workshop
{
	std::string get_usermap_publisher_id(const std::string& folder_name);
	std::string get_usermap_publisher_id();
	std::string get_usermap_path(const std::string& mapname, const std::string& pub_id);
	std::string get_mod_publisher_id();
	std::string get_mod_resized_name();
	bool check_valid_usermap_id(const std::string& mapname, const std::string& pub_id, const std::string& base_url);
	bool check_valid_mod_id(const std::string& pub_id);
	bool check_required_content(const std::string& mapname, const std::string& usermap_id,
	                            const std::string& mod_id, const std::string& base_url);
	void handle_install_progress(const std::string& request_id, int percent);
	void handle_install_result(const std::string& request_id, bool success, const std::string& error);
	void handle_launcher_disconnect();
	void setup_same_mod_as_host(const std::string& usermap, const std::string& mod);
}
