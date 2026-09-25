#include <std_include.hpp>

#include "game_event.hpp"
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace dedicated_vehicle
{
	namespace
	{
		// Provenance: Ezz-lol/boiii-free fefeba3e488d25f4da153fc8044f6fa579ac718a,
		// with the vehicle follow-ups 937f147d0c7a15f5b86bf022c0628d4a2d1d2c91
		// and 1d64404bedb64c325ad5f1c43d340145702b7762.
		//
		// These overlays intentionally describe only the dedicated-server ABI fields
		// touched below. Keeping them local avoids importing Ezz's unrelated game
		// structure refactor into this focused backport.
		struct vec3
		{
			float x;
			float y;
			float z;
		};

		struct vec4
		{
			float x;
			float y;
			float z;
			float w;
		};

		struct matrix43
		{
			vec4 x;
			vec4 y;
			vec4 z;
			vec4 position;
		};

		static_assert(sizeof(vec3) == 0xC);
		static_assert(sizeof(vec4) == 0x10);
		static_assert(sizeof(matrix43) == 0x40);

#pragma pack(push, 1)
		struct vehicle_path_position
		{
			std::byte pad_0000[0x2C];
			vec3 origin;
			vec3 angles;
			std::byte pad_0044[0x120];
		};

		enum class vehicle_move_state : std::int32_t
		{
			stop = 0,
		};

		struct vehicle
		{
			vehicle_path_position path_position;
			std::byte pad_0164[0x190];
			vehicle_move_state move_state;
			std::byte pad_02F8[0x22C];
			std::int32_t stopping;
		};

		enum class vehicle_type : std::uint16_t
		{
			boat = 4,
		};

		struct vehicle_definition
		{
			const char *name;
			vehicle_type type;
			std::byte pad_000A[0x17E];
			std::int32_t use_heli_bone_controllers;
			std::byte pad_018C[0x844];
			std::int32_t is_sentient;
		};

		struct game_entity
		{
			std::byte pad_0000[0x270];
			vehicle *vehicle_data;
		};

		struct wheel_constraint;

		struct nitrous_vehicle
		{
			std::byte pad_0000[0x2C8];
			game_entity *owner;
			std::byte pad_02D0[0x8];
			const vehicle_definition *definition;
			std::byte pad_02E0[0x18];
			std::uint32_t flags;
			std::byte pad_02FC[0x198];
			std::int32_t colliding_wheel_count;
			std::byte pad_0498[0x408];
			wheel_constraint *wheels[6];
		};

		struct path_constraint
		{
			std::byte pad_0000[0x20];
			matrix43 path_matrix;
			vec4 body1_local_position;
			std::byte pad_0070[0x8];
			std::int32_t timestamp;
		};
#pragma pack(pop)

		static_assert(sizeof(vehicle_path_position) == 0x164);
		static_assert(offsetof(vehicle_path_position, origin) == 0x2C);
		static_assert(offsetof(vehicle_path_position, angles) == 0x38);
		static_assert(offsetof(vehicle, move_state) == 0x2F4);
		static_assert(offsetof(vehicle, stopping) == 0x524);
		static_assert(offsetof(vehicle_definition, type) == 0x8);
		static_assert(offsetof(vehicle_definition, use_heli_bone_controllers) == 0x188);
		static_assert(offsetof(vehicle_definition, is_sentient) == 0x9D0);
		static_assert(offsetof(game_entity, vehicle_data) == 0x270);
		static_assert(offsetof(nitrous_vehicle, owner) == 0x2C8);
		static_assert(offsetof(nitrous_vehicle, definition) == 0x2D8);
		static_assert(offsetof(nitrous_vehicle, flags) == 0x2F8);
		static_assert(offsetof(nitrous_vehicle, colliding_wheel_count) == 0x494);
		static_assert(offsetof(nitrous_vehicle, wheels) == 0x8A0);
		static_assert(offsetof(path_constraint, path_matrix) == 0x20);
		static_assert(offsetof(path_constraint, body1_local_position) == 0x60);
		static_assert(offsetof(path_constraint, timestamp) == 0x78);

		constexpr auto local_client_count = 2u;
		constexpr auto cg_size = 0x342720u;
		constexpr auto cgs_size = 0x1E940u;
		constexpr auto centity_pool_size = 0x3F0000u;

		alignas(16) std::array<std::byte, cg_size * local_client_count> cg_pool{};
		alignas(8) std::array<std::byte, cgs_size * local_client_count> cgs_pool{};
		alignas(8) std::array<std::byte, centity_pool_size * local_client_count> centity_pool{};

		// Dedicated code leaves these client-game pool pointers null. Vehicle code
		// reaches client-game helpers, so provide correctly sized static backing
		// stores without pretending that a full local client exists.
		constexpr auto builtin_cg_array = 0x14222BCB0ull;
		constexpr auto builtin_cgs_array = 0x14222BCB8ull;
		constexpr auto builtin_centity_array = 0x14222BCC0ull;

		constexpr auto path_constraint_update = 0x1405C8920ull;
		constexpr auto nitrous_vehicle_is_path_moving = 0x1405C5D60ull;
		constexpr auto nitrous_vehicle_pause_physics = 0x1405C5EF0ull;
		constexpr auto nitrous_vehicle_unpause_physics = 0x1405C65F0ull;
		constexpr auto collide_vehicle_wheels = 0x1405D8390ull;
		constexpr auto calculate_wheel_penetration_depth = 0x14002A740ull;
		constexpr auto physics_get_current_time = 0x1405E2B70ull;
		constexpr auto scoped_critical_section_construct = 0x14004F520ull;
		constexpr auto scoped_critical_section_destruct = 0x14004F5E0ull;
		constexpr auto physics_critical_section_id = 3;
		constexpr auto normal_critical_section = 0u;
		constexpr auto initialized_vehicle_flag = 1u << 1;

		constexpr std::uintptr_t no_cg_array_error_branches[] = {
			0x14002EDF2, 0x14002F08D, 0x14002F5F6, 0x14002FFAD, 0x1400305E7, 0x140030897, 0x1400318EE, 0x140031D6B,
			0x140034DA7, 0x140050554, 0x140050A40, 0x140050C6E, 0x1400511BD, 0x1400513EE, 0x140051AB6, 0x1400797DB,
			0x140079A26, 0x140079B4A, 0x140079D48, 0x140079DA1, 0x14007A6E9, 0x14007AA5E, 0x14007AB4B, 0x14007AC06,
			0x14007ACBE, 0x14007FC34, 0x14007FE5E, 0x140080067, 0x14008013E, 0x1400801C4, 0x1400802AE, 0x140080734,
			0x140080938, 0x140082FB6, 0x1400832AC, 0x140083491, 0x140083DAE, 0x140088B12, 0x140088E4E, 0x140089132,
			0x140089272, 0x14008956E, 0x14008AD6C, 0x14008D808, 0x14008DCFC, 0x14008ED54, 0x14008F213, 0x14008F333,
			0x14008F3BA, 0x14008F423, 0x14008F63C, 0x14008F68F, 0x14008F75E, 0x14008F893, 0x14008FA3D, 0x14008FB63,
			0x14008FC8E, 0x140090767, 0x140090861, 0x140090931, 0x140090A01, 0x140090AD1, 0x140090BA1, 0x140090C71,
			0x140090EB3, 0x140090F33, 0x140090FB3, 0x140091033, 0x1400910B3, 0x140091133, 0x1400911B3, 0x140091233,
			0x140092573, 0x140092789, 0x1400927A3, 0x1400928E4, 0x1400928FE, 0x1400929CF, 0x140093F13, 0x1400947DD,
			0x140095354, 0x14009536E, 0x14009543E, 0x140095514, 0x14009552E, 0x140095655, 0x140095DBE, 0x140095E0D,
			0x14009726A, 0x14009739B, 0x1400977CE, 0x140097B22, 0x140098682, 0x1400988CD, 0x140098A7B, 0x1400991A1,
			0x14009A414, 0x14009AF14, 0x1400A174E, 0x1400A28B2, 0x1400A2B1E, 0x1400A2B8E, 0x1400A3426, 0x1400A3A1A,
			0x1400A3D34, 0x1400A4C1E, 0x1400A4D6E, 0x1400A4E98, 0x1400A572E, 0x1400A67F8, 0x1400A6B4A, 0x1400A70F7,
			0x1400A89BA, 0x1400AB2A2, 0x1400AB92C, 0x1400ACB41, 0x1400AD0EA, 0x1400ADDC4, 0x1400AE27E, 0x1400AEBE3,
			0x1400AEEEC, 0x1400AF7B3, 0x1400B02EB, 0x1400B22AE, 0x1400B2715, 0x1400B28DA, 0x1400B2A76, 0x1400B2F5F,
			0x1400B301F, 0x1400B3129, 0x1400B33F0, 0x1400B3B6A, 0x1400B6556, 0x1400B9EF3, 0x1400BAB5A, 0x1400BACEA,
			0x1400BBD27, 0x1400BD94A, 0x1400BE267, 0x1400BECC6, 0x1400BED5A, 0x1400BF105, 0x1400BFE33, 0x1400C0003,
			0x1400C14E3, 0x1400C529C, 0x1400C52ED, 0x1400C58AB, 0x1400C5ADA, 0x1400C5F6E, 0x1400C7429, 0x1400C85E8,
			0x1400CB246, 0x1400CB5DA, 0x1400CB914, 0x1400D03D0, 0x1400D06B3, 0x1400D0E3E, 0x1400D104C, 0x1400D1465,
			0x1400D157F, 0x1400D15CE, 0x1400D1744, 0x1400D316A, 0x1400D326F, 0x1400D925A, 0x1400DA3C6, 0x1400DA4D6,
			0x1400DA606, 0x1400DA9E3, 0x1400DB69B, 0x1400DBE36, 0x1400DC284, 0x1400DC7EA, 0x1400DC8DB, 0x1400DCAA2,
			0x1400DCD16, 0x1400DD083, 0x1400DEA71, 0x1400DF6D5, 0x1400E048B, 0x1400E1D01, 0x1400E1FD6, 0x1400E2092,
			0x1400E213E, 0x1400E222E, 0x1400E2304, 0x1400E23C4, 0x1400E328E, 0x1400E37D6, 0x1400E3851, 0x1400E61E3,
			0x1400E657E, 0x1400E7CE3, 0x1400E820E, 0x1400E832B, 0x1400E9A77, 0x1400E9C51, 0x1400EA815, 0x1400EC0EB,
			0x1400EDD93, 0x1400EDEBE, 0x1400EF5E7, 0x1400EF7E6, 0x1400F0D11, 0x1400F525F, 0x1400F6484, 0x1400F776D,
			0x1400F79DC, 0x1400F7AC6, 0x1400F7BEA, 0x1400F7D8A, 0x1400F7F16, 0x1400F7FDA, 0x1400F8065, 0x1400F82BF,
			0x1400F83F4, 0x1400F8608, 0x1400F8856, 0x1400F8956, 0x1400F8AA6, 0x1400F8CB6, 0x1400F90A6, 0x1400F9284,
			0x1400F9366, 0x1400F9564, 0x1400F96F9, 0x1400F987B, 0x1400F9BE5, 0x1400F9CFC, 0x1400F9DFE, 0x1400F9F29,
			0x1400FA084, 0x1400FA224, 0x1400FA328, 0x1400FA3D5, 0x1400FA4A5, 0x1400FA565, 0x1400FA61B, 0x1400FA732,
			0x1400FA8A3, 0x1400FA935, 0x1400FA9A2, 0x1400FACA6, 0x1400FADAB, 0x1400FAF3B, 0x1400FB0CB, 0x1400FB23C,
			0x1400FB45F, 0x1400FB522, 0x1400FB79D, 0x1400FB90F, 0x1400FBADD, 0x1400FBD4D, 0x1400FC004, 0x1400FC0BC,
			0x1400FC23C, 0x1400FC3F4, 0x1400FC4DD, 0x1400FC616, 0x1400FC6C9, 0x1400FC7C6, 0x1400FC8AD, 0x1400FCA11,
			0x1400FCB5B, 0x1400FCEB8, 0x1400FCF74, 0x1400FD026, 0x1400FD1DC, 0x1400FD32A, 0x1400FD47A, 0x1400FD5EA,
			0x1400FD72A, 0x1400FD87A, 0x1400FD9BA, 0x1400FDB24, 0x1400FE52B, 0x1400FE6AB, 0x1400FE7F5, 0x1400FE8C5,
			0x1400FF4DF, 0x1400FF656, 0x1400FF796, 0x1400FF90F, 0x1400FF99F, 0x1400FFA8E, 0x1401001E7, 0x1401002E5,
			0x1401003B7, 0x140100665, 0x140100813, 0x140100FC1, 0x140101048, 0x1401010DC, 0x1401011CC, 0x14010121F,
			0x140101361, 0x1401013BD, 0x1401016C0, 0x140101886, 0x1401018E6, 0x140101D36, 0x140101FD6, 0x14010234B,
			0x1401024F6, 0x1401026C3, 0x14010276A, 0x140102ABE, 0x140102D3E, 0x140102E4B, 0x140102EFF, 0x14010443E,
			0x1401046E6, 0x140104E2A, 0x140104FA8, 0x140104FFD, 0x1401050AC, 0x14010510B, 0x140105943, 0x140105B15,
			0x140105C36, 0x140105F21, 0x140106047, 0x14010616B, 0x1401062EA, 0x140106425, 0x140106487, 0x14010657E,
			0x14010665B, 0x140106B36, 0x140106F3A, 0x1401070C0, 0x140107413, 0x140107EB6, 0x140107F89, 0x1401084C6,
			0x1401086AC, 0x140108B75, 0x140108DB5, 0x140108E4F, 0x140108F24, 0x140108FEC, 0x14010918A, 0x1401094A6,
			0x1401095AC, 0x1401096C6, 0x1401097DC, 0x140109900, 0x140109A2C, 0x140109B4C, 0x140109C6C, 0x140109D8C,
			0x140109EAB, 0x140109FDC, 0x14010A0FC, 0x14010A399, 0x14010A966, 0x14010AEB1, 0x14010B028, 0x14010B2E4,
			0x14010C1D8, 0x14010C400, 0x14010CD1F, 0x14010CDC4, 0x14010CE74, 0x14010CF16, 0x14010D0B2, 0x14010D70B,
			0x14010DD48, 0x14010E1D0, 0x14010E646, 0x14010E7ED, 0x14010EAA9, 0x14010EBF4, 0x14010EC96, 0x14010EE19,
			0x14010EF3F, 0x14010F1AC, 0x14010F2A4, 0x14010F346, 0x14010F516, 0x14010F82F, 0x14010F965, 0x14010FA6A,
			0x14010FAE4, 0x14010FCE4, 0x14010FD88, 0x14010FF3A, 0x14011003D, 0x140110461, 0x14011064A, 0x1401106A4,
			0x1401108B4, 0x140110994, 0x14011103C, 0x14011113C, 0x140111214, 0x140111F0F, 0x140111F9B, 0x1401121B8,
			0x1401124B2, 0x14011266F, 0x140112A2C, 0x140112C2A, 0x14011393B, 0x140114A96, 0x1401193B1, 0x140119421,
			0x1401194AA, 0x140119791, 0x140119852, 0x140119AA1, 0x140119C82, 0x140119DC3, 0x140119F32, 0x14011A22B,
			0x14011A401, 0x14011A858, 0x14011AE32, 0x14011B169, 0x14011B346, 0x14011B48E, 0x14011BEDC, 0x14011BFBD,
			0x14011C105, 0x14011C491, 0x14011C5C0, 0x14011CA88, 0x14011DF8E, 0x14011E321, 0x14011E39B, 0x14011F672,
			0x14011FAF1, 0x140122E5F, 0x140122EB8, 0x140122F71, 0x140124B31, 0x140125152, 0x1401264BA, 0x14012BD1E,
			0x14012BDE4, 0x14012D370, 0x14012EBB4, 0x1401321CC, 0x14013240F, 0x140132DE2, 0x14013372C, 0x140134376,
			0x14013498E, 0x140134B35, 0x140134BE8, 0x140134D95, 0x14013AD57, 0x14013B024, 0x14013CE89, 0x14013DF02,
			0x14013DF4A, 0x14013FE6D, 0x14013FEDE, 0x1401412BD, 0x14014133D, 0x140141D67, 0x140141F35, 0x140141FFC,
			0x14014303B, 0x1401439FF, 0x140146F3B, 0x14014735E, 0x140147886, 0x140148697, 0x140148BE9, 0x14014C3E4,
			0x14014C42B, 0x14014D79E, 0x140150039, 0x140150153, 0x1401506CF, 0x140150D6C, 0x140151F8F, 0x14017DD8E,
			0x1401E097B, 0x1401F208E, 0x1401F3901, 0x1401F3E04, 0x1401F4F0E, 0x14020C92C, 0x14020CFAF, 0x1402132DD,
			0x14021ECB4, 0x140508E19, 0x1405BB3C4, 0x1405BC60D, 0x1405BCE8D, 0x1405BE038, 0x1405C10CB, 0x1405C5D94,
			0x1405D92A2, 0x1405D9F2D, 0x1405DCA98, 0x1405DCB40, 0x1405DCC3A, 0x1405DCCE9, 0x1405DEDFA, 0x1405E235F,
			0x1405E7AC7, 0x1405EFD45, 0x1405F2F20, 0x1405F4225, 0x1405F48E0, 0x1405F73E0, 0x1405F74DF, 0x1405F754B,
			0x1405F7F89, 0x1405F93DD, 0x1405FB514, 0x1405FB575, 0x140600DD3, 0x140601C3B, 0x1406042BC, 0x1406137EF,
			0x140616469, 0x14061659E, 0x140654751, 0x14066B0DD, 0x140670032, 0x140670766, 0x14067F3A2, 0x14068086F,
			0x140681D90, 0x140682C51, 0x140682E1E, 0x14068455A, 0x14068A72E, 0x1406903F4, 0x140693020, 0x1406AA26E,
			0x1406CC172, 0x1406CC247, 0x1406CC382, 0x1406CC4D4, 0x1406FD63E, 0x1406FE051, 0x1406FE9A1,
		};

		static_assert(std::size(no_cg_array_error_branches) == 543);

		utils::hook::detour path_constraint_update_hook;
		utils::hook::detour is_path_moving_hook;
		utils::hook::detour pause_physics_hook;
		utils::hook::detour unpause_physics_hook;

		class physics_critical_section final
		{
		  public:
			physics_critical_section()
			{
				using constructor = void (*)(physics_critical_section *, std::int32_t, std::uint32_t);
				reinterpret_cast<constructor>(game::relocate(scoped_critical_section_construct))(
					this, physics_critical_section_id, normal_critical_section);
			}

			~physics_critical_section()
			{
				using destructor = void (*)(physics_critical_section *);
				reinterpret_cast<destructor>(game::relocate(scoped_critical_section_destruct))(this);
			}

			physics_critical_section(const physics_critical_section &) = delete;
			physics_critical_section &operator=(const physics_critical_section &) = delete;

		  private:
			std::int32_t section_{};
			bool has_ownership_{};
			bool is_scoped_release_{};
			std::byte pad_0006[2]{};
			physics_critical_section *next_{};
		};

		static_assert(sizeof(physics_critical_section) == 0x10);

		matrix43 make_path_matrix(const vec3 &angles, const vec3 &origin)
		{
			constexpr auto degrees_to_radians = 0.01745329251994329576923690768489f;
			const auto pitch = angles.x * degrees_to_radians;
			const auto yaw = angles.y * degrees_to_radians;
			const auto roll = angles.z * degrees_to_radians;

			const auto cos_pitch = std::cos(pitch);
			const auto sin_pitch = std::sin(pitch);
			const auto cos_yaw = std::cos(yaw);
			const auto sin_yaw = std::sin(yaw);
			const auto cos_roll = std::cos(roll);
			const auto sin_roll = std::sin(roll);

			return {
				{cos_pitch * cos_yaw, cos_pitch * sin_yaw, -sin_pitch, 0.0f},
				{sin_roll * sin_pitch * cos_yaw - cos_roll * sin_yaw,
				 sin_roll * sin_pitch * sin_yaw + cos_roll * cos_yaw, sin_roll * cos_pitch, 0.0f},
				{cos_roll * sin_pitch * cos_yaw + sin_roll * sin_yaw,
				 cos_roll * sin_pitch * sin_yaw - sin_roll * cos_yaw, cos_roll * cos_pitch, 0.0f},
				{origin.x, origin.y, origin.z, 1.0f},
			};
		}

		std::size_t branch_instruction_length(const std::uint8_t *instruction)
		{
			// Adapted from Ezz-lol/boiii-free's fail-safe branch decoder added in
			// 72314249b8fc4ab69fdc1abed5e2da23b5dbc165.
			std::size_t prefix_length = 0;
			while (true)
			{
				const auto value = instruction[prefix_length];
				if (value == 0x26 || value == 0x2E || value == 0x36 || value == 0x3E || value == 0x64 ||
					value == 0x65 || value == 0x66 || value == 0x67 || value == 0xF0 || value == 0xF2 ||
					value == 0xF3 || (value >= 0x40 && value <= 0x4F))
				{
					++prefix_length;
					continue;
				}

				break;
			}

			const auto opcode = instruction[prefix_length];
			if (opcode == 0xEB || (opcode >= 0x70 && opcode <= 0x7F) || opcode == 0xE3)
			{
				return prefix_length + 2;
			}

			if (opcode == 0xE8 || opcode == 0xE9)
			{
				return prefix_length + 5;
			}

			if (opcode == 0x0F && instruction[prefix_length + 1] >= 0x80 && instruction[prefix_length + 1] <= 0x8F)
			{
				return prefix_length + 6;
			}

			if (opcode == 0xFF)
			{
				const auto modrm = instruction[prefix_length + 1];
				const auto mode = (modrm >> 6) & 3;
				const auto operation = (modrm >> 3) & 7;
				const auto operand = modrm & 7;

				if (operation >= 2 && operation <= 5)
				{
					auto length = prefix_length + 2;
					if (mode != 3)
					{
						if (operand == 4)
						{
							const auto sib = instruction[length++];
							if (mode == 0 && (sib & 7) == 5)
							{
								length += 4;
							}
						}
						else if (mode == 0 && operand == 5)
						{
							length += 4;
						}

						if (mode == 1)
						{
							++length;
						}
						else if (mode == 2)
						{
							length += 4;
						}
					}

					return length;
				}
			}

			throw std::runtime_error(utils::string::va("Dedicated vehicle patch expected a branch at %p, but "
													   "found opcode 0x%02X",
													   instruction, opcode));
		}

		void patch_no_cg_array_error_branches()
		{
			std::array<std::size_t, std::size(no_cg_array_error_branches)> lengths{};

			// Validate the complete table before changing any executable bytes. If the
			// dedicated executable ever drifts, startup fails without a partial patch.
			for (std::size_t i = 0; i < std::size(no_cg_array_error_branches); ++i)
			{
				const auto *instruction =
					reinterpret_cast<const std::uint8_t *>(game::relocate(no_cg_array_error_branches[i]));
				lengths[i] = branch_instruction_length(instruction);
			}

			for (std::size_t i = 0; i < std::size(no_cg_array_error_branches); ++i)
			{
				utils::hook::nop(game::relocate(no_cg_array_error_branches[i]), lengths[i]);
			}
		}

		void assign_client_game_pools()
		{
			*reinterpret_cast<void **>(game::relocate(builtin_cg_array)) = cg_pool.data();
			*reinterpret_cast<void **>(game::relocate(builtin_cgs_array)) = cgs_pool.data();

			auto **entity_pools = reinterpret_cast<void **>(game::relocate(builtin_centity_array));
			entity_pools[0] = centity_pool.data();
			entity_pools[1] = centity_pool.data() + centity_pool_size;
		}

		void reset_client_game_pools()
		{
			std::memset(cg_pool.data(), 0, cg_pool.size());
			std::memset(cgs_pool.data(), 0, cgs_pool.size());
			std::memset(centity_pool.data(), 0, centity_pool.size());
		}

		void path_constraint_update_stub(path_constraint *constraint, const nitrous_vehicle *nitrous)
		{
			if (!constraint || !nitrous)
			{
				return;
			}

			const auto *owner = nitrous->owner;
			const auto *vehicle_data = owner ? owner->vehicle_data : nullptr;
			if (vehicle_data && nitrous->definition)
			{
				constraint->path_matrix =
					make_path_matrix(vehicle_data->path_position.angles, vehicle_data->path_position.origin);

				constraint->body1_local_position.x = 0.0f;
				constraint->body1_local_position.y = 0.0f;
				constraint->body1_local_position.w = 0.0f;
				const auto lower_constraint =
					nitrous->colliding_wheel_count >= 3 || nitrous->definition->type == vehicle_type::boat;
				constraint->body1_local_position.z = lower_constraint ? -6.0f : 0.0f;
				return;
			}

			using get_current_time = std::int32_t (*)();
			const auto current_time = reinterpret_cast<get_current_time>(game::relocate(physics_get_current_time))();
			if (constraint->timestamp > current_time)
			{
				constraint->timestamp = current_time;
			}
		}

		bool is_path_moving_stub(nitrous_vehicle *nitrous)
		{
			const auto *owner = nitrous ? nitrous->owner : nullptr;
			const auto *vehicle_data = owner ? owner->vehicle_data : nullptr;
			return vehicle_data && (vehicle_data->move_state != vehicle_move_state::stop || !vehicle_data->stopping);
		}

		void unpause_physics_stub(nitrous_vehicle *nitrous)
		{
			unpause_physics_hook.invoke<void>(nitrous);
			if (!nitrous)
			{
				return;
			}

			const physics_critical_section lock{};
			if (!nitrous->definition || nitrous->definition->use_heli_bone_controllers ||
				(nitrous->flags & initialized_vehicle_flag) == 0)
			{
				return;
			}

			using collide_wheels = void (*)(nitrous_vehicle *);
			reinterpret_cast<collide_wheels>(game::relocate(collide_vehicle_wheels))(nitrous);

			using calculate_penetration = void (*)(wheel_constraint *);
			const auto calculate =
				reinterpret_cast<calculate_penetration>(game::relocate(calculate_wheel_penetration_depth));
			for (auto *wheel : nitrous->wheels)
			{
				if (wheel)
				{
					calculate(wheel);
				}
			}
		}

		void pause_physics_stub(nitrous_vehicle *nitrous, const bool shutdown)
		{
			if (nitrous && nitrous->definition && !nitrous->definition->is_sentient)
			{
				pause_physics_hook.invoke<void>(nitrous, shutdown);
			}
		}
	} // namespace

	struct component final : server_component
	{
		void post_unpack() override
		{
			patch_no_cg_array_error_branches();
			assign_client_game_pools();
			game_event::on_g_init_game(reset_client_game_pools);

			path_constraint_update_hook.create(game::relocate(path_constraint_update), path_constraint_update_stub);
			is_path_moving_hook.create(game::relocate(nitrous_vehicle_is_path_moving), is_path_moving_stub);
			unpause_physics_hook.create(game::relocate(nitrous_vehicle_unpause_physics), unpause_physics_stub);
			pause_physics_hook.create(game::relocate(nitrous_vehicle_pause_physics), pause_physics_stub);
		}
	};
} // namespace dedicated_vehicle

REGISTER_COMPONENT(dedicated_vehicle::component)
