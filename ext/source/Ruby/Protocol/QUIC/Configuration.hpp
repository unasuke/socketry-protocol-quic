//
//  Configuration.h
//  This file is part of the "Protocol::QUIC" project and released under the MIT License.
//
//  Created by Samuel Williams on 27/4/2023.
//  Copyright, 2023, by Samuel Williams. All rights reserved.
//

#pragma once

#include <ruby.h>

#include <Protocol/QUIC/Configuration.hpp>

#include <optional>
#include <cstdint>

#include <ngtcp2/ngtcp2.h>

namespace Protocol::QUIC {
	// Defined in Protocol/QUIC/Connection.cpp; prints ngtcp2's debug log to stderr.
	void log_printf(void *user_data, const char *fmt, ...);
}

namespace Ruby::Protocol::QUIC {

	// Transport parameters that a Ruby application can override. They are applied to each server connection right after it is created, because `Protocol::QUIC::Connection::setup` overwrites the values set by `Configuration::setup`.
	class Configuration : public ::Protocol::QUIC::Configuration {
	public:
		std::optional<ngtcp2_duration> max_idle_timeout;
		std::optional<std::uint64_t> initial_max_streams_bidi;
		std::optional<std::uint64_t> initial_max_streams_uni;
		bool debug = false;
		
		void setup(ngtcp2_settings *settings, ngtcp2_transport_params *params) override
		{
			::Protocol::QUIC::Configuration::setup(settings, params);
			
			if (debug) settings->log_printf = ::Protocol::QUIC::log_printf;
		}

		void apply(ngtcp2_transport_params & params) const
		{
			if (max_idle_timeout) params.max_idle_timeout = *max_idle_timeout;
			if (initial_max_streams_bidi) params.initial_max_streams_bidi = *initial_max_streams_bidi;
			if (initial_max_streams_uni) params.initial_max_streams_uni = *initial_max_streams_uni;
		}
	};

}

#ifdef __cplusplus
extern "C" {
#endif

extern VALUE Ruby_Protocol_QUIC_Configuration;

void Init_Ruby_Protocol_QUIC_Configuration(VALUE Protocol_QUIC);

::Protocol::QUIC::Configuration * Ruby_Protocol_QUIC_Configuration_get(VALUE self);

Ruby::Protocol::QUIC::Configuration * Ruby_Protocol_QUIC_Configuration_get_ruby(VALUE self);

#ifdef __cplusplus
}
#endif
