
#include "Server.hpp"

#include "Dispatcher.hpp"
#include "Configuration.hpp"
#include "Connection.hpp"
#include "TLS/ServerContext.hpp"
#include "Socket.hpp"
#include "Address.hpp"
#include "PacketHeader.hpp"

#include "Stream.hpp"
#include "Callback.hpp"

#include <unordered_map>
#include <vector>

#include <ngtcp2/ngtcp2.h>

VALUE Ruby_Protocol_QUIC_Server = Qnil;

namespace Ruby::Protocol::QUIC {

	class Server : public ::Protocol::QUIC::Server {
	public:
		VALUE self;
	
	private:
		VALUE _dispatcher;
		VALUE _configuration;
		VALUE _tls_context;
		VALUE _socket;
		VALUE _remote_address;
		std::unordered_map<::Protocol::QUIC::StreamID, VALUE> _ruby_streams;
		bool _disconnected = false;
		
		// Connection IDs issued after the connection was created (NEW_CONNECTION_ID). The base dispatcher only registers the IDs that exist when the connection is created, so packets sent to a later ID (quic-go switches to one during the handshake) would otherwise be treated as a new connection.
		std::vector<ngtcp2_cid> _issued_cids;
		
		// Wakes up `run` whenever the dispatcher has processed packets for this connection. `Scheduler::Semaphore` cannot be used for this: it calls `Queue#pop(timeout)` with a positional argument, which Ruby >= 3.2 treats as `non_block` and raises `ThreadError` instead of waiting.
		VALUE _wakeup = Qnil;
		
		void wakeup()
		{
			if (RTEST(rb_funcall(_wakeup, rb_intern("empty?"), 0))) {
				rb_funcall(_wakeup, rb_intern("push"), 1, Qtrue);
			}
		}
		
		// @returns false if the timeout expired.
		bool wait_for_wakeup(const std::optional<::Protocol::QUIC::Timestamp> & timeout)
		{
			VALUE result;
			
			if (timeout) {
				::Protocol::QUIC::Timestamp now;
				double seconds = (*timeout - now).as_seconds();
				if (seconds <= 0) return false;
				
				VALUE options = rb_hash_new();
				rb_hash_aset(options, ID2SYM(rb_intern("timeout")), rb_float_new(seconds));
				result = rb_funcallv_kw(_wakeup, rb_intern("pop"), 1, &options, RB_PASS_KEYWORDS);
			} else {
				result = rb_funcall(_wakeup, rb_intern("pop"), 0);
			}
			
			return !NIL_P(result);
		}
	
		static bool remote_bidirectional(::Protocol::QUIC::StreamID stream_id)
		{
			// Client-initiated bidirectional streams have the two least significant bits 0b00 (RFC 9000 §2.1).
			return (stream_id & 0x3) == 0;
		}
	
	public:
		Server(VALUE self, VALUE dispatcher, VALUE configuration, VALUE tls_context, VALUE socket, VALUE remote_address, VALUE packet_header, VALUE ocid) : ::Protocol::QUIC::Server(*Ruby_Protocol_QUIC_Dispatcher_get(dispatcher), *Ruby_Protocol_QUIC_Configuration_get(configuration), *Ruby_Protocol_QUIC_TLS_ServerContext_get(tls_context), *Ruby_Protocol_QUIC_Socket_get(socket), *Ruby_Protocol_QUIC_Address_get(remote_address), *Ruby_Protocol_QUIC_PacketHeader_get(packet_header), nullptr), self(self), _dispatcher(dispatcher), _configuration(configuration), _tls_context(tls_context), _socket(socket), _remote_address(remote_address)
		{
			_wakeup = rb_funcall(rb_path2class("Thread::Queue"), rb_intern("new"), 0);
			
			// `Connection::setup` overwrites the transport parameters provided by `Configuration::setup`, so apply the Ruby configuration here. This is allowed until the handshake keys are installed, which happens only after the first packet is processed.
			auto ruby_configuration = Ruby_Protocol_QUIC_Configuration_get_ruby(configuration);
			auto params = *ngtcp2_conn_get_local_transport_params(_connection);
			ruby_configuration->apply(params);
			
			if (auto result = ngtcp2_conn_set_local_transport_params(_connection, &params)) {
				throw std::runtime_error(ngtcp2_strerror(result));
			}
		}
		
		virtual ~Server() {}
	
		::Protocol::QUIC::Stream * create_stream(::Protocol::QUIC::StreamID stream_id) override
		{
			VALUE argument = RB_LL2NUM(stream_id);
			VALUE stream = protected_call(self, "create_stream", 1, &argument);
			
			if (NIL_P(stream)) throw std::runtime_error("create_stream returned nil");
			
			_ruby_streams[stream_id] = stream;
		
			return Ruby_Protocol_QUIC_Stream_get(stream);
		}
	
		void handshake_completed() override
		{
			protected_call(self, "handshake_completed");
		}
	
		// Invoked once when the connection is gone (closed by either side, idle timeout, or a fatal error): `Server#disconnected`.
		void disconnect() override
		{
			if (_disconnected) return;
			_disconnected = true;
			
			::Protocol::QUIC::Server::disconnect();
			
			// Retired IDs are no longer returned by `scids()`, so remove every ID this connection issued; a stale entry would point at a freed server.
			for (auto & cid : _issued_cids) {
				::Protocol::QUIC::Server::_dispatcher.disassociate(&cid);
			}
			_issued_cids.clear();
			
			_ruby_streams.clear();
			
			// Wake up `run` so that it can return:
			wakeup();
			
			try {
				protected_call(self, "disconnected");
			} catch (...) {
				// The connection is already gone; nothing else to unwind.
			}
		}
	
		bool disconnected() const noexcept {return _disconnected;}
	
		void generate_connection_id(ngtcp2_cid *cid, std::size_t length, uint8_t *token) override
		{
			::Protocol::QUIC::Server::generate_connection_id(cid, length, token);
			
			::Protocol::QUIC::Server::_dispatcher.associate(cid, this);
			_issued_cids.push_back(*cid);
		}
	
		// The idle timeout is a silent close (RFC 9000 §10.1): do not try to send CONNECTION_CLOSE, which would fail.
		Status handle_error(int result, std::string_view reason = "") override
		{
			if (result == NGTCP2_ERR_IDLE_CLOSE) {
				set_last_error(result, reason);
				disconnect();
				return Status::DRAINING;
			}
			
			return ::Protocol::QUIC::Server::handle_error(result, reason);
		}
	
		void stream_close(::Protocol::QUIC::Stream * stream, std::int32_t flags, std::uint64_t error_code) override
		{
			auto stream_id = stream->stream_id();
			::Protocol::QUIC::Server::stream_close(stream, flags, error_code);
			_ruby_streams.erase(stream_id);
			
			// ngtcp2 does not raise the stream limit by itself; allow the client to open a new stream for each one that is closed.
			if (remote_bidirectional(stream_id)) {
				ngtcp2_conn_extend_max_streams_bidi(_connection, 1);
			}
		}
	
		// Unlike the base class, keep the stream registered after RESET_STREAM: ngtcp2 still calls `stream_close` for it later, and the base class would fail to find it then.
		void stream_reset(::Protocol::QUIC::Stream * stream, std::size_t final_size, std::uint64_t error_code) override
		{
			stream->reset(final_size, error_code);
		}
	
		// Close the connection with an application error code (CONNECTION_CLOSE of type 0x1d).
		void close_with_application_error(std::uint64_t error_code)
		{
			if (_disconnected) return;
			
			ngtcp2_ccerr_set_application_error(&_last_error, error_code, nullptr, 0);
			
			if (is_closing() || is_draining()) {
				disconnect();
			} else {
				close();
			}
		}
	
		// The dispatcher calls `send_packets` (which ends with `send_stream_data`) after every packet it hands to this connection.
		Status send_stream_data() override
		{
			auto status = ::Protocol::QUIC::Server::send_stream_data();
			wakeup();
			return status;
		}
	
		// Run the connection's timers (loss detection, idle timeout) and the closing and draining periods. Packets are received and answered by the dispatcher. Returns when the connection is gone.
		void run()
		{
			while (!_disconnected) {
				bool woken = wait_for_wakeup(expiry_timeout());
				
				if (_disconnected) break;
				
				if (!woken) {
					handle_expiry();
					if (_disconnected) break;
				}
				
				if (is_closing() || is_draining()) {
					drain();
					break;
				}
			}
		}
	
		void mark() {
			rb_gc_mark_movable(self);
			rb_gc_mark_movable(_dispatcher);
			rb_gc_mark_movable(_configuration);
			rb_gc_mark_movable(_tls_context);
			rb_gc_mark_movable(_socket);
			rb_gc_mark_movable(_remote_address);
			rb_gc_mark_movable(_wakeup);
		
			for (auto & [stream_id, ruby_stream] : _ruby_streams) {
				rb_gc_mark_movable(ruby_stream);
			}
		}
	
		void compact() {
			self = rb_gc_location(self);
			_dispatcher = rb_gc_location(_dispatcher);
			_configuration = rb_gc_location(_configuration);
			_tls_context = rb_gc_location(_tls_context);
			_socket = rb_gc_location(_socket);
			_remote_address = rb_gc_location(_remote_address);
			_wakeup = rb_gc_location(_wakeup);
		
			for (auto & [stream_id, ruby_stream] : _ruby_streams) {
				ruby_stream = rb_gc_location(ruby_stream);
			}
		}
	};

}

static void Ruby_Protocol_QUIC_Server_mark(void *data)
{
	if (data) {
		reinterpret_cast<Ruby::Protocol::QUIC::Server *>(data)->mark();
	}
}

static void Ruby_Protocol_QUIC_Server_compact(void *data)
{
	if (data) {
		reinterpret_cast<Ruby::Protocol::QUIC::Server *>(data)->compact();
	}
}

static void Ruby_Protocol_QUIC_Server_free(void *data)
{
	if (data) {
		delete reinterpret_cast<::Protocol::QUIC::Server *>(data);
	}
}

static size_t Ruby_Protocol_QUIC_Server_size(const void *data) {
	return sizeof(Ruby::Protocol::QUIC::Server);
}

static const rb_data_type_t Ruby_Protocol_QUIC_Server_type = {
	.wrap_struct_name = "Protocol::QUIC::Server",
	.function = {
		.dmark = Ruby_Protocol_QUIC_Server_mark,
		.dfree = Ruby_Protocol_QUIC_Server_free,
		.dsize = Ruby_Protocol_QUIC_Server_size,
		.dcompact = Ruby_Protocol_QUIC_Server_compact,
	},
	.parent = &Ruby_Protocol_QUIC_Connection_type,
	.data = NULL,
	.flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

::Protocol::QUIC::Server * Ruby_Protocol_QUIC_Server_get(VALUE self)
{
	::Protocol::QUIC::Server *server;

	TypedData_Get_Struct(self, ::Protocol::QUIC::Server, &Ruby_Protocol_QUIC_Server_type, server);

	return server;
}

static VALUE Ruby_Protocol_QUIC_Server_allocate(VALUE klass) {
	return TypedData_Wrap_Struct(klass, &Ruby_Protocol_QUIC_Server_type, NULL);
}

// Dispatcher & dispatcher, Configuration & configuration, TLS::ServerContext & tls_context, Socket & socket, const Address & remote_address, const ngtcp2_pkt_hd & packet_header, ngtcp2_cid *ocid = nullptr
static VALUE Ruby_Protocol_QUIC_Server_initialize(VALUE self, VALUE dispatcher, VALUE configuration, VALUE tls_context, VALUE socket, VALUE remote_address, VALUE packet_header, VALUE ocid) {
	return Ruby::Protocol::QUIC::guard([&]{
		DATA_PTR(self) = new Ruby::Protocol::QUIC::Server(self, dispatcher, configuration, tls_context, socket, remote_address, packet_header, ocid);
		return self;
	});
}

static Ruby::Protocol::QUIC::Server * Ruby_Protocol_QUIC_Server_get_ruby(VALUE self)
{
	auto server = dynamic_cast<Ruby::Protocol::QUIC::Server *>(Ruby_Protocol_QUIC_Server_get(self));
	
	if (!server) rb_raise(rb_eRuntimeError, "server is not initialized");
	
	return server;
}

static VALUE Ruby_Protocol_QUIC_Server_send_packets(VALUE self) {
	auto server = Ruby_Protocol_QUIC_Server_get_ruby(self);
	
	if (server->disconnected()) return Qnil;
	
	return Ruby::Protocol::QUIC::guard([&]{
		server->send_packets();
		return Qnil;
	});
}

// Drive the connection until it is gone. Blocks the calling fiber; other fibers keep running while it waits.
static VALUE Ruby_Protocol_QUIC_Server_run(VALUE self) {
	auto server = Ruby_Protocol_QUIC_Server_get_ruby(self);
	
	return Ruby::Protocol::QUIC::guard([&]{
		server->run();
		return Qnil;
	});
}

static VALUE Ruby_Protocol_QUIC_Server_close(VALUE self, VALUE error_code) {
	auto server = Ruby_Protocol_QUIC_Server_get_ruby(self);
	std::uint64_t code = NUM2ULL(error_code);
	
	return Ruby::Protocol::QUIC::guard([&]{
		server->close_with_application_error(code);
		return Qnil;
	});
}

static VALUE Ruby_Protocol_QUIC_Server_disconnected_p(VALUE self) {
	return Ruby_Protocol_QUIC_Server_get_ruby(self)->disconnected() ? Qtrue : Qfalse;
}

void Init_Ruby_Protocol_QUIC_Server(VALUE Protocol_QUIC) {
	Ruby_Protocol_QUIC_Server =
			rb_define_class_under(Protocol_QUIC, "Server", rb_cObject);

	rb_define_alloc_func(Ruby_Protocol_QUIC_Server, Ruby_Protocol_QUIC_Server_allocate);
	rb_define_method(Ruby_Protocol_QUIC_Server, "initialize", Ruby_Protocol_QUIC_Server_initialize, 7);
	
	rb_define_method(Ruby_Protocol_QUIC_Server, "send_packets", Ruby_Protocol_QUIC_Server_send_packets, 0);
	rb_define_method(Ruby_Protocol_QUIC_Server, "run", Ruby_Protocol_QUIC_Server_run, 0);
	rb_define_method(Ruby_Protocol_QUIC_Server, "close", Ruby_Protocol_QUIC_Server_close, 1);
	rb_define_method(Ruby_Protocol_QUIC_Server, "disconnected?", Ruby_Protocol_QUIC_Server_disconnected_p, 0);
}
