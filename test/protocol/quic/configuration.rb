# frozen_string_literal: true

require "protocol/quic/address"
require "protocol/quic/client_context"
require "protocol/quic/server_context"
require "protocol/quic/test_dispatcher"
require "protocol/quic/test_stream"
require "tempfile"
require "timeout"

module Protocol::QUIC
	# Reports each completed handshake, and does nothing else, so that the connection goes idle.
	class HandshakeClient < Client
		def initialize(...)
			super
			@handshakes = Thread::Queue.new
		end
		
		attr :handshakes
		
		def handshake_completed
			@handshakes << true
		end
	end
	
	# Opens bidirectional streams until the peer's limit is reached, and reports how many it opened.
	class StreamLimitClient < Client
		def initialize(...)
			super
			@opened = Thread::Queue.new
		end
		
		attr :opened
		
		def handshake_completed
			count = 0
			
			10.times do
				open_bidirectional_stream
				count += 1
			rescue RuntimeError
				break
			end
			
			@opened << count
		end
		
		def create_stream(stream_id)
			TestStream.new(self, stream_id)
		end
	end
end

describe Protocol::QUIC::Configuration do
	let(:configuration) {subject.new}
	
	it "generates a static secret" do
		secret = configuration.static_secret
		
		expect(secret).to be_a(String)
		expect(secret.bytesize).to be == 32
	end
	
	with "#max_idle_timeout=" do
		it "rejects a negative duration" do
			expect{configuration.max_idle_timeout = -1}.to raise_exception(ArgumentError)
		end
	end
	
	with "a connection" do
		let(:server_configuration) {subject.new}
		let(:client_configuration) {subject.new}
		let(:server_context) {Protocol::QUIC::Fixtures.server_context}
		let(:client_context) {Protocol::QUIC::Fixtures.client_context}
		let(:address) {Protocol::QUIC::Fixtures.address}
		
		def with_connection(client_class)
			server_socket = Protocol::QUIC::Socket.new(address.family, ::Socket::SOCK_DGRAM, ::Socket::IPPROTO_UDP)
			server_socket.bind(address)
			local_address = server_socket.local_address
			dispatcher = Protocol::QUIC::TestDispatcher.new(server_configuration, server_context)
			
			listener = Thread.new do
				loop do
					dispatcher.listen(server_socket)
				end
			end
			
			client_socket = Protocol::QUIC::Socket.new(local_address.family, ::Socket::SOCK_DGRAM, ::Socket::IPPROTO_UDP)
			client_socket.connect(local_address)
			
			client = client_class.new(client_configuration, client_context, client_socket, local_address, 1)
			
			client_thread = Thread.new do
				client.connect
			end
			
			yield client, client_thread
		ensure
			listener&.kill
			listener&.join
			client_thread&.kill
			client_thread&.join
		end
		
		# Capture what the native code writes to file descriptor 2 (`std::cerr`).
		def capture_stderr
			file = Tempfile.new("protocol-quic-stderr")
			saved = STDERR.dup
			STDERR.reopen(file)
			
			begin
				yield
			ensure
				STDERR.reopen(saved)
				saved.close
			end
			
			file.rewind
			file.read
		ensure
			file&.close!
		end
		
		it "limits the streams a client can open to the server's initial_max_streams_bidi" do
			# Distinct from the default of 3 set by `Protocol::QUIC::Connection::setup`:
			server_configuration.initial_max_streams_bidi = 5
			
			with_connection(Protocol::QUIC::StreamLimitClient) do |client, client_thread|
				Timeout.timeout(10) do
					expect(client.opened.pop).to be == 5
				end
			end
		end
		
		it "closes an idle connection after the server's max_idle_timeout" do
			server_configuration.max_idle_timeout = 0.5
			
			with_connection(Protocol::QUIC::HandshakeClient) do |client, client_thread|
				Timeout.timeout(10) do
					client.handshakes.pop
				end
				
				# Without an idle timeout, `Client#connect` would never return:
				expect(client_thread.join(5)).to be == client_thread
			end
		end
		
		it "prints the ngtcp2 log when debug is enabled" do
			client_configuration.debug = true
			
			output = capture_stderr do
				with_connection(Protocol::QUIC::HandshakeClient) do |client, client_thread|
					Timeout.timeout(10) do
						client.handshakes.pop
					end
				end
			end
			
			expect(output).to be =~ /ngtcp2: /
		end
		
		it "does not print the ngtcp2 log when debug is disabled" do
			output = capture_stderr do
				with_connection(Protocol::QUIC::HandshakeClient) do |client, client_thread|
					Timeout.timeout(10) do
						client.handshakes.pop
					end
				end
			end
			
			expect(output).not.to be =~ /ngtcp2: /
		end
	end
end
