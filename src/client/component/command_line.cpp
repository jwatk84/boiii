#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "scheduler.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace command_line
{
	namespace
	{
		// The engine only applies +set for a hardcoded dvar whitelist, stub the check to allow all
		void allow_all_startup_dvars()
		{
			constexpr uint8_t stub[] = {0x31, 0xC0, 0xC3}; // xor eax, eax; ret
			utils::hook::copy(game::select(0x14133B830, 0x140183C80), stub, sizeof(stub));
		}

		std::vector<std::string> get_console_lines()
		{
			const std::string command_line = GetCommandLineA();

			std::vector<std::string> lines{};
			auto in_quotes = false;
			auto start = std::string::npos;

			for (size_t i = 0; i < command_line.size(); ++i)
			{
				const auto character = command_line[i];

				if (character == '"')
				{
					in_quotes = !in_quotes;
					continue;
				}

				if (in_quotes || character != '+')
				{
					continue;
				}

				if (start != std::string::npos)
				{
					lines.emplace_back(command_line.substr(start, i - start));
				}

				start = i + 1;
			}

			if (start != std::string::npos)
			{
				lines.emplace_back(command_line.substr(start));
			}

			return lines;
		}

		std::string get_startup_dvar_buffer()
		{
			std::string buffer{};

			for (const auto& line : get_console_lines())
			{
				const auto separator = line.find_first_of(" \t");
				if (separator == std::string::npos)
				{
					continue;
				}

				const auto command = utils::string::to_lower(line.substr(0, separator));
				if (command != "set" && command != "seta")
				{
					continue;
				}

				buffer.append("set ");
				buffer.append(line.substr(separator + 1));
				buffer.append("\n");
			}

			return buffer;
		}

		// Applied again after the config file was read, so the command line wins over it
		void apply_startup_dvars()
		{
			const auto buffer = get_startup_dvar_buffer();
			if (buffer.empty())
			{
				return;
			}

			game::Cbuf_ExecuteBuffer(0, game::ControllerIndex_t::CONTROLLER_INDEX_0, buffer.data());
		}
	}

	struct component final : generic_component
	{
		void post_unpack() override
		{
			allow_all_startup_dvars();

			// Only the client reads a config file
			const auto pipeline = game::is_server()
				                      ? scheduler::pipeline::main
				                      : scheduler::pipeline::dvars_loaded;

			scheduler::once(apply_startup_dvars, pipeline);
		}
	};
}

REGISTER_COMPONENT(command_line::component)
