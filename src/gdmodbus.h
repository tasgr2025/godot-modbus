#ifndef GDMODBUS_H
#define GDMODBUS_H

#include <atomic>
#include <mutex>
#include <modbus.h>
#include <thread>
#include <condition_variable>

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/templates/list.h"
#include "core/variant/array.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"
#include "core/os/thread.h"
#include "core/os/mutex.h"
#include "core/os/os.h"

/** ... */
bool get_key_val(const Dictionary &d, const List<Variant> &keys, const int &i, const int &nb, int &key, int &val);

class ModbusRtu: public Object
{
    GDCLASS(ModbusRtu, Object);
public:
    Error open(
    const String &port      = "/dev/ttyS0",
    int           server_id = 0,
    int           baud      = 9600,
    const String &parity    = "N",
    int           bits      = 8,
    int           stop_bits = 1);
    /** Завершает соединения. После вызова все дескрипторы становятся недостоверными. */
    void close();
    String get_libmodbus_version() const { return String(LIBMODBUS_VERSION_STRING); }
    ~ModbusRtu() { close(); }
    Error flush();
    Error set_debug(bool is_debug);
    Error report_slave_id(Array resp);
    bool get_debug();
    float get_indication_timeout();
    Error set_indication_timeout(float timeout);
    Error set_byte_timeout(float timeout);
    Error set_response_timeout(float timeout);
    float get_response_timeout();
    float get_byte_timeout();
    Error set_error_recovery(bool val);
    Error set_slave(int slave_id);
    int get_slave();
    Error set_socket(int s);
    int get_socket();
    bool get_error_recovery();
    bool is_open() const {return ctx != nullptr;}
protected:
    static void _bind_methods();
    String _to_string() const {return String("<ModbusRtu: {_}>").format(this);}
    modbus_t *ctx = nullptr;
};


enum TaskCode
{
    none,
    read_input,
    read,
    read_bits,
    read_input_bits,
    write,
    write_bits,
    interrupt
};


class TaskItem
{
public:
    TaskItem() : code{TaskCode::none}, addr{0}, count{0} {};
    TaskCode code;
    int addr;
    int count;
    Array data;
};


class ModbusClientRtu : public ModbusRtu
{
    GDCLASS(ModbusClientRtu, ModbusRtu);
public:
    Error read(int base_addr, int count, Array resp);
    Error read_bits(int base_addr, int count, Array resp);
    Error read_input(int base_addr, int count, Array resp);
    Error read_input_bits(int base_addr, int count, Array resp);
    Error write(int base_addr, const Array &resp);
    Error write_bits(int base_addr, const Array &resp);
    void request_read(int base_addr, int count);
    void request_read_bits(int base_addr, int count);
    void request_read_input(int base_addr, int count);
    void request_read_input_bits(int base_addr, int count);
    void request_write(int base_addr, const Array &resp);
    void request_write_bits(int base_addr, const Array &resp);
protected:
    static void thread_proc(void *arg);
    Error thread_run();
    Error thread_stop();
    static void _bind_methods();
    String _to_string() const {return String("<ModbusClientRtu: {_}>").format(this);}
    std::mutex mutex;
    Thread thread;
    std::mutex cv_mutex;
    std::condition_variable cv;
    bool resume_thread {false};
    std::atomic<bool> run_thread {true};
private:
    List<TaskItem> queue;
    /** Добавляет запрос на запись в порты
     * task - код задания
     * base_addr - базовый адрес портов
     * resp - содержит данные для записи в порты */
    void push_request(TaskCode task, int base_addr, const Array &resp);
    /** Добавляет запрос на чтение из портов
     * task - код задания
     * base_addr - базовый адрес портов
     * count - количество портов для чтения */
    void push_request(TaskCode task, int base_addr, int count);
};


class ModbusServerRtu : public ModbusRtu
{
    GDCLASS(ModbusServerRtu, ModbusRtu);
public:
    ModbusServerRtu();
   ~ModbusServerRtu();
    /** Процедура для обслуживания запросов. Возвращает:
     *     Error::ERR_BUSY - требуется продолжить приём пакетов.
     *     Error::FAILED - при обработке запроса возникла ошибка.
     *     Error::OK - входящий запрос обработан. */
    Error process();
    Error thread_run();
    Error thread_stop();
    bool is_open() const {return ctx != nullptr && mb_mapping != nullptr;}
    /** Возвращает текущие значения битов */
    Dictionary get_bits() const;
    /** Возвращает текущие значения входных битов */
    Dictionary get_input_bits() const;
    /** Возвращает текущие значения регистров */
    Dictionary get_registers() const;
    /** Возвращает текущие значения входных регистров */
    Dictionary get_input_registers() const;
    /** Устанавливает текущие значения битов */
    Error set_bits(const Variant &dic);
    /** Устанавливает текущие значения входных битов */
    Error set_input_bits(const Variant &dic);
    /** Устанавливает текущие значения регистров */
    Error set_registers(const Variant &dic);
    /** Устанавливает текущие значения входных регистров */
    Error set_input_registers(const Variant &dic);
    /** Устанавливает параметры хранилища данных. dic - должен быть типа
    * Dictionary и содержать любой набор элементов со следующими ключами:
    * [b]"nb_bits"[/b]               - количество битов;
    * [b]"start_bits"[/b]            - начальный адрес битов;
    * [b]"nb_input_bits"[/b]         - количество входных битов;
    * [b]"start_input_bits"[/b]      - начальный адрес входных битов;
    * [b]"nb_input_registers"[/b]    - количество регистров;
    * [b]"start_input_registers"[/b] - начальный адрес регистров;
    * [b]"nb_registers"[/b]          - количество входных регистров;
    * [b]"start_registers"[/b]       - начальный адрес входных регистров.
    * Для не указазанных ключей, значения останутся не изменными. Тип ключа - [b]String[/b].
    * Тип значения целое без знака. Значения с ключами не указанными здесь игнорируются. */
    Error set_mapping(const Variant &dic);
    /** Возвращает параметры хранилища данных. Смотри [set_mapping] */
    Dictionary get_mapping();
    /** Задаёт время задержки в мкс добавляемой после завершения обработки запроса от клиента.
     * Влияет только на цикл внутри потока запускаемого возовом [method thread_run].*/
    void set_delay(uint32_t val) { udelay = val; };
    uint32_t get_delay() { return static_cast<uint32_t>(udelay); };
protected:
    static void _bind_methods();
    static void thread_proc(void* arg);
    modbus_mapping_t *mb_mapping = nullptr;
    uint8_t *query = nullptr;
    String _to_string() const {return String("<ModbusServerRtu: {_}>").format(this);}
    Thread thread;
    Mutex mutex;
    std::atomic<bool> run_thread {true};
    std::atomic<uint32_t> udelay {1000U};
};

#endif // GDMODBUS_H
