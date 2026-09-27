# frozen_string_literal: true

require "protocol/quic"

describe Protocol::QUIC::Configuration do
	let(:configuration) {subject.new}

	it "generates a static secret" do
		secret = configuration.static_secret

		expect(secret).to be_a(String)
		expect(secret.bytesize).to be == 32
	end

	with "#max_idle_timeout=" do
		it "accepts a duration in seconds" do
			expect(configuration.max_idle_timeout = 30).to be == 30
		end

		it "accepts zero to disable the idle timeout" do
			expect(configuration.max_idle_timeout = 0).to be == 0
		end

		it "rejects a negative duration" do
			expect{configuration.max_idle_timeout = -1}.to raise_exception(ArgumentError)
		end
	end

	with "#initial_max_streams_bidi=" do
		it "accepts a stream count" do
			expect(configuration.initial_max_streams_bidi = 100).to be == 100
		end
	end

	with "#initial_max_streams_uni=" do
		it "accepts a stream count" do
			expect(configuration.initial_max_streams_uni = 100).to be == 100
		end
	end

	with "#debug=" do
		it "accepts a boolean" do
			expect(configuration.debug = true).to be == true
			expect(configuration.debug = false).to be == false
		end
	end
end
