#include <std_include.hpp>

#include "game/game.hpp"
#include "client_fps_limit.hpp"
#include "network.hpp"
#include "party.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

namespace client_fps_limit
{
	namespace
	{
		using clock = std::chrono::steady_clock;
		struct join_policy
		{
			game::netadr_t server{};
			game::netadr_t prior_server{};
			uint64_t generation{};
			bool current_join{};
			bool info_received{};
			bool enabled{};
			bool join_started{};
			bool awaiting_departure{};
			bool observed_attempt{};
			clock::time_point pending_deadline{};
		};

		std::mutex policy_mutex;
		join_policy policy;
		struct connected_policy
		{
			game::netadr_t server{};
			uint64_t generation{};
			bool enabled{};
		};
		connected_policy established;
		// Only the existing renderer hook owns these pacing timestamps.
		clock::time_point last_frame{};
		uint64_t paced_generation{};

		void retire_join_policy()
		{
			policy.current_join = false;
			policy.enabled = false;
			policy.join_started = false;
			policy.observed_attempt = false;
		}

		bool active_join_policy(uint64_t& generation)
		{
			const auto state = game::clientUIActives->connectionState;
			std::lock_guard lock(policy_mutex);
			if (!established.enabled && (!policy.current_join || !policy.join_started))
			{
				generation = 0;
				return false;
			}
			const auto connected = state >= game::CA_CONNECTED;
			const auto server = connected ? party::get_connected_server() : game::netadr_t{};
			const auto frontend = game::Com_IsRunningUILevel();
			const auto gameplay = state == game::CA_ACTIVE && !frontend && game::Com_IsInGame() &&
				game::CL_LocalClient_IsActive(0);
			const auto local_match = gameplay && game::SV_Loaded();

			// A failed/pending new query must not remove the established match's cap.
			if (established.enabled && (!connected || server != established.server || local_match))
			{
				established = {};
			}
			if (policy.current_join && policy.join_started)
			{
				if (!connected)
				{
					policy.awaiting_departure = false;
					if (policy.observed_attempt && state < game::CA_CONNECTING)
					{
						retire_join_policy();
					}
					else if (state >= game::CA_CONNECTING)
					{
						policy.observed_attempt = true;
					}
					else if (clock::now() >= policy.pending_deadline)
					{
						retire_join_policy();
					}
				}
				else
				{
					// A changed peer proves departure; the same peer needs a state transition.
					if (policy.awaiting_departure && server != policy.prior_server) policy.awaiting_departure = false;
					if (local_match && !policy.awaiting_departure)
					{
						retire_join_policy();
					}
					else if (!policy.awaiting_departure && server == policy.server)
					{
						if (policy.enabled) established = {policy.server, policy.generation, true};
						retire_join_policy(); // Consume this generation; retries begin a fresh one.
					}
					else if (!policy.observed_attempt && clock::now() >= policy.pending_deadline)
					{
						retire_join_policy();
					}
				}
			}
			generation = established.enabled ? established.generation : 0;
			return established.enabled && gameplay && !local_match;
		}

		int effective_limit()
		{
			auto* preference = game::Dvar_FindVar("com_maxfps");
			if (preference && preference->type == game::DVAR_TYPE_SESSIONMODE_BASE_DVAR)
			{
				preference = game::Dvar_GetSessionModeSpecificDvar(preference,
					static_cast<game::eModes>(game::Com_SessionMode_GetMode()));
			}
			if (preference && preference->type == game::DVAR_TYPE_INT && preference->current.value.integer > 0)
			{
				return std::min(150, preference->current.value.integer);
			}
			return 150;
		}
	}

	uint64_t begin_join(const game::netadr_t& server)
	{
		std::lock_guard lock(policy_mutex);
		const auto generation = policy.generation + 1;
		policy = {};
		policy.server = server;
		policy.generation = generation;
		policy.current_join = true;
		return generation;
	}

	bool is_current_join(const game::netadr_t& server, const uint64_t generation)
	{
		std::lock_guard lock(policy_mutex);
		return policy.current_join && policy.generation == generation && policy.server == server;
	}

	void accept_verified_join_info(const game::netadr_t& server, const uint64_t generation,
	                              const std::string_view value, const bool dedicated)
	{
		std::lock_guard lock(policy_mutex);
		if (!policy.current_join || policy.generation != generation || policy.server != server || policy.info_received)
		{
			return;
		}
		policy.info_received = true;
		policy.enabled = dedicated && network::is_ip_address(server) && value == fixed_limit;
	}

	bool prepare_native_join(const game::netadr_t& server, const uint64_t generation)
	{
		const auto state = game::clientUIActives->connectionState;
		std::lock_guard lock(policy_mutex);
		if (!policy.current_join || policy.generation != generation || policy.server != server) return false;
		policy.join_started = true;
		policy.awaiting_departure = state >= game::CA_CONNECTED;
		policy.prior_server = policy.awaiting_departure ? party::get_connected_server() : game::netadr_t{};
		policy.observed_attempt = state >= game::CA_CONNECTING && state < game::CA_CONNECTED;
		policy.pending_deadline = clock::now() + std::chrono::seconds{60};
		return true;
	}

	void observe_native_join_start(const game::netadr_t& server, const uint64_t generation)
	{
		const auto state = game::clientUIActives->connectionState;
		std::lock_guard lock(policy_mutex);
		if (!policy.current_join || policy.generation != generation || policy.server != server || !policy.join_started) return;
		if (state < game::CA_CONNECTED)
		{
			policy.awaiting_departure = false;
			established = {}; // Capture departure even if rendering paused through it.
		}
		else if (policy.awaiting_departure && party::get_connected_server() != policy.prior_server)
		{
			policy.awaiting_departure = false;
			established = {};
		}
		if (state >= game::CA_CONNECTING && state < game::CA_CONNECTED) policy.observed_attempt = true;
	}

	void before_renderer_frame()
	{
		// Dedicated execution must return before consulting client-only symbols.
		if (game::is_server()) return;
		uint64_t generation{};
		if (!active_join_policy(generation))
		{
			last_frame = {};
			paced_generation = generation;
			return;
		}
		if (paced_generation != generation)
		{
			last_frame = {};
			paced_generation = generation;
		}

		// Never set/archive a player's dvar: only constrain this frame interval.
		const auto limit = effective_limit();
		const auto interval = std::chrono::nanoseconds{(1'000'000'000LL + limit - 1) / limit};
		if (last_frame != clock::time_point{}) std::this_thread::sleep_until(last_frame + interval);
		last_frame = clock::now(); // No catch-up bursts after a delayed frame.
	}
}
