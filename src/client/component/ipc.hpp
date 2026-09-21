#pragma once

#include <string>
#include <vector>

namespace ipc
{
	// Queue a single-line JSON message for the launcher (newline appended). Safe from any thread;
	// dropped if the pipe is down (messages are not persisted across reconnects).
	void send_message(std::string line);

	// Push a presence update on the next main-thread frame instead of waiting for the 5s tick.
	void flush_presence();

	bool request_workshop_install(const std::string& request_id, const std::vector<std::string>& item_ids);
}
