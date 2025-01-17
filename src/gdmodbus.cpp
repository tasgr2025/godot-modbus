#include "gdmodbus.h"


static const char* sn_emit_signal     = "emit_signal";
static const char* sn_read            = "read";
static const char* sn_read_bits       = "read_bits";
static const char* sn_read_input      = "read_input";
static const char* sn_read_input_bits = "read_input_bits";
static const char* sn_write           = "write";
static const char* sn_write_bits      = "write_bits";
static const char* sn_queue_empty     = "queue_empty";
static const char* sn_thread_run      = "thread_run";
static const char* sn_thread_stop     = "thread_stop";
static const char* sn_receive         = "receive";
static const char* sn_reply           = "reply";
static const char* sn_receive_error   = "receive_error";


Error ModbusRtu::open(
		const String &port,
		int slave,
		int baud,
		const String &parity,
		int bits,
		int stop_bits) {
	if (ctx) {
        return Error::FAILED;
    }
    ctx = modbus_new_rtu(
        port.ascii().get_data(),
        baud,
        parity.ascii().get_data()[0],
        bits,
        stop_bits);
    if (!ctx) {
        return Error::FAILED;
    }
    if (modbus_set_slave(ctx, slave) == -1) {
        close();
        return Error::FAILED;
    }
    if (modbus_connect(ctx) == -1) {
        close();
        return Error::FAILED;
    }
    return Error::OK;
}


void ModbusRtu::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open"),                   &ModbusRtu::open);
    ClassDB::bind_method(D_METHOD("close"),                  &ModbusRtu::close);
    ClassDB::bind_method(D_METHOD("is_open"),                &ModbusRtu::is_open);
    ClassDB::bind_method(D_METHOD("flush"),                  &ModbusRtu::flush);
    ClassDB::bind_method(D_METHOD("set_debug"),              &ModbusRtu::set_debug);
    ClassDB::bind_method(D_METHOD("set_socket"),             &ModbusRtu::set_socket);
    ClassDB::bind_method(D_METHOD("get_socket"),             &ModbusRtu::get_socket);
    ClassDB::bind_method(D_METHOD("report_slave_id"),        &ModbusRtu::report_slave_id);
    ClassDB::bind_method(D_METHOD("get_indication_timeout"), &ModbusRtu::get_indication_timeout);
    ClassDB::bind_method(D_METHOD("set_indication_timeout"), &ModbusRtu::set_indication_timeout);
    ClassDB::bind_method(D_METHOD("get_response_timeout"),   &ModbusRtu::get_response_timeout);
    ClassDB::bind_method(D_METHOD("set_response_timeout"),   &ModbusRtu::set_response_timeout);
    ClassDB::bind_method(D_METHOD("get_byte_timeout"),       &ModbusRtu::get_byte_timeout);
    ClassDB::bind_method(D_METHOD("set_byte_timeout"),       &ModbusRtu::set_byte_timeout);
    ClassDB::bind_method(D_METHOD("get_error_recovery"),     &ModbusRtu::get_error_recovery);
}


void ModbusRtu::close() {
    modbus_close(ctx);
    modbus_free(ctx);
    ctx = NULL;
}


void ModbusClientRtu::thread_proc(void *arg) {
    ModbusClientRtu* self = static_cast<ModbusClientRtu*>(arg);
    self->call_deferred(sn_emit_signal, sn_thread_run);
    while (self->run_thread) {
        self->mutex.lock();
        size_t sz = self->queue.size();
        self->mutex.unlock();
        if (sz == 0) {
            self->call_deferred(sn_emit_signal, sn_queue_empty);
            {    std::unique_lock lk(self->cv_mutex);
                 self->cv.wait(lk, [self]{ return self->resume_thread; });
                 self->resume_thread = false; }
            self->cv.notify_one();
            continue;
        }
        self->mutex.lock();
        TaskItem item (self->queue.get(0));
        self->queue.pop_front();
        switch(item.code) {
        case TaskCode::write: {
            Error rc = self->write(item.addr, item.data);
            self->call_deferred(sn_emit_signal, sn_write, rc, item.addr, item.data);
            break; }
        case TaskCode::write_bits: {
            Error rc = self->write_bits(item.addr, item.data);
            self->call_deferred(sn_emit_signal, sn_write_bits, rc, item.addr, item.data);
            break; }
        case TaskCode::read: {
            Error rc = self->read(item.addr, item.count, item.data);
            self->call_deferred(sn_emit_signal, sn_read, rc, item.addr, item.data);
            break; }
        case TaskCode::read_bits: {
            Error rc = self->read_bits(item.addr, item.count, item.data);
            self->call_deferred(sn_emit_signal, sn_read_bits, rc, item.addr, item.data);
            break; }
        case TaskCode::read_input: {
            Error rc = self->read_input(item.addr, item.count, item.data);
            self->call_deferred(sn_emit_signal, sn_read_input, rc, item.addr, item.data);
            break; }
        case TaskCode::read_input_bits: {
            Error rc = self->read_input_bits(item.addr, item.count, item.data);
            self->call_deferred(sn_emit_signal, sn_read_input_bits, rc, item.addr, item.data);
            break; }
        }
        self->mutex.unlock();
    }
    self->call_deferred(sn_emit_signal, sn_thread_stop);
}


Error ModbusClientRtu::thread_run() {
    if (thread.is_started()) {
        return Error::FAILED;
    }
    run_thread = true;
    resume_thread = false;
    thread.start(thread_proc, this);
    return Error::OK;
}


Error ModbusClientRtu::thread_stop() {
    if (!thread.is_started()) {
        return Error::FAILED;
    }
    run_thread = false;
    {   std::lock_guard lk(cv_mutex);
        resume_thread = true; }
    cv.notify_one();
    thread.wait_to_finish();
    return Error::OK;
}


void ModbusClientRtu::request_read(int base_addr, int count) {
    push_request(TaskCode::read, base_addr, count);
}


void ModbusClientRtu::request_read_bits(int base_addr, int count) {
    push_request(TaskCode::read_bits, base_addr, count);
}


void ModbusClientRtu::request_read_input(int base_addr, int count) {
    push_request(TaskCode::read_input, base_addr, count);
}


void ModbusClientRtu::request_read_input_bits(int base_addr, int count){
    push_request(TaskCode::read_input_bits, base_addr, count);
}


void ModbusClientRtu::request_write(int base_addr, const Array &resp) {
    push_request(TaskCode::write, base_addr, resp);
}


void ModbusClientRtu::request_write_bits(int base_addr, const Array &resp) {
    push_request(TaskCode::write_bits, base_addr, resp);
}


void ModbusClientRtu::push_request(TaskCode task_code, int base_addr, int count) {
    TaskItem item;
    item.code = task_code;
    item.addr = base_addr;
    item.count = count;
    mutex.lock();
    queue.push_back(item);
    mutex.unlock();
    {   std::lock_guard lk(cv_mutex);
        resume_thread = true; }
    cv.notify_one();
}


void ModbusClientRtu::push_request(TaskCode task_code, int base_addr, const Array &resp) {
    TaskItem item;
    item.code = task_code;
    item.addr = base_addr;
    item.count = resp.size();
    for (size_t i = 0; i < item.count; i ++) {
        Variant val = resp[i];
        item.data.append(val);
    }
    mutex.lock();
    queue.push_back(item);
    mutex.unlock();
    {   std::lock_guard lk(cv_mutex);
        resume_thread = true; }
    cv.notify_one();
}


Error ModbusClientRtu::read(int base_addr, int count, Array resp) {
    resp.clear();
    if ((base_addr < 0) || (count < 0)) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Vector<uint16_t> resp_data;
    resp_data.resize(count);
    int rc = modbus_read_registers(ctx, base_addr, count, resp_data.ptrw());
    if (rc == -1) {
        return Error::FAILED;
    }
    for(size_t i = 0; i < count; i++) {
        resp.append(static_cast<int>(resp_data[i]));
    }
    return Error::OK;
}


Error ModbusClientRtu::read_input(int base_addr, int count, Array resp) {
    resp.clear();
    if ((base_addr < 0) || (count < 0)) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Vector<uint16_t> resp_data;
    resp_data.resize(count);
    int rc = modbus_read_input_registers(ctx, base_addr, count, resp_data.ptrw());
    if (rc == -1) {
        return Error::FAILED;
    }
    for(size_t i = 0; i < count; i++) {
        resp.append(static_cast<int>(resp_data[i]));
    }
    return Error::OK;
}


Error ModbusClientRtu::read_bits(int base_addr, int count, Array resp) {
    resp.clear();
    if ((base_addr < 0) || (count < 0)) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Vector<uint8_t> resp_data;
    resp_data.resize(count);
    int rc = modbus_read_bits(ctx, base_addr, count, resp_data.ptrw());
    if (rc == -1) {
        return Error::FAILED;
    }
    for(size_t i = 0; i < count; i++) {
        resp.append(static_cast<bool>(resp_data[i]));
    }
    return Error::OK;
}


Error ModbusClientRtu::read_input_bits(int base_addr, int count, Array resp) {
    resp.clear();
    if ((base_addr < 0) || (count < 0)) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Vector<uint8_t> resp_data;
    resp_data.resize(count);
    int rc = modbus_read_input_bits(ctx, base_addr, count, resp_data.ptrw());
    if (rc == -1) {
        return Error::FAILED;
    }
    for(size_t i = 0; i < count; i++) {
        resp.append(static_cast<bool>(resp_data[i]));
    }
    return Error::OK;
}


Error ModbusClientRtu::write(int base_addr, const Array &resp) {
    if (base_addr < 0) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Vector<uint16_t> resp_data;
    resp_data.resize(resp.size());
    for(size_t i = 0; i < resp.size(); i++) {
        if (resp[i].get_type() != Variant::Type::INT) {
            return Error::ERR_INVALID_PARAMETER;
        }
        resp_data.ptrw()[i] = static_cast<uint16_t>(resp[i]);
    }
    int rc = modbus_write_registers(ctx, base_addr, resp.size(), resp_data.ptrw());
    return (rc == -1) ? Error::FAILED : Error::OK;
}


Error ModbusClientRtu::write_bits(int base_addr, const Array &resp) {
    if (base_addr < 0) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Vector<uint8_t> resp_data;
    resp_data.resize(resp.size());
    for(size_t i = 0; i < resp.size(); i++) {
        if (resp[i].get_type() != Variant::Type::BOOL) {
            return Error::ERR_INVALID_PARAMETER;
        }
        resp_data.ptrw()[i] = static_cast<uint8_t>(resp[i]);
    }
    int rc = modbus_write_bits(ctx, base_addr, resp.size(), resp_data.ptrw());
    return (rc == -1) ? Error::FAILED : Error::OK;
}


Error ModbusRtu::flush() {
    if (modbus_flush(ctx) == -1) {
        return Error::FAILED;
    }
	return Error::OK;
}


Error ModbusRtu::set_debug(bool is_debug) {
    if (modbus_set_debug(ctx, is_debug ? 1 : 0) == -1) {
        return Error::FAILED;
    }
	return Error::OK;
}


bool ModbusRtu::get_debug() {
   int rc = modbus_get_debug(ctx);
   if (rc == -1) {
       return false;
   }
   return rc > 0;
}


float ModbusRtu::get_indication_timeout() {
    uint32_t tv_sec  = 0U;
    uint32_t tv_usec = 0U;
    if (modbus_get_indication_timeout(ctx, &tv_sec, &tv_usec) == -1) {
        return 0.0f;
    }
    return static_cast<float>(tv_sec * 1000000U + tv_usec);
}


Error ModbusRtu::set_indication_timeout(float timeout) {
    if (timeout < 0.0f) {
        return Error::ERR_INVALID_PARAMETER;
    }
    uint32_t tv_sec  = static_cast<uint32_t>(roundf(timeout / 1000000.0f));
    uint32_t tv_usec = static_cast<uint32_t>(timeout) % 1000000U;
    if (modbus_set_indication_timeout(ctx, tv_sec, tv_usec) == -1) {
        return Error::FAILED;
    }
    return Error::OK;
}


float ModbusRtu::get_response_timeout() {
    uint32_t tv_sec  = 0U;
    uint32_t tv_usec = 0U;
    if (modbus_get_response_timeout(ctx, &tv_sec, &tv_usec) == -1) {
        return 0.0f;
    }
    return static_cast<float>(tv_sec * 1000000U + tv_usec);
}


Error ModbusRtu::set_response_timeout(float timeout) {
    if (timeout < 0.0f) {
        return Error::ERR_INVALID_PARAMETER;
    }
    uint32_t tv_sec  = static_cast<uint32_t>(roundf(timeout / 1000000.0f));
    uint32_t tv_usec = static_cast<uint32_t>(timeout) % 1000000U;
    if (modbus_set_response_timeout(ctx, tv_sec, tv_usec) == -1) {
        return Error::FAILED;
    }
    return Error::OK;
}


Error ModbusRtu::set_byte_timeout(float timeout) {
    if (timeout < 0.0f) {
        return Error::ERR_INVALID_PARAMETER;
    }
    uint32_t tv_sec  = static_cast<uint32_t>(roundf(timeout / 1000000.0f));
    uint32_t tv_usec = static_cast<uint32_t>(timeout) % 1000000U;
    if (modbus_set_byte_timeout(ctx, tv_sec, tv_usec) == -1) {
        return Error::FAILED;
    }
    return Error::OK;
}


float ModbusRtu::get_byte_timeout() {
    uint32_t tv_sec  = 0U;
    uint32_t tv_usec = 0U;
    if (modbus_get_byte_timeout(ctx, &tv_sec, &tv_usec) == -1) {
        return 0.0f;
    }
    return static_cast<float>(tv_sec * 1000000U + tv_usec);
}


Error ModbusRtu::report_slave_id(Array resp) {
    resp.clear();
    Vector<uint8_t> resp_data;
    resp_data.resize(MODBUS_MAX_ADU_LENGTH);
    int rc = modbus_report_slave_id(ctx, resp_data.size(), resp_data.ptrw());
    if (rc == -1) {
        return Error::FAILED;
    }
    resp.resize(MIN(rc, resp_data.size()));
    for(size_t i = 0; i < resp.size(); i++) {
        resp[i] = static_cast<int>(resp_data[i]);
    }
    return Error::OK;
}


Error ModbusRtu::set_error_recovery(bool val) {
   if (modbus_set_error_recovery(ctx,
        val ?
        (modbus_error_recovery_mode) (MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL):
        (modbus_error_recovery_mode) 0) == -1) {
        return Error::FAILED;
    }
    return Error::OK;
}


Error ModbusRtu::set_slave(int slave) {
    if (modbus_set_slave(ctx, slave) == -1) {
        return Error::FAILED;
    }
	return Error::OK;
}


int ModbusRtu::get_slave() {
    return modbus_get_slave(ctx);
}


Error ModbusRtu::set_socket(int s) {
    if (modbus_set_socket(ctx, s) == -1) {
        return Error::FAILED;
    }
    return Error::OK;
}


int ModbusRtu::get_socket() {
    return modbus_get_socket(ctx);
}


bool ModbusRtu::get_error_recovery() {
   int rc = modbus_get_error_recovery(ctx);
   if (rc == -1) {
        return false;
   }
   return rc == MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL;
}


void ModbusClientRtu::_bind_methods() {
    ClassDB::bind_method(D_METHOD("thread_run"),  &ModbusClientRtu::thread_run);
    ClassDB::bind_method(D_METHOD("thread_stop"), &ModbusClientRtu::thread_stop);

    ClassDB::bind_method(D_METHOD("read",            "base_addr", "count", "resp"), &ModbusClientRtu::read);
    ClassDB::bind_method(D_METHOD("read_bits",       "base_addr", "count", "resp"), &ModbusClientRtu::read_bits);
    ClassDB::bind_method(D_METHOD("read_input",      "base_addr", "count", "resp"), &ModbusClientRtu::read_input);
    ClassDB::bind_method(D_METHOD("read_input_bits", "base_addr", "count", "resp"), &ModbusClientRtu::read_input_bits);

    ClassDB::bind_method(D_METHOD("write",      "base_addr", "resp"), &ModbusClientRtu::write);
    ClassDB::bind_method(D_METHOD("write_bits", "base_addr", "resp"), &ModbusClientRtu::write_bits);

    ClassDB::bind_method(D_METHOD("request_read",            "base_addr", "count"), &ModbusClientRtu::request_read);
    ClassDB::bind_method(D_METHOD("request_read_bits",       "base_addr", "count"), &ModbusClientRtu::request_read_bits);
    ClassDB::bind_method(D_METHOD("request_read_input",      "base_addr", "count"), &ModbusClientRtu::request_read_input);
    ClassDB::bind_method(D_METHOD("request_read_input_bits", "base_addr", "count"), &ModbusClientRtu::request_read_input_bits);

    ClassDB::bind_method(D_METHOD("request_write",      "base_addr", "resp"), &ModbusClientRtu::request_write);
    ClassDB::bind_method(D_METHOD("request_write_bits", "base_addr", "resp"), &ModbusClientRtu::request_write_bits);

    ADD_SIGNAL(MethodInfo(sn_read,            PropertyInfo(Variant::INT, "return_code"), PropertyInfo(Variant::INT, "base_addr"), PropertyInfo(Variant::ARRAY, "data")));
    ADD_SIGNAL(MethodInfo(sn_read_bits,       PropertyInfo(Variant::INT, "return_code"), PropertyInfo(Variant::INT, "base_addr"), PropertyInfo(Variant::ARRAY, "data")));
    ADD_SIGNAL(MethodInfo(sn_read_input,      PropertyInfo(Variant::INT, "return_code"), PropertyInfo(Variant::INT, "base_addr"), PropertyInfo(Variant::ARRAY, "data")));
    ADD_SIGNAL(MethodInfo(sn_read_input_bits, PropertyInfo(Variant::INT, "return_code"), PropertyInfo(Variant::INT, "base_addr"), PropertyInfo(Variant::ARRAY, "data")));
    ADD_SIGNAL(MethodInfo(sn_write,           PropertyInfo(Variant::INT, "return_code"), PropertyInfo(Variant::INT, "base_addr"), PropertyInfo(Variant::ARRAY, "data")));
    ADD_SIGNAL(MethodInfo(sn_write_bits,      PropertyInfo(Variant::INT, "return_code"), PropertyInfo(Variant::INT, "base_addr"), PropertyInfo(Variant::ARRAY, "data")));
    ADD_SIGNAL(MethodInfo(sn_queue_empty));
    ADD_SIGNAL(MethodInfo(sn_thread_run));
    ADD_SIGNAL(MethodInfo(sn_thread_stop));
}


ModbusServerRtu::ModbusServerRtu() {
    mb_mapping = modbus_mapping_new_start_address(0, 512, 0, 512, 0, 512, 0, 512);
    query = static_cast<uint8_t*>(malloc(MODBUS_RTU_MAX_ADU_LENGTH));
}


ModbusServerRtu::~ModbusServerRtu() {
    if (thread.is_started()) {
        run_thread = false;
        thread.wait_to_finish();
    }
    modbus_mapping_free(mb_mapping);
    free(query);
}


Error ModbusServerRtu::set_mapping(const Variant &dic) {
    if (dic.get_type() != Variant::DICTIONARY) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Dictionary d = dic;
    mutex.lock();
    modbus_mapping_t *mb_mapping_tmp = mb_mapping;
    mb_mapping = modbus_mapping_new_start_address(
        static_cast<int>(d.get("start_bits",            mb_mapping_tmp->start_bits)),
        static_cast<int>(d.get("nb_bits",               mb_mapping_tmp->nb_bits)),
        static_cast<int>(d.get("start_input_bits",      mb_mapping_tmp->start_input_bits)),
        static_cast<int>(d.get("nb_input_bits",         mb_mapping_tmp->nb_input_bits)),
        static_cast<int>(d.get("start_registers",       mb_mapping_tmp->start_registers)),
        static_cast<int>(d.get("nb_registers",          mb_mapping_tmp->nb_registers)),
        static_cast<int>(d.get("start_input_registers", mb_mapping_tmp->start_input_registers)),
        static_cast<int>(d.get("nb_input_registers",    mb_mapping_tmp->nb_input_registers)));
   modbus_mapping_free(mb_mapping_tmp);
   mutex.unlock();
   return Error::OK;
}


Dictionary ModbusServerRtu::get_mapping() {
    Dictionary dic;
    mutex.lock();
    dic["start_bits"]            = mb_mapping->start_bits;
    dic["nb_bits"]               = mb_mapping->nb_bits;
    dic["start_input_bits"]      = mb_mapping->start_input_bits;
    dic["nb_input_bits"]         = mb_mapping->nb_input_bits;
    dic["start_registers"]       = mb_mapping->start_registers;
    dic["nb_registers"]          = mb_mapping->nb_registers;
    dic["start_input_registers"] = mb_mapping->start_input_registers;
    dic["nb_input_registers"]    = mb_mapping->nb_input_registers;
    mutex.unlock();
    return dic;
}


Error ModbusServerRtu::process() {
    int rc = modbus_receive(ctx, query);
    if (rc == 0) {
        return Error::ERR_BUSY;
    }
    else if (rc == -1 && errno != EMBBADCRC) {
        call_deferred(sn_receive_error);
        return Error::FAILED;
    }
    call_deferred(sn_receive);
    int rsp_length = compute_response_length_from_request(ctx, query);
    if (modbus_reply(ctx, query, rsp_length, mb_mapping) == -1) {
        return Error::FAILED;
    }
    call_deferred(sn_reply);
    return Error::OK;
}


Error ModbusServerRtu::thread_run() {
    if (thread.is_started()) {
        return Error::FAILED;
    }
    run_thread = true;
    thread.start(thread_proc, this);
	return Error::OK;
}


Error ModbusServerRtu::thread_stop() {
    if (!thread.is_started()) {
        return Error::FAILED;
    }
    run_thread = false;
    thread.wait_to_finish();
	return Error::OK;
}


void ModbusServerRtu::thread_proc(void *arg) {
    ModbusServerRtu* self = static_cast<ModbusServerRtu*>(arg);
    while(self->run_thread) {
        self->mutex.lock();
        Error rc = self->process();
        self->mutex.unlock();
        if ((rc == Error::FAILED) || (rc == Error::OK)) {
            OS::get_singleton()->delay_usec(self->udelay);
        }
    }
}


Dictionary ModbusServerRtu::get_bits() const {
    Dictionary dic;
    mutex.lock();
    for (int i = mb_mapping->start_bits; i < mb_mapping->nb_bits; i ++) {
        dic[i] = static_cast<bool>(mb_mapping->tab_bits[i]);
    }
    mutex.unlock();
    return dic;
}


Dictionary ModbusServerRtu::get_input_bits() const {
    Dictionary dic;
    mutex.lock();
    for (int i = mb_mapping->start_input_bits; i < mb_mapping->nb_input_bits; i ++) {
        dic[i] = static_cast<bool>(mb_mapping->tab_input_bits[i]);
    }
    mutex.unlock();
    return dic;
}


Dictionary ModbusServerRtu::get_registers() const {
    Dictionary dic;
    mutex.lock();
    for (int i = mb_mapping->start_registers; i < mb_mapping->nb_registers; i ++) {
        dic[i] = static_cast<int>(mb_mapping->tab_registers[i]);
    }
    mutex.unlock();
    return dic;
}


Dictionary ModbusServerRtu::get_input_registers() const {
    Dictionary dic;
    mutex.lock();
    for (int i = mb_mapping->start_input_registers; i < mb_mapping->nb_input_registers; i ++) {
        dic[i] = static_cast<int>(mb_mapping->tab_input_registers[i]);
    }
    mutex.unlock();
    return dic;
}


Error ModbusServerRtu::set_bits(const Variant &dic) {
    if (dic.get_type() != Variant::DICTIONARY) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Dictionary d = dic;
    List<Variant> keys;
    d.get_key_list(&keys);
    Error rc = Error::OK;
    mutex.lock();
    for (int i = 0; i < keys.size(); i ++) {
       int key, val;
       if (!get_key_val(d, keys, i, mb_mapping->nb_bits, key, val)) {
           rc = Error::ERR_INVALID_PARAMETER;
           break;
       }
       mb_mapping->tab_bits[key] = val;
    }
    mutex.unlock();
	return rc;
}


Error ModbusServerRtu::set_input_bits(const Variant &dic) {
    if (dic.get_type() != Variant::DICTIONARY) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Dictionary d = dic;
    List<Variant> keys;
    d.get_key_list(&keys);
    Error rc = Error::OK;
    mutex.lock();
    for (int i = 0; i < keys.size(); i ++) {
       int key, val;
       if (!get_key_val(d, keys, i, mb_mapping->nb_input_bits, key, val)) {
           rc = Error::ERR_INVALID_PARAMETER;
           break;
       }
       mb_mapping->tab_input_bits[key] = val;
    }
    mutex.unlock();
	return rc;
}


Error ModbusServerRtu::set_registers(const Variant &dic) {
    if (dic.get_type() != Variant::DICTIONARY) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Dictionary d = dic;
    List<Variant> keys;
    d.get_key_list(&keys);
    Error rc = Error::OK;
    mutex.lock();
    for (int i = 0; i < keys.size(); i ++) {
       int key, val;
       if (!get_key_val(d, keys, i, mb_mapping->nb_registers, key, val)) {
            rc = Error::ERR_INVALID_PARAMETER;
            break;
       }
       mb_mapping->tab_registers[key] = val;
    }
    mutex.unlock();
	return rc;
}


Error ModbusServerRtu::set_input_registers(const Variant &dic) {
    if (dic.get_type() != Variant::DICTIONARY) {
        return Error::ERR_INVALID_PARAMETER;
    }
    Dictionary d = dic;
    List<Variant> keys;
    d.get_key_list(&keys);
    Error rc = Error::OK;
    mutex.lock();
    for (int i = 0; i < keys.size(); i ++) {
       int key, val;
       if (!get_key_val(d, keys, i, mb_mapping->nb_input_registers, key, val)) {
            rc = Error::ERR_INVALID_PARAMETER;
            break;
       }
       mb_mapping->tab_input_registers[key] = val;
    }
    mutex.unlock();
	return rc;
}


bool get_key_val(const Dictionary &d, const List<Variant> &keys,
    const int &i, const int &nb, int &key, int &val) {
    Variant vkey = keys.get(i);
    if (vkey.get_type() != Variant::INT) {
        return false;
    }
    key = vkey;
    Variant vval = d.get(vkey, 0);
    if (vval.get_type() != Variant::INT) {
        return false;
    }
    val = vval;
    if ((key < 0) || (key >= nb)) {
        return false;
    }
    return true;
}


void ModbusServerRtu::_bind_methods() {
    ClassDB::bind_method(D_METHOD("process"),             &ModbusServerRtu::process);
    ClassDB::bind_method(D_METHOD("set_mapping"),         &ModbusServerRtu::set_mapping);
    ClassDB::bind_method(D_METHOD("get_mapping"),         &ModbusServerRtu::get_mapping);
    ClassDB::bind_method(D_METHOD("get_bits"),            &ModbusServerRtu::get_bits);
    ClassDB::bind_method(D_METHOD("get_input_bits"),      &ModbusServerRtu::get_input_bits);
    ClassDB::bind_method(D_METHOD("get_registers"),       &ModbusServerRtu::get_registers);
    ClassDB::bind_method(D_METHOD("get_input_registers"), &ModbusServerRtu::get_input_registers);
    ClassDB::bind_method(D_METHOD("set_delay"),           &ModbusServerRtu::set_delay);
    ClassDB::bind_method(D_METHOD("get_delay"),           &ModbusServerRtu::get_delay);
    ADD_SIGNAL(MethodInfo(sn_receive_error));
    ADD_SIGNAL(MethodInfo(sn_receive));
    ADD_SIGNAL(MethodInfo(sn_reply));
}
