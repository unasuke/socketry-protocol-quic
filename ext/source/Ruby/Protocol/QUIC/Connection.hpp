//
//  Connection.h
//  This file is part of the "Protocol::QUIC" project and released under the MIT License.
//
//  Created by Samuel Williams on 27/4/2023.
//  Copyright, 2023, by Samuel Williams. All rights reserved.
//

#pragma once

#include <ruby.h>

#include <Protocol/QUIC/Connection.hpp>

#include <cstddef>

namespace Ruby::Protocol::QUIC {

	// A native Stream refers to its native Connection (its destructor calls Connection::remove_stream, and
	// ~Connection walks the remaining streams). Ruby may free a connection and its streams in the same GC cycle, in
	// any order, so the connection counts its live streams and defers its own deletion until the last one is gone.
	struct StreamOwner {
		std::size_t live_streams = 0;
		bool released = false;

		virtual ~StreamOwner() = default;
	};

	// Delete a connection from its GC free function, or defer it while streams still refer to it.
	template <typename ConnectionType>
	void release_connection(ConnectionType * connection)
	{
		if (connection->live_streams > 0) {
			connection->released = true;
		} else {
			delete connection;
		}
	}

}

#ifdef __cplusplus
extern "C" {
#endif

extern VALUE Ruby_Protocol_QUIC_Connection;

extern const rb_data_type_t Ruby_Protocol_QUIC_Connection_type;

void Init_Ruby_Protocol_QUIC_Connection(VALUE Protocol_QUIC);

::Protocol::QUIC::Connection * Ruby_Protocol_QUIC_Connection_get(VALUE self);

#ifdef __cplusplus
}
#endif
