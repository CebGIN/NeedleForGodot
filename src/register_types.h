#ifndef NEEDLE_FOR_GODOT_REGISTER_TYPES_H
#define NEEDLE_FOR_GODOT_REGISTER_TYPES_H

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void initialize_needle_for_godot(ModuleInitializationLevel p_level);
void uninitialize_needle_for_godot(ModuleInitializationLevel p_level);

#endif // NEEDLE_FOR_GODOT_REGISTER_TYPES_H
