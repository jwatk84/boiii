#pragma once

#include <cstdint>
#include <string_view>

namespace game
{
	struct netadr_t;
}

namespace client_fps_limit
{
	inline constexpr const char* info_key = "en_client_fps_limit_v1";
	inline constexpr const char* fixed_limit = "150";

	uint64_t begin_join(const game::netadr_t& server);
	bool is_current_join(const game::netadr_t& server, uint64_t generation);
	// Only the checked connect-query callback may arm a fixed cap policy.
	void accept_verified_join_info(const game::netadr_t& server, uint64_t generation,
	                              std::string_view value, bool dedicated);
	// Call around the actual native join, after assets/mode are ready.
	bool prepare_native_join(const game::netadr_t& server, uint64_t generation);
	void observe_native_join_start(const game::netadr_t& server, uint64_t generation);
	void before_renderer_frame();
}
