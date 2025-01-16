#include "register_types.h"
#include "core/object/class_db.h"
#include "src/gdmodbus.h"


void initialize_modbus_module(ModuleInitializationLevel p_level)
{
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
	GDREGISTER_CLASS(ModbusRtu);
    GDREGISTER_CLASS(ModbusClientRtu);
	GDREGISTER_CLASS(ModbusServerRtu);
}


void uninitialize_modbus_module(ModuleInitializationLevel p_level)
{
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}
