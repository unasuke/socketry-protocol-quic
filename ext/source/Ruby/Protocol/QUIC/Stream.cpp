//
//  Stream.cpp
//  This file is part of the "Protocol::QUIC" project and released under the MIT License.
//
//  Created by Samuel Williams on 27/4/2023.
//  Copyright, 2023, by Samuel Williams. All rights reserved.
//

#include "Stream.hpp"
#include "Connection.hpp"
#include "Callback.hpp"

#include <Protocol/QUIC/BufferedStream.hpp>

VALUE Ruby_Protocol_QUIC_Stream = Qnil;

namespace Ruby::Protocol::QUIC {

	class Stream : public ::Protocol::QUIC::BufferedStream {
	public:
		VALUE self;
	
	private:
		VALUE _connection;
	public:
		Stream(VALUE self, VALUE connection, VALUE stream_id) : ::Protocol::QUIC::BufferedStream(*Ruby_Protocol_QUIC_Connection_get(connection), RB_NUM2LL(stream_id)), self(self), _connection(connection) {}
	
		virtual ~Stream() {}
	
		// Hand received data to Ruby (`Stream#received(data, fin)`) instead of buffering it here. The data is consumed immediately, so the flow control windows are extended by the same amount; otherwise a long lived connection would stall once the initial windows are used up.
		void receive_data(std::size_t offset, const void * data, std::size_t size, std::uint32_t flags) override
		{
			auto connection = _connection_handle();
			
			if (size > 0) {
				ngtcp2_conn_extend_max_stream_offset(connection, _stream_id, size);
				ngtcp2_conn_extend_max_offset(connection, size);
			}
			
			VALUE arguments[2] = {
				rb_str_new(static_cast<const char *>(data), size),
				(flags & NGTCP2_STREAM_DATA_FLAG_FIN) ? Qtrue : Qfalse,
			};
			
			protected_call(self, "received", 2, arguments);
		}
	
		// The remote peer reset its sending side (RESET_STREAM): `Stream#reset_received(error_code)`.
		void reset(std::size_t final_size, std::uint64_t error_code) override
		{
			::Protocol::QUIC::BufferedStream::reset(final_size, error_code);
			
			VALUE arguments[1] = {ULL2NUM(error_code)};
			protected_call(self, "reset_received", 1, arguments);
		}
	
		// The stream is fully closed: `Stream#closed(error_code)`.
		void close(std::uint32_t flags, std::uint64_t error_code) override
		{
			::Protocol::QUIC::BufferedStream::close(flags, error_code);
			
			VALUE arguments[1] = {ULL2NUM(error_code)};
			protected_call(self, "closed", 1, arguments);
		}
	
		void write(VALUE data)
		{
			_output_buffer.append(RSTRING_PTR(data), RSTRING_LEN(data));
		}
	
		void finish()
		{
			_output_buffer.close();
		}
	
		// Abandon the stream in both directions (RESET_STREAM and STOP_SENDING) with the given application error code.
		void abort(std::uint64_t error_code)
		{
			_output_buffer.stop_sending();
			shutdown(error_code);
		}
	
		void mark() {
			rb_gc_mark_movable(self);
			rb_gc_mark_movable(_connection);
		}
	
		void compact() {
			self = rb_gc_location(self);
			_connection = rb_gc_location(_connection);
		}
	
	private:
		ngtcp2_conn * _connection_handle()
		{
			return connection().native_handle();
		}
	};

}

static void Ruby_Protocol_QUIC_Stream_mark(void *data) {
	if (data) {
		reinterpret_cast<Ruby::Protocol::QUIC::Stream *>(data)->mark();
	}
}

static void Ruby_Protocol_QUIC_Stream_compact(void *data) {
	if (data) {
		reinterpret_cast<Ruby::Protocol::QUIC::Stream *>(data)->compact();
	}
}

static void Ruby_Protocol_QUIC_Stream_free(void *data) {
	if (data) {
		delete reinterpret_cast<Ruby::Protocol::QUIC::Stream *>(data);
	}
}

static size_t Ruby_Protocol_QUIC_Stream_size(const void *data) {
	return sizeof(Ruby::Protocol::QUIC::Stream);
}

const rb_data_type_t Ruby_Protocol_QUIC_Stream_type = {
	.wrap_struct_name = "Protocol::QUIC::Stream",
	.function = {
		.dmark = Ruby_Protocol_QUIC_Stream_mark,
		.dfree = Ruby_Protocol_QUIC_Stream_free,
		.dsize = Ruby_Protocol_QUIC_Stream_size,
		.dcompact = Ruby_Protocol_QUIC_Stream_compact,
	},
	.data = NULL,
	.flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

::Protocol::QUIC::Stream * Ruby_Protocol_QUIC_Stream_get(VALUE self)
{
	::Protocol::QUIC::Stream *address;
	
	TypedData_Get_Struct(self, ::Protocol::QUIC::Stream, &Ruby_Protocol_QUIC_Stream_type, address);
	
	return address;
}

VALUE Ruby_Protocol_QUIC_Stream_allocate(VALUE klass) {
	return TypedData_Wrap_Struct(klass, &Ruby_Protocol_QUIC_Stream_type, NULL);
}

static VALUE Ruby_Protocol_QUIC_Stream_initialize(VALUE self, VALUE connection, VALUE stream_id) {
	DATA_PTR(self) = new Ruby::Protocol::QUIC::Stream(self, connection, stream_id);
	
	return self;
}

static Ruby::Protocol::QUIC::Stream * Ruby_Protocol_QUIC_Stream_get_ruby(VALUE self)
{
	auto stream = dynamic_cast<Ruby::Protocol::QUIC::Stream *>(Ruby_Protocol_QUIC_Stream_get(self));
	
	if (!stream) rb_raise(rb_eRuntimeError, "stream is not initialized");
	
	return stream;
}

static VALUE Ruby_Protocol_QUIC_Stream_stream_id(VALUE self) {
	return LL2NUM(Ruby_Protocol_QUIC_Stream_get_ruby(self)->stream_id());
}

// Append data to the output buffer. It is sent by the next `send_packets` of the connection.
static VALUE Ruby_Protocol_QUIC_Stream_write(VALUE self, VALUE data) {
	StringValue(data);
	auto stream = Ruby_Protocol_QUIC_Stream_get_ruby(self);
	
	return Ruby::Protocol::QUIC::guard([&]{
		stream->write(data);
		return Qnil;
	});
}

// Mark the end of the output (STREAM FIN). It is sent by the next `send_packets` of the connection.
static VALUE Ruby_Protocol_QUIC_Stream_finish(VALUE self) {
	Ruby_Protocol_QUIC_Stream_get_ruby(self)->finish();
	
	return Qnil;
}

// Reset the stream in both directions with an application error code.
static VALUE Ruby_Protocol_QUIC_Stream_abort(VALUE self, VALUE error_code) {
	Ruby_Protocol_QUIC_Stream_get_ruby(self)->abort(NUM2ULL(error_code));
	
	return Qnil;
}

void Init_Ruby_Protocol_QUIC_Stream(VALUE Protocol_QUIC) {
	Ruby_Protocol_QUIC_Stream =
			rb_define_class_under(Protocol_QUIC, "Stream", rb_cObject);

	rb_define_alloc_func(Ruby_Protocol_QUIC_Stream, Ruby_Protocol_QUIC_Stream_allocate);
	rb_define_method(Ruby_Protocol_QUIC_Stream, "initialize", Ruby_Protocol_QUIC_Stream_initialize, 2);
	
	rb_define_method(Ruby_Protocol_QUIC_Stream, "stream_id", Ruby_Protocol_QUIC_Stream_stream_id, 0);
	rb_define_method(Ruby_Protocol_QUIC_Stream, "write", Ruby_Protocol_QUIC_Stream_write, 1);
	rb_define_method(Ruby_Protocol_QUIC_Stream, "finish", Ruby_Protocol_QUIC_Stream_finish, 0);
	rb_define_method(Ruby_Protocol_QUIC_Stream, "abort", Ruby_Protocol_QUIC_Stream_abort, 1);
}
