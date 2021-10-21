/*************************************************************************/
/*  noise_texture.cpp                                                    */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "noise_texture.h"

#include <godot_cpp/classes/rendering_server.hpp>

NoiseTextureExt::NoiseTextureExt() {
	noise = Ref<OpenSimplexNoiseExt>();

	_queue_update();
}

NoiseTextureExt::~NoiseTextureExt() {
	if (texture.get_id() != 0) {
		RenderingServer::get_singleton()->free_rid(texture);
	}
	noise_thread.wait_to_finish();
}

void NoiseTextureExt::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_width", "width"), &NoiseTextureExt::set_width);
	ClassDB::bind_method(D_METHOD("get_width"), &NoiseTextureExt::get_width);
	ClassDB::bind_method(D_METHOD("set_height", "height"), &NoiseTextureExt::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &NoiseTextureExt::get_height);

	ClassDB::bind_method(D_METHOD("set_noise", "noise"), &NoiseTextureExt::set_noise);
	ClassDB::bind_method(D_METHOD("get_noise"), &NoiseTextureExt::get_noise);

	ClassDB::bind_method(D_METHOD("set_noise_offset", "noise_offset"), &NoiseTextureExt::set_noise_offset);
	ClassDB::bind_method(D_METHOD("get_noise_offset"), &NoiseTextureExt::get_noise_offset);

	ClassDB::bind_method(D_METHOD("set_seamless", "seamless"), &NoiseTextureExt::set_seamless);
	ClassDB::bind_method(D_METHOD("get_seamless"), &NoiseTextureExt::get_seamless);

	ClassDB::bind_method(D_METHOD("set_as_normal_map", "as_normal_map"), &NoiseTextureExt::set_as_normal_map);
	ClassDB::bind_method(D_METHOD("is_normal_map"), &NoiseTextureExt::is_normal_map);

	ClassDB::bind_method(D_METHOD("set_bump_strength", "bump_strength"), &NoiseTextureExt::set_bump_strength);
	ClassDB::bind_method(D_METHOD("get_bump_strength"), &NoiseTextureExt::get_bump_strength);

	ClassDB::bind_method(D_METHOD("_update_texture"), &NoiseTextureExt::_update_texture);
	ClassDB::bind_method(D_METHOD("_generate_texture"), &NoiseTextureExt::_generate_texture);
	ClassDB::bind_method(D_METHOD("_queue_update"), &NoiseTextureExt::_queue_update);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "width", PROPERTY_HINT_RANGE, "1,2048,1,or_greater"), "set_width", "get_width");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "height", PROPERTY_HINT_RANGE, "1,2048,1,or_greater"), "set_height", "get_height");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "seamless"), "set_seamless", "get_seamless");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "as_normal_map"), "set_as_normal_map", "is_normal_map");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bump_strength", PROPERTY_HINT_RANGE, "0,32,0.1,or_greater"), "set_bump_strength", "get_bump_strength");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "noise", PROPERTY_HINT_RESOURCE_TYPE, "OpenSimplexNoiseExt"), "set_noise", "get_noise");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "noise_offset"), "set_noise_offset", "get_noise_offset");
}

void NoiseTextureExt::_validate_property(PropertyInfo &property) const {
	if (property.name == "bump_strength") {
		if (!as_normal_map) {
			property.usage = PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL;
		}
	}
}

void NoiseTextureExt::_set_texture_image(const Ref<Image> &p_image) {
	image = p_image;
	if (image.is_valid()) {
		if (texture.get_id() != 0) {
			RID new_texture = RenderingServer::get_singleton()->texture_2d_create(p_image);
			RenderingServer::get_singleton()->texture_replace(texture, new_texture);
		} else {
			texture = RenderingServer::get_singleton()->texture_2d_create(p_image);
		}
	}
	emit_changed();
}

void NoiseTextureExt::_thread_done(const Ref<Image> &p_image) {
	_set_texture_image(p_image);
	noise_thread.wait_to_finish();
	if (regen_queued) {
		//noise_thread.start(_thread_function, this);
		regen_queued = false;
	}
}

void NoiseTextureExt::_thread_function(void *p_ud) {
	NoiseTextureExt *tex = (NoiseTextureExt *)p_ud;
	//tex->call_deferred("_thread_done", tex->_generate_texture());
}

void NoiseTextureExt::_queue_update() {
	if (update_queued) {
		return;
	}

	update_queued = true;
	call_deferred("_update_texture");
}

Ref<Image> NoiseTextureExt::_generate_texture() {
	// Prevent memdelete due to unref() on other thread.
	Ref<OpenSimplexNoiseExt> ref_noise = noise;

	if (ref_noise.is_null()) {
		return Ref<Image>();
	}

	Ref<Image> image;

	if (seamless) {
		image = ref_noise->get_seamless_image(size.x);
	} else {
		image = ref_noise->get_image(size.x, size.y, noise_offset);
	}

	if (as_normal_map) {
		image->bump_map_to_normal_map(bump_strength);
	}

	return image;
}

void NoiseTextureExt::_update_texture() {
	bool use_thread = true;
	if (first_time) {
		use_thread = false;
		first_time = false;
	}
	use_thread = false;
    Ref<Image> image = _generate_texture();
	_set_texture_image(image);
	update_queued = false;
}

void NoiseTextureExt::set_noise(Ref<OpenSimplexNoiseExt> p_noise) {
	if (p_noise == noise) {
		return;
	}
	if (noise.is_valid()) {
		noise->disconnect("changed", Callable(this, "_queue_update"));
	}
	noise = p_noise;
	if (noise.is_valid()) {
		noise->connect("changed", Callable(this, "_queue_update"));
	}
	_queue_update();
}

Ref<OpenSimplexNoiseExt> NoiseTextureExt::get_noise() {
	return noise;
}

void NoiseTextureExt::set_width(int p_width) {
	ERR_FAIL_COND(p_width <= 0);
	if (p_width == size.x) {
		return;
	}
	size.x = p_width;
	_queue_update();
}

void NoiseTextureExt::set_height(int p_height) {
	ERR_FAIL_COND(p_height <= 0);
	if (p_height == size.y) {
		return;
	}
	size.y = p_height;
	_queue_update();
}

void NoiseTextureExt::set_noise_offset(Vector2 p_noise_offset) {
	if (noise_offset == p_noise_offset) {
		return;
	}
	noise_offset = p_noise_offset;
	_queue_update();
}

void NoiseTextureExt::set_seamless(bool p_seamless) {
	if (p_seamless == seamless) {
		return;
	}
	seamless = p_seamless;
	_queue_update();
}

bool NoiseTextureExt::get_seamless() {
	return seamless;
}

void NoiseTextureExt::set_as_normal_map(bool p_as_normal_map) {
	if (p_as_normal_map == as_normal_map) {
		return;
	}
	as_normal_map = p_as_normal_map;
	_queue_update();
	notify_property_list_changed();
}

bool NoiseTextureExt::is_normal_map() {
	return as_normal_map;
}

void NoiseTextureExt::set_bump_strength(float p_bump_strength) {
	if (p_bump_strength == bump_strength) {
		return;
	}
	bump_strength = p_bump_strength;
	if (as_normal_map) {
		_queue_update();
	}
}

float NoiseTextureExt::get_bump_strength() {
	return bump_strength;
}

int NoiseTextureExt::get_width() const {
	return size.x;
}

int NoiseTextureExt::get_height() const {
	return size.y;
}

Vector2 NoiseTextureExt::get_noise_offset() const {
	return noise_offset;
}

RID NoiseTextureExt::get_rid() const {
	if (!texture.get_id() != 0) {
		texture = RenderingServer::get_singleton()->texture_2d_placeholder_create();
	}

	return texture;
}

Ref<Image> NoiseTextureExt::get_image() const {
	return image;
}
