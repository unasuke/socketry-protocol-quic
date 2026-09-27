# frozen_string_literal: true

require "protocol/quic"

module Protocol::QUIC
	# A server-side stream that writes back whatever it receives.
	class EchoStream < Stream
		def received(data, fin)
			write(data)
			finish if fin
		end
	end
end
