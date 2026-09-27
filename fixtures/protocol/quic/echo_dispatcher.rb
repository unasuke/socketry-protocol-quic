# frozen_string_literal: true

require "protocol/quic"
require "protocol/quic/echo_stream"

module Protocol::QUIC
	class EchoServer < Server
		def create_stream(stream_id)
			EchoStream.new(self, stream_id)
		end
	end

	class EchoDispatcher < Dispatcher
		def create_server(socket, address, packet_header)
			EchoServer.new(self, configuration, tls_context, socket, address, packet_header, nil)
		end
	end
end
