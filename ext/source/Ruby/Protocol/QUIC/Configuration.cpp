
#include <ruby.h>

#include "Configuration.hpp"

#include <stdarg.h>
#include <stdio.h>

#include <ngtcp2/ngtcp2.h>

VALUE Ruby_Protocol_QUIC_Configuration = Qnil;

static void Ruby_Protocol_QUIC_Configuration_free(void *data)
{
	if (data) {
		delete reinterpret_cast<Ruby::Protocol::QUIC::Configuration *>(data);
	}
}

static size_t Ruby_Protocol_QUIC_Configuration_size(const void *data) {
	return sizeof(Ruby::Protocol::QUIC::Configuration);
}

static const rb_data_type_t Ruby_Protocol_QUIC_Configuration_type = {
	.wrap_struct_name = "Protocol::QUIC::Configuration",
	.function = {
		.dmark = NULL,
		.dfree = Ruby_Protocol_QUIC_Configuration_free,
		.dsize = Ruby_Protocol_QUIC_Configuration_size,
	},
	.data = NULL,
	.flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE Ruby_Protocol_QUIC_Configuration_allocate(VALUE klass) {
	auto configuration = new Ruby::Protocol::QUIC::Configuration();

	return TypedData_Wrap_Struct(klass, &Ruby_Protocol_QUIC_Configuration_type, configuration);
}

::Protocol::QUIC::Configuration * Ruby_Protocol_QUIC_Configuration_get(VALUE self)
{
	return Ruby_Protocol_QUIC_Configuration_get_ruby(self);
}

Ruby::Protocol::QUIC::Configuration * Ruby_Protocol_QUIC_Configuration_get_ruby(VALUE self)
{
	Ruby::Protocol::QUIC::Configuration *configuration;

	TypedData_Get_Struct(self, Ruby::Protocol::QUIC::Configuration, &Ruby_Protocol_QUIC_Configuration_type, configuration);

	return configuration;
}

static VALUE Ruby_Protocol_QUIC_Configuration_initialize(VALUE self) {
	::Protocol::QUIC::Configuration *configuration;

	TypedData_Get_Struct(self, ::Protocol::QUIC::Configuration, &Ruby_Protocol_QUIC_Configuration_type, configuration);

	return self;
}

static VALUE Ruby_Protocol_QUIC_Configuration_static_secret(VALUE self) {
	::Protocol::QUIC::Configuration *configuration;

	TypedData_Get_Struct(self, ::Protocol::QUIC::Configuration, &Ruby_Protocol_QUIC_Configuration_type, configuration);
	
	return rb_str_new((const char *)configuration->static_secret.data(), configuration->static_secret.size());
}

// @parameter seconds [Numeric] The idle timeout in seconds. Zero disables the idle timeout.
static VALUE Ruby_Protocol_QUIC_Configuration_set_max_idle_timeout(VALUE self, VALUE seconds) {
	auto configuration = Ruby_Protocol_QUIC_Configuration_get_ruby(self);
	double value = NUM2DBL(seconds);

	if (value < 0) rb_raise(rb_eArgError, "max_idle_timeout must not be negative");

	configuration->max_idle_timeout = static_cast<ngtcp2_duration>(value * NGTCP2_SECONDS);

	return seconds;
}

static VALUE Ruby_Protocol_QUIC_Configuration_set_initial_max_streams_bidi(VALUE self, VALUE count) {
	Ruby_Protocol_QUIC_Configuration_get_ruby(self)->initial_max_streams_bidi = NUM2ULL(count);

	return count;
}

static VALUE Ruby_Protocol_QUIC_Configuration_set_initial_max_streams_uni(VALUE self, VALUE count) {
	Ruby_Protocol_QUIC_Configuration_get_ruby(self)->initial_max_streams_uni = NUM2ULL(count);

	return count;
}

// Print ngtcp2's debug log to stderr for new connections.
static VALUE Ruby_Protocol_QUIC_Configuration_set_debug(VALUE self, VALUE value) {
	Ruby_Protocol_QUIC_Configuration_get_ruby(self)->debug = RTEST(value);

	return value;
}

void Init_Ruby_Protocol_QUIC_Configuration(VALUE Protocol_QUIC) {
	Ruby_Protocol_QUIC_Configuration =
			rb_define_class_under(Protocol_QUIC, "Configuration", rb_cObject);

	rb_define_alloc_func(Ruby_Protocol_QUIC_Configuration, Ruby_Protocol_QUIC_Configuration_allocate);
	rb_define_method(Ruby_Protocol_QUIC_Configuration, "initialize", Ruby_Protocol_QUIC_Configuration_initialize, 0);
	
	rb_define_method(Ruby_Protocol_QUIC_Configuration, "static_secret", Ruby_Protocol_QUIC_Configuration_static_secret, 0);
	
	rb_define_method(Ruby_Protocol_QUIC_Configuration, "max_idle_timeout=", Ruby_Protocol_QUIC_Configuration_set_max_idle_timeout, 1);
	rb_define_method(Ruby_Protocol_QUIC_Configuration, "initial_max_streams_bidi=", Ruby_Protocol_QUIC_Configuration_set_initial_max_streams_bidi, 1);
	rb_define_method(Ruby_Protocol_QUIC_Configuration, "debug=", Ruby_Protocol_QUIC_Configuration_set_debug, 1);
	rb_define_method(Ruby_Protocol_QUIC_Configuration, "initial_max_streams_uni=", Ruby_Protocol_QUIC_Configuration_set_initial_max_streams_uni, 1);
}
