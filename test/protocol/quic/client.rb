# frozen_string_literal: true

require "protocol/quic/address"
require "protocol/quic/client_context"

describe Protocol::QUIC::Client do
	let(:configuration) {Protocol::QUIC::Configuration.new}
	let(:client_context) {Protocol::QUIC::Fixtures.client_context}
	let(:address) {Protocol::QUIC::Fixtures.address}

	def bound_socket(address)
		socket = Protocol::QUIC::Socket.new(address.family, ::Socket::SOCK_DGRAM, ::Socket::IPPROTO_UDP)
		socket.bind(address)
		socket
	end

	# Build a client without driving the connection, so the assertions do not race the `connect` loop.
	let(:client) do
		peer = bound_socket(address)
		socket = Protocol::QUIC::Socket.new(address.family, ::Socket::SOCK_DGRAM, ::Socket::IPPROTO_UDP)
		socket.connect(peer.local_address)
		subject.new(configuration, client_context, socket, peer.local_address, 1)
	end

	with "#remote_close_error" do
		it "is nil before the peer sends CONNECTION_CLOSE" do
			expect(client.remote_close_error).to be_nil
		end
	end
end
