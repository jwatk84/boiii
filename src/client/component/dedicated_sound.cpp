#include <std_include.hpp>

#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/flags.hpp>
#include <utils/hook.hpp>
#include <utils/nt.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>

namespace dedicated_sound
{
	namespace
	{
		// This is a narrow ABI backport of the dedicated sound-bank work from
		// Ezz-lol/boiii-free. Keep these definitions local: the target branch
		// predates Ezz's large game-structure reorganization.
		namespace abi
		{
			using qboolean = std::uint32_t;
			using sd_byte = std::uint8_t;
			using stream_file_id = std::int32_t;
			using stream_id = std::int32_t;

			enum class sound_bank_state : std::int32_t
			{
			};

			enum class sound_command_type : std::uint32_t
			{
			};

			union sound_command
			{
				void* payload;
				std::uint64_t raw;
			};

#pragma pack(push, 1)
			struct sound_asset_bank_header
			{
				std::uint32_t magic;
				std::uint32_t version;
				std::uint32_t entry_size;
				std::uint32_t checksum_size;
				std::uint32_t dependency_size;
				std::uint32_t entry_count;
				std::uint32_t dependency_count;
				std::uint32_t padding_1c;
				std::int64_t file_size;
				std::int64_t entry_offset;
				std::uint8_t remaining_data[0x7D0];
			};

			struct sound_asset_bank_entry
			{
				std::uint32_t id;
				std::uint32_t size;
				std::uint32_t frame_count;
				std::uint32_t order;
				std::uint64_t offset;
				std::uint8_t frame_rate_index;
				std::uint8_t channel_count;
				std::uint8_t looping;
				std::uint8_t format;
				std::uint8_t envelope_loudness[4];
				std::uint16_t envelope_time_1;
				std::uint16_t envelope_time_2;
			};

			struct sound_asset_bank_load
			{
				sound_asset_bank_header header;
				char filename[256];
				sound_asset_bank_entry* entries;
				std::uint32_t entry_count;
				stream_file_id file_handle;
				qboolean indices_loaded;
				qboolean indices_allocated;
			};

			// Only this verified prefix is needed for diagnostics.
			struct sound_bank
			{
				const char* name;
				const char* zone;
			};

			struct sound_bank_load
			{
				const sound_bank* bank;
				sound_asset_bank_load stream_asset_bank;
				sound_asset_bank_load load_asset_bank;
				sound_asset_bank_entry* loaded_entries;
				sd_byte* loaded_data;
				std::uint32_t loaded_asset_count;
				std::uint32_t loaded_asset_total;
				std::uint32_t loaded_entry_count;
				std::uint32_t loaded_data_size;
				std::uint32_t priority;
				qboolean patch_zone;
				std::uint8_t unknown_1260[4];
				stream_id stream_request_id;
				qboolean pending_io;
				qboolean io_error;
				sound_bank_state state;
				std::uint32_t pending_io_count;
			};

			// The fields between bank_magic and load_gate are intentionally opaque.
			// Their aggregate size is fixed by the verified 0x39594-byte engine type.
			struct sound_bank_globals_overlay
			{
				std::uint32_t bank_magic;
				std::uint8_t data_before_load_gate[0x38D6C];
				qboolean load_gate;
			};

			struct sound_local_overlay
			{
				std::int32_t magic;
				qboolean initialized;
				qboolean paused;
				float timescale;
				float script_timescale;
				std::int32_t time;
				std::int32_t loop_time;
				std::int32_t pause_time;
				std::uint32_t frame;
				std::uint8_t padding_24[4];
				const void* global_constants;
				std::int32_t cinematic_voices_playing;
				std::int32_t cinematic_timestamp;
				qboolean cinematic_update;
				qboolean force_pause;
				std::int32_t playback_id_counter;
				std::uint32_t default_hash;
			};
#pragma pack(pop)

			struct database_load_overlay
			{
				std::int32_t file;
				std::uint8_t padding_04[4];
				const char* filename;
				void* blocks;
				std::int32_t flags;
			};

			struct sound_queue;

			static_assert(sizeof(sound_asset_bank_header) == 0x800);
			static_assert(sizeof(sound_command) == 0x8);
			static_assert(offsetof(sound_asset_bank_header, entry_size) == 0x8);
			static_assert(offsetof(sound_asset_bank_header, entry_count) == 0x14);
			static_assert(offsetof(sound_asset_bank_header, entry_offset) == 0x28);
			static_assert(sizeof(sound_asset_bank_entry) == 0x24);
			static_assert(offsetof(sound_asset_bank_entry, offset) == 0x10);
			static_assert(sizeof(sound_asset_bank_load) == 0x918);
			static_assert(offsetof(sound_asset_bank_load, entries) == 0x900);
			static_assert(offsetof(sound_asset_bank_load, file_handle) == 0x90C);
			static_assert(sizeof(sound_bank_load) == 0x1278);
			static_assert(offsetof(sound_bank_load, load_asset_bank) == 0x920);
			static_assert(offsetof(sound_bank_load, loaded_entries) == 0x1238);
			static_assert(offsetof(sound_bank_load, loaded_data_size) == 0x1254);
			static_assert(sizeof(sound_bank_globals_overlay) == 0x38D74);
			static_assert(offsetof(sound_bank_globals_overlay, load_gate) == 0x38D70);
			static_assert(sizeof(sound_local_overlay) == 0x48);
			static_assert(offsetof(sound_local_overlay, initialized) == 0x4);
			static_assert(offsetof(sound_local_overlay, default_hash) == 0x44);
			static_assert(sizeof(database_load_overlay) == 0x20);
			static_assert(offsetof(database_load_overlay, flags) == 0x18);
		}

		namespace engine
		{
			game::symbol<abi::database_load_overlay> database_load{0x0, 0x1468FD4A0};
			game::symbol<abi::sound_bank_globals_overlay> sound_bank_globals{0x0, 0x14A8AAA80};
			game::symbol<abi::sound_local_overlay> sound_local{0x0, 0x141189800};
			game::symbol<abi::qboolean> sound_enabled_flag{0x0, 0x14A63D4EC};

			game::symbol<bool(const char*, std::int32_t, void*, const char*, void*, void*, std::uint8_t*,
			                  std::uint32_t, std::int32_t)> database_load_xfile{0x0, 0x1401A4920};
			game::symbol<void(abi::sound_bank_load*)> sound_enqueue_loaded_assets{0x0, 0x14064C900};
			game::symbol<bool(abi::sound_bank_load*, abi::sound_asset_bank_load*, bool)> sound_start_toc_read{
				0x0, 0x14064CBB0
			};
			game::symbol<void(abi::sound_bank_load*)> sound_bank_load_error{0x0, 0x14064B460};
			game::symbol<void(abi::sound_bank_load*, abi::stream_file_id, std::int64_t, std::size_t, void*)>
				sound_stream_read{0x0, 0x14064CC50};
			game::symbol<int(const void*, const void*)> sound_compare_asset_loads{0x0, 0x14064A010};
			game::symbol<void()> sound_error_if_globals_trashed{0x0, 0x140644DE0};
			game::symbol<void()> sound_load_sounds_wait{0x0, 0x14064C6E0};
			game::symbol<void(abi::sound_command_type, std::uint64_t, abi::sound_command)> sound_command{0x0,
				0x140545CB0};
			game::symbol<void(abi::sound_queue*, abi::sound_command_type, std::uint32_t, abi::sound_command)>
				sound_queue_add{
				0x0, 0x140549A20
			};
			game::symbol<void()> sound_process_queue{0x0, 0x140546720};
			game::symbol<void(abi::sound_queue*)> sound_queue_flush{0x0, 0x140549C20};
			game::symbol<abi::qboolean()> sound_active{0x0, 0x1405479D0};
			game::symbol<void()> sound_init{0x0, 0x1406459B0};
			game::symbol<bool()> game_sound_enabled{0x0, 0x140584DB0};
			game::symbol<bool()> sound_should_init{0x0, 0x140647FB0};
			game::symbol<void()> sound_update{0x0, 0x140643340};
			game::symbol<void*(std::uint32_t)> sound_get_duck_by_id{0x0, 0x14064BF00};
			game::symbol<void*(std::uint32_t, const char*)> sound_get_reverb{0x0, 0x14064C320};
			game::symbol<void(std::int32_t)> enter_critical_section{0x0, 0x140055230};
			game::symbol<void(std::int32_t)> leave_critical_section{0x0, 0x140055280};
			game::symbol<void(void*, std::size_t, std::size_t, int (*)(const void*, const void*))> sort{0x0,
				0x140AB6020};
		}

		constexpr std::int32_t sound_bank_critical_section = 0x10;
		constexpr std::uint32_t sound_bank_magic = 0x12233445;
		constexpr std::int32_t sound_xfile_flags = 0x1000C00;

		std::mutex allocation_mutex;
		std::unordered_set<void*> sound_allocations;

		utils::hook::detour enqueue_loaded_assets_hook;
		utils::hook::detour start_toc_read_hook;
		utils::hook::detour process_queue_hook;
		utils::hook::detour flush_queue_hook;
		utils::hook::detour game_sound_enabled_hook;
		utils::hook::detour sound_should_init_hook;
		utils::hook::detour bank_load_error_hook;

		abi::sd_byte* allocate_sound_memory(const char* name, const std::uint32_t size, const std::uint32_t alignment)
		{
			if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
			{
				std::fprintf(stderr, "[Sound] Invalid allocation request for %s (%u bytes, alignment %u).\n",
				             name, size, alignment);
				return nullptr;
			}

			auto* const allocation = _aligned_malloc(size, alignment);
			if (!allocation)
			{
				std::fprintf(stderr, "[Sound] Unable to allocate %u bytes for %s.\n", size, name);
				return nullptr;
			}

			try
			{
				std::lock_guard lock(allocation_mutex);
				sound_allocations.emplace(allocation);
			}
			catch (...)
			{
				_aligned_free(allocation);
				std::fprintf(stderr, "[Sound] Unable to track allocation for %s.\n", name);
				return nullptr;
			}

			return static_cast<abi::sd_byte*>(allocation);
		}

		bool free_sound_memory(void* allocation)
		{
			if (!allocation)
			{
				return false;
			}

			bool owned = false;
			{
				std::lock_guard lock(allocation_mutex);
				owned = sound_allocations.erase(allocation) != 0;
			}

			if (owned)
			{
				_aligned_free(allocation);
			}

			// Unknown or already-freed pointers are deliberately ignored. The
			// remove-bank hook can therefore clear aliased fields idempotently
			// without ever passing an engine-owned pointer to _aligned_free.
			return owned;
		}

		void report_bank_error(const abi::sound_bank_load* load, const char* reason)
		{
			const auto* const zone = load && load->bank && load->bank->zone && *load->bank->zone
				                         ? load->bank->zone
				                         : "unknown";

			const auto game_folder = utils::nt::library{}.get_folder();
			const auto sound_path = game_folder.empty() ? std::filesystem::path{"zone/snd"}
			                                             : game_folder / "zone" / "snd";
			const auto path = sound_path.string();

			std::fprintf(stderr,
			             "[Sound] Could not load a sound bank for zone '%s': %s. Verify the bank files under '%s', "
			             "or restart with -nosnd to use the dedicated-server fallback.\n",
			             zone, reason, path.c_str());
			std::fflush(stderr);
		}

		void fail_bank_load(abi::sound_bank_load* load, const char* reason)
		{
			report_bank_error(load, reason);
			bank_load_error_hook.invoke<void>(load);
		}

		constexpr bool calculate_loaded_span(const abi::sound_asset_bank_entry* entries, const std::uint32_t count,
		                                     std::uint64_t& first_offset, std::uint32_t& span)
		{
			if (!entries || count == 0)
			{
				return false;
			}

			first_offset = entries[0].offset;
			std::uint64_t end_offset = first_offset;

			for (std::uint32_t i = 0; i < count; ++i)
			{
				const auto& entry = entries[i];
				if (entry.offset > (std::numeric_limits<std::uint64_t>::max)() - entry.size)
				{
					return false;
				}

				first_offset = (std::min)(first_offset, entry.offset);
				end_offset = (std::max)(end_offset, entry.offset + entry.size);
			}

			const auto total_size = end_offset - first_offset;
			if (total_size == 0 || total_size > (std::numeric_limits<std::uint32_t>::max)())
			{
				return false;
			}

			span = static_cast<std::uint32_t>(total_size);
			return true;
		}

		constexpr bool loaded_span_happy_path_test()
		{
			abi::sound_asset_bank_entry entries[3]{};
			entries[0].offset = 0x140;
			entries[0].size = 0x10;
			entries[1].offset = 0x100;
			entries[1].size = 0x20;
			entries[2].offset = 0x120;
			entries[2].size = 0x08;
			std::uint64_t first_offset = 0;
			std::uint32_t span = 0;
			return calculate_loaded_span(entries, 3, first_offset, span) && first_offset == 0x100 && span == 0x50;
		}

		constexpr bool loaded_span_overflow_test()
		{
			abi::sound_asset_bank_entry entry{};
			entry.offset = (std::numeric_limits<std::uint64_t>::max)() - 1;
			entry.size = 2;
			std::uint64_t first_offset = 0;
			std::uint32_t span = 0;
			return !calculate_loaded_span(&entry, 1, first_offset, span);
		}

		static_assert(loaded_span_happy_path_test());
		static_assert(loaded_span_overflow_test());

		utils::hook::detour database_load_xfile_hook;

		bool database_load_xfile_stub(const char* path, const std::int32_t file, void* file_buffer,
		                              const char* filename, void* blocks, void* interrupt, std::uint8_t* buffer,
		                              const std::uint32_t side, const std::int32_t flags)
		{
			const auto succeeded = database_load_xfile_hook.invoke<bool>(
				path, file, file_buffer, filename, blocks, interrupt, buffer, side, flags);

			if (succeeded && (engine::database_load->flags & sound_xfile_flags) != 0)
			{
				engine::sound_bank_globals->load_gate = 0;
				engine::sound_load_sounds_wait();
			}

			return succeeded;
		}

		void enqueue_loaded_assets(abi::sound_bank_load* load)
		{
			engine::sound_error_if_globals_trashed();

			load->loaded_asset_count = 0;
			load->loaded_entry_count = load->load_asset_bank.header.entry_count;

			if (load->loaded_entry_count == 0)
			{
				load->loaded_entries = nullptr;
				load->loaded_data = nullptr;
				load->loaded_data_size = 0;
				load->loaded_asset_total = 0;
				engine::sound_error_if_globals_trashed();
				return;
			}

			if (load->load_asset_bank.header.entry_size != sizeof(abi::sound_asset_bank_entry))
			{
				fail_bank_load(load, "unexpected sound-bank entry size");
				return;
			}

			std::uint64_t first_offset = 0;
			std::uint32_t loaded_data_size = 0;
			if (!calculate_loaded_span(load->load_asset_bank.entries, load->loaded_entry_count, first_offset,
			                           loaded_data_size))
			{
				fail_bank_load(load, "invalid sound-bank entry table");
				return;
			}

			if (first_offset > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
			{
				fail_bank_load(load, "sound-bank data offset is outside the supported range");
				return;
			}

			const auto entries_size_64 = static_cast<std::uint64_t>(load->loaded_entry_count) *
			                             sizeof(abi::sound_asset_bank_entry);
			if (entries_size_64 > (std::numeric_limits<std::uint32_t>::max)())
			{
				fail_bank_load(load, "sound-bank entry table is too large");
				return;
			}

			engine::enter_critical_section(sound_bank_critical_section);
			load->loaded_entries = reinterpret_cast<abi::sound_asset_bank_entry*>(
				allocate_sound_memory(load->load_asset_bank.filename, static_cast<std::uint32_t>(entries_size_64), 0x100));
			load->loaded_data = allocate_sound_memory(load->load_asset_bank.filename, loaded_data_size, 0x1000);
			engine::leave_critical_section(sound_bank_critical_section);

			if (!load->loaded_entries || !load->loaded_data)
			{
				free_sound_memory(load->loaded_entries);
				free_sound_memory(load->loaded_data);
				load->loaded_entries = nullptr;
				load->loaded_data = nullptr;
				fail_bank_load(load, "sound-bank memory allocation failed");
				return;
			}

			load->loaded_data_size = loaded_data_size;
			std::memcpy(load->loaded_entries, load->load_asset_bank.entries, static_cast<std::size_t>(entries_size_64));
			for (std::uint32_t i = 0; i < load->loaded_entry_count; ++i)
			{
				load->loaded_entries[i].offset -= first_offset;
			}

			load->loaded_asset_total = load->loaded_entry_count;
			engine::sound_stream_read(load, load->load_asset_bank.file_handle, static_cast<std::int64_t>(first_offset),
			                          loaded_data_size, load->loaded_data);
			engine::sort(load->loaded_entries, load->loaded_entry_count, sizeof(abi::sound_asset_bank_entry),
			             engine::sound_compare_asset_loads.get());

			engine::sound_error_if_globals_trashed();
		}

		bool start_toc_read(abi::sound_bank_load* load, abi::sound_asset_bank_load* asset_bank,
		                    [[maybe_unused]] const bool streamed)
		{
			engine::sound_error_if_globals_trashed();
			asset_bank->entries = nullptr;
			asset_bank->entry_count = 0;

			if (!asset_bank->filename[0] || asset_bank->header.entry_count == 0)
			{
				return false;
			}

			if (asset_bank->header.entry_size != sizeof(abi::sound_asset_bank_entry))
			{
				fail_bank_load(load, "unexpected sound-bank table-of-contents entry size");
				return false;
			}

			if (asset_bank->header.entry_offset < 0)
			{
				fail_bank_load(load, "sound-bank table-of-contents offset is negative");
				return false;
			}

			const auto table_size = static_cast<std::uint64_t>(asset_bank->header.entry_count) *
			                        asset_bank->header.entry_size;
			if (table_size > (std::numeric_limits<std::uint32_t>::max)() - 0x7FF)
			{
				fail_bank_load(load, "sound-bank table of contents is too large");
				return false;
			}

			const auto allocation_size = static_cast<std::uint32_t>((table_size + 0x7FF) & ~0x7FFull);
			auto* const allocation = allocate_sound_memory(asset_bank->filename, allocation_size, 0x800);
			if (!allocation)
			{
				fail_bank_load(load, "table-of-contents memory allocation failed");
				return false;
			}

			asset_bank->entry_count = asset_bank->header.entry_count;
			asset_bank->entries = reinterpret_cast<abi::sound_asset_bank_entry*>(allocation);
			asset_bank->indices_allocated = 1;
			engine::sound_stream_read(load, asset_bank->file_handle, asset_bank->header.entry_offset, allocation_size,
			                          asset_bank->entries);
			engine::sound_error_if_globals_trashed();
			return true;
		}

		void free_bank_allocations_and_clear(abi::sound_bank_load* load, const int value, const std::size_t size)
		{
			free_sound_memory(load->loaded_entries);
			free_sound_memory(load->loaded_data);
			free_sound_memory(load->load_asset_bank.entries);
			free_sound_memory(load->stream_asset_bank.entries);
			std::memset(load, value, size);
		}

		utils::hook::detour sound_init_hook;

		void sound_init_stub()
		{
			sound_init_hook.invoke<void>();

			// Despite its name, G_SndEnabled returns whether this value is non-zero.
			*engine::sound_enabled_flag = 1;
			engine::sound_local->initialized = 1;
			engine::sound_bank_globals->bank_magic = sound_bank_magic;
		}

		utils::hook::detour sound_queue_add_hook;

		void sound_queue_add_stub([[maybe_unused]] abi::sound_queue* queue, const abi::sound_command_type command,
		                          const std::uint32_t size, const abi::sound_command data)
		{
			// The native QueueAdd fourth argument is a pointer-sized union value in
			// R9. Preserve those bits when forwarding it to SND_CommandSND.
			engine::sound_command(command, size, data);
		}

		utils::hook::detour sound_active_hook;

		abi::qboolean sound_active_stub()
		{
			engine::sound_local->initialized = 1;
			return sound_active_hook.invoke<abi::qboolean>();
		}

		utils::hook::detour sound_update_hook;

		void sound_update_stub()
		{
			const auto default_hash = engine::sound_local->default_hash;
			if (engine::sound_get_duck_by_id(default_hash) && engine::sound_get_reverb(default_hash, "default"))
			{
				sound_update_hook.invoke<void>();
			}
		}

		bool return_true()
		{
			return true;
		}

		void process_queue_stub()
		{
		}

		void flush_queue_stub([[maybe_unused]] abi::sound_queue* queue)
		{
		}

		void bank_load_error_stub(abi::sound_bank_load* load)
		{
			report_bank_error(load, "the engine rejected the bank");
			bank_load_error_hook.invoke<void>(load);
		}

		void enable_sound()
		{
			// Provenance: Ezz-lol/boiii-free 67d9bf69, with shutdown and -nosnd
			// follow-ups c3dd6f3d, 24670f1a, 6c38c614, and modular cleanup 8fcd0dbc.
			bank_load_error_hook.create(engine::sound_bank_load_error.get(), bank_load_error_stub);

			// Dedicated binaries replace allocator calls with null. Restore the
			// client-equivalent bank load stages using locally tracked allocations.
			enqueue_loaded_assets_hook.create(engine::sound_enqueue_loaded_assets.get(), enqueue_loaded_assets);
			start_toc_read_hook.create(engine::sound_start_toc_read.get(), start_toc_read);

			// The server's native removal path only clears SndBankLoad because it
			// normally never allocates these buffers. Free our allocations first.
			utils::hook::call(0x14064AB30_g, free_bank_allocations_and_clear);

			// Match client behavior by waiting for level sound banks after the XPAK
			// finishes loading.
			database_load_xfile_hook.create(engine::database_load_xfile.get(), database_load_xfile_stub);

			// A dedicated server has no initialized asynchronous sound queue.
			// Process submissions immediately and bypass the absent queue workers.
			sound_queue_add_hook.create(engine::sound_queue_add.get(), sound_queue_add_stub);
			process_queue_hook.create(engine::sound_process_queue.get(), process_queue_stub);
			flush_queue_hook.create(engine::sound_queue_flush.get(), flush_queue_stub);

			sound_active_hook.create(engine::sound_active.get(), sound_active_stub);
			sound_init_hook.create(engine::sound_init.get(), sound_init_stub);
			game_sound_enabled_hook.create(engine::game_sound_enabled.get(), return_true);
			sound_should_init_hook.create(engine::sound_should_init.get(), return_true);

			// SNDL_Update dereferences default duck/reverb assets during shutdown.
			// Trimmed or partially loaded installations may not have them.
			sound_update_hook.create(engine::sound_update.get(), sound_update_stub);

			std::printf("[Sound] Dedicated sound-bank processing enabled.\n");
		}
	}

	struct component final : server_component
	{
		void post_unpack() override
		{
			if (utils::flags::has_flag("nosnd"))
			{
				std::printf("[Sound] Dedicated sound-bank processing disabled by -nosnd.\n");
				return;
			}

			enable_sound();
		}
	};
}

REGISTER_COMPONENT(dedicated_sound::component)
