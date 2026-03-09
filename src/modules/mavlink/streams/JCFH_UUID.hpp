/****************************************************************************
 *
 *   Copyright (c) 2020 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#ifndef JCFH_UUID_HPP
#define JCFH_UUID_HPP

#include <uORB/topics/jcfh_uuid.h>

class MavlinkStreamUuidData : public MavlinkStream
{
public:
	static MavlinkStream *new_instance(Mavlink *mavlink) { return new MavlinkStreamUuidData(mavlink); }
	static constexpr const char *get_name_static() { return "UUID_DATA"; }
	static constexpr uint16_t get_id_static() { return MAVLINK_MSG_ID_UUID_DATA; }

	const char *get_name() const override { return get_name_static(); }
	uint16_t get_id() override { return get_id_static(); }

	unsigned get_size() override
	{
		return MAVLINK_MSG_ID_UUID_DATA_LEN + MAVLINK_NUM_NON_PAYLOAD_BYTES;
	}

	bool request_message(float param2 = 0.0f, float param3 = 0.0f, float param4 = 0.0f,
			     float param5 = 0.0f, float param6 = 0.0f, float param7 = 0.0f) override
	{
		(void)param2;
		(void)param3;
		(void)param4;
		(void)param5;
		(void)param6;
		(void)param7;

		jcfh_uuid_s uorb_msg{};

		if (_jcfh_uuid_sub.copy(&uorb_msg)) {
			return send_uuid_data(uorb_msg.uuid);
		}

		return false;
	}

private:
	explicit MavlinkStreamUuidData(Mavlink *mavlink) : MavlinkStream(mavlink) {}

	uORB::Subscription _jcfh_uuid_sub{ORB_ID(jcfh_uuid)};

	bool send_uuid_data(const uint8_t *uuid)
	{
		mavlink_uuid_data_t msg{};
		memcpy(msg.uuid, uuid, sizeof(msg.uuid));
		mavlink_msg_uuid_data_send_struct(_mavlink->get_channel(), &msg);
		return true;
	}

	bool send() override
	{
		jcfh_uuid_s uorb_msg{};

		if (_jcfh_uuid_sub.update(&uorb_msg)) {
			return send_uuid_data(uorb_msg.uuid);
		}

		return false;
	}
};

#endif // JCFH_UUID_HPP
