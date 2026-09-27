# frozen_string_literal: true

require "protocol/quic/address"
require "protocol/quic/client_context"
require "protocol/quic/server_context"
require "protocol/quic/echo_dispatcher"
require "timeout"

module Protocol::QUIC
	# A client-side stream that accumulates received data and publishes it once the peer finishes.
	class CollectingStream < Stream
		def initialize(...)
			super
			@input = Thread::Queue.new
			@buffer = +""
		end

		attr :input

		def received(data, fin)
			@buffer << data
			@input << @buffer if fin
		end
	end

	class StreamTestClient < Client
		def initialize(...)
			super
			@streams = Thread::Queue.new
		end

		attr :streams

		def handshake_completed
			# Opening the stream and buffering output here is safe: neither `write` nor `finish`
			# re-enter ngtcp2; the data is flushed by `connect`'s next `send_packets`.
			stream = open_bidirectional_stream
			stream.write("Hello World")
			stream.finish
			@streams << stream
		end

		def create_stream(stream_id)
			CollectingStream.new(self, stream_id)
		end
	end
end

describe Protocol::QUIC::Stream do
	let(:configuration) {Protocol::QUIC::Configuration.new}
	let(:server_context) {Protocol::QUIC::Fixtures.server_context}
	let(:client_context) {Protocol::QUIC::Fixtures.client_context}
	let(:address) {Protocol::QUIC::Fixtures.address}

	def bound_socket(address)
		socket = Protocol::QUIC::Socket.new(address.family, ::Socket::SOCK_DGRAM, ::Socket::IPPROTO_UDP)
		socket.bind(address)
		socket
	end

	it "exchanges data over a client-initiated bidirectional stream" do
		server_socket = bound_socket(address)
		local_address = server_socket.local_address
		dispatcher = Protocol::QUIC::EchoDispatcher.new(configuration, server_context)

		listener = Thread.new do
			loop do
				dispatcher.listen(server_socket)
			end
		end

		client_socket = Protocol::QUIC::Socket.new(local_address.family, ::Socket::SOCK_DGRAM, ::Socket::IPPROTO_UDP)
		client_socket.connect(local_address)

		client = Protocol::QUIC::StreamTestClient.new(configuration, client_context, client_socket, local_address, 1)

		client_thread = Thread.new do
			client.connect
		end

		Timeout.timeout(10) do
			stream = client.streams.pop

			# The first client-initiated bidirectional stream has id 0 (RFC 9000 §2.1):
			expect(stream.stream_id).to be == 0

			# The server echoes the payload back:
			expect(stream.input.pop).to be == "Hello World"
		end
	ensure
		listener&.kill
		listener&.join
		client_thread&.kill
		client_thread&.join
	end
end
