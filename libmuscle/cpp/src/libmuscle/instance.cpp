#include <libmuscle/instance.hpp>

#include <libmuscle/mmp_client.hpp>
#include <ymmsl/compute_element.hpp>
#include <ymmsl/identity.hpp>
#include <ymmsl/settings.hpp>

#include <stdexcept>


using ymmsl::Operator;
using ymmsl::Reference;
using ymmsl::Settings;


namespace libmuscle {

Instance::Instance(int argc, char const * const argv[])
    : instance_name_(make_full_name_(argc, argv))
    , manager_(extract_manager_location_(argc, argv))
    , communicator_(name_(), index_(), {}, 0)
    , declared_ports_()
    , settings_manager_()
    , first_run_(true)
    , f_init_cache_()
    , is_shut_down_(false)
{
    register_();
    connect_();
}

Instance::Instance(int argc, char const * const argv[],
                   PortsDescription const & ports)
    : instance_name_(make_full_name_(argc, argv))
    , manager_(extract_manager_location_(argc, argv))
    , communicator_(name_(), index_(), {}, 0)
    , declared_ports_(ports)
    , settings_manager_()
    , first_run_(true)
    , f_init_cache_()
    , is_shut_down_(false)
{
    register_();
    connect_();
}

bool Instance::reuse_instance(bool apply_overlay) {
    bool do_reuse = receive_settings_();

    // TODO: f_init_cache_ should be empty here, or the user didn't receive
    // something that was sent on the last go-around. At least emit a warning.
    pre_receive_f_init_(apply_overlay);

    auto ports = communicator_.list_ports();

    bool f_init_not_connected = true;
    if (ports.count(Operator::F_INIT) != 0)
        for (auto const & port : ports.at(Operator::F_INIT))
            if (communicator_.get_port(port).is_connected()) {
                f_init_not_connected = false;
                break;
            }

    bool no_settings_in = !communicator_.settings_in_connected();

    if (f_init_not_connected && no_settings_in) {
        do_reuse = first_run_;
        first_run_ = false;
    }
    else {
        for (auto const & ref_msg : f_init_cache_)
            if (is_close_port(ref_msg.second.data()))
                    do_reuse = false;
    }

    return do_reuse;
}

void Instance::exit_error(std::string const & message) {
    shutdown_();
    exit(1);
}

::ymmsl::SettingValue Instance::get_setting_value(std::string const & name) const {
    return settings_manager_.get_setting(instance_name_, name);
}

std::unordered_map<::ymmsl::Operator, std::vector<std::string>>
Instance::list_ports() const {
    return communicator_.list_ports();
}

bool Instance::is_connected(std::string const & port) const {
    return communicator_.get_port(port).is_connected();
}

bool Instance::is_vector_port(std::string const & port) const {
    return communicator_.get_port(port).is_vector();
}

bool Instance::is_resizable(std::string const & port) const {
    return communicator_.get_port(port).is_resizable();
}

int Instance::get_port_length(std::string const & port) const {
    return communicator_.get_port(port).get_length();
}

void Instance::set_port_length(std::string const & port, int length) {
    return communicator_.get_port(port).set_length(length);
}

void Instance::send(std::string const & port_name, Message const & message) {
    check_port_(port_name);
    if (!message.has_settings()) {
        Message msg(message);
        msg.set_settings(settings_manager_.overlay);
        communicator_.send_message(port_name, msg);
    }
    else
        communicator_.send_message(port_name, message);
}

void Instance::send(
        std::string const & port_name, Message const & message, int slot)
{
    check_port_(port_name);
    if (!message.has_settings()) {
        Message msg(message);
        msg.set_settings(settings_manager_.overlay);
        communicator_.send_message(port_name, msg, slot);
    }
    else
        communicator_.send_message(port_name, message, slot);
}

Message Instance::receive(std::string const & port_name) {
    return receive_message_(port_name, {}, {}, false);
}

Message Instance::receive(
        std::string const & port_name,
        int slot)
{
    return receive_message_(port_name, slot, {}, false);
}

Message Instance::receive(
        std::string const & port_name, Message const & default_msg)
{
    return receive_message_(port_name, {}, default_msg, false);
}

Message Instance::receive(
        std::string const & port_name,
        int slot,
        Message const & default_msg)
{
    return receive_message_(port_name, slot, default_msg, false);
}

Message Instance::receive_with_settings(std::string const & port_name) {
    return receive_message_(port_name, {}, {}, true);
}

Message Instance::receive_with_settings(
        std::string const & port_name,
        int slot)
{
    return receive_message_(port_name, slot, {}, false);
}

Message Instance::receive_with_settings(
        std::string const & port_name, Message const & default_msg)
{
    return receive_message_(port_name, {}, default_msg, true);
}


Message Instance::receive_with_settings(
        std::string const & port_name,
        int slot,
        Message const & default_msg)
{
    return receive_message_(port_name, slot, default_msg, true);
}

/* Register this instance with the manager.
 */
void Instance::register_() {
    // TODO: profile this
    auto locations = communicator_.get_locations();
    auto port_list = list_declared_ports_();
    manager_.register_instance(instance_name_, locations, port_list);
    // TODO: stop profile
}

/* Connect this instance to the given peers / conduits.
 */
void Instance::connect_() {
    // TODO: profile this
    auto peer_info = manager_.request_peers(instance_name_);
    communicator_.connect(std::get<0>(peer_info), std::get<1>(peer_info), std::get<2>(peer_info));
    settings_manager_.base = manager_.get_settings();
    // TODO: stop profile
}

/* Deregister this instance from the manager.
 */
void Instance::deregister_() {
    // TODO: profile this
    manager_.deregister_instance(instance_name_);
    // TODO: stop profile
    // This is the last thing we'll profile, so flush messages
    // TODO: shut down profiler
}

Message Instance::receive_message_(
                std::string const & port_name,
                Optional<int> slot,
                Optional<Message> default_msg,
                bool with_settings)
{
    check_port_(port_name);

    Reference port_ref(port_name);
    auto port = communicator_.get_port(port_name);
    if (port.oper == Operator::F_INIT) {
        if (slot.is_set())
            port_ref += slot.get();

        if (f_init_cache_.count(port_ref) == 1) {
            Message msg(f_init_cache_.at(port_ref));
            f_init_cache_.erase(port_ref);

            if (with_settings && !msg.has_settings()) {
                shutdown_();
                throw std::logic_error(
                        "If you use receive_with_settings() on an F_INIT"
                        " port, then you have to pass false to"
                        " reuse_instance(), otherwise the settings will"
                        " already have been applied by MUSCLE.");
            }
            return msg;
        }
        else {
            if (port.is_connected()) {
                std::ostringstream oss;
                oss << "Tried to receive twice on the same port '";
                oss << port_ref << "' in a single F_INIT, that's not possible.";
                oss << " Did you forget to call reuse_instance() in your reuse";
                oss << " loop?";
                shutdown_();
                throw std::logic_error(oss.str());
            }
            else {
                if (default_msg.is_set())
                    return default_msg.get();

                std::ostringstream oss;
                oss << "Tried to receive on port '" << port_ref << "', which";
                oss << " is not connected, and no default value was given.";
                oss << " Please connect this port!";
                shutdown_();
                throw std::logic_error(oss.str());
            }
        }
    }
    else {
        Message msg(communicator_.receive_message(port_name, slot, default_msg));
        if (port.is_connected() && !port.is_open(slot)) {
            std::ostringstream oss;
            oss << "Port '" << port_ref << "' is closed, but we're trying to";
            oss << " receive on it. Did the peer crash?";
            shutdown_();
            throw std::runtime_error(oss.str());
        }
        if (port.is_connected() && !with_settings)
            check_compatibility_(port_name, msg.settings());
        if (!with_settings)
            msg.unset_settings();
        return msg;
    }
    // unreachable, this just to avoid a compiler warning
    return Message(0.0, "Unreachable code, please report a bug in MUSCLE 3.");
}


/* Returns instance name.
 *
 * This takes the argument to the --muscle-instance= command-line option and
 * returns it as a Reference.
 */
Reference Instance::make_full_name_(int argc, char const * const argv[]) const {
    std::string prefix_tag("--muscle-instance=");
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], prefix_tag.c_str(), prefix_tag.size()) == 0) {
            std::string prefix_str(argv[i] + prefix_tag.size());
            return Reference(prefix_str);
        }
    }
    throw std::runtime_error("A --muscle-instance command line argument is"
            " required to identify this instance. Please add one.");
}

/* Gets the manager network location from the command line.
 *
 * We use a --muscle-manager=<host:port> argument to tell the MUSCLE library
 * how to connect to the manager. This function will extract this argument
 * from the command line arguments, if it is present.
 */
std::string Instance::extract_manager_location_(
        int argc, char const * const argv[]) const
{
    std::string prefix_tag("--muscle-manager=");
    for (int i = 1; i < argc; ++i)
        if (strncmp(argv[i], prefix_tag.c_str(), prefix_tag.size()) == 0)
            return std::string(argv[i] + prefix_tag.size());

    return "localhost:9000";
}

/* Returns the compute element name of this instance, i.e. without the index.
 */
Reference Instance::name_() const {
    auto it = instance_name_.cbegin();
    while (it != instance_name_.cend() && it->is_identifier())
        ++it;

    return Reference(instance_name_.cbegin(), it);
}

/* Returns the index of this instance, i.e. without the compute element.
 */
std::vector<int> Instance::index_() const {
    std::vector<int> result;
    auto it = instance_name_.cbegin();
    while (it != instance_name_.cend() && it->is_identifier())
        ++it;

    while (it != instance_name_.cend() && it->is_index()) {
        result.push_back(it->index());
        ++it;
    }
    return result;
}

/* Returns a list of declared ports for this instance.
 *
 * This returns a list of ymmsl::Port objects, which have only the name and
 * operator, not libmuscle::Port, which has more.
 */
std::vector<::ymmsl::Port> Instance::list_declared_ports_() const {
    std::vector<::ymmsl::Port> result;
    for (auto const & oper_ports : declared_ports_) {
        for (auto const & fullname : oper_ports.second) {
            std::string portname(fullname);
            if (fullname.size() > 2 && fullname.substr(fullname.size() - 2) == "[]")
                portname = fullname.substr(0, fullname.size() - 2);
            result.push_back(::ymmsl::Port(portname, oper_ports.first));
        }
    }
    return result;
}

/* Checks that the given port exists, error-exits if not.
 *
 * @param port_name The name of the port to check.
 */
void Instance::check_port_(std::string const & port_name) {
    if (!communicator_.port_exists(port_name)) {
        std::ostringstream oss;
        oss << "Port '" << port_name << "' does not exist on '";
        oss << instance_name_ << "'. Please check the name and the list of";
        oss << " ports you gave for this compute element.";
        shutdown_();
        throw std::logic_error(oss.str());
    }
}

/* Receives settings on muscle_settings_in.
 *
 * @return false iff the port is connected and ClosePort was received.
 */
bool Instance::receive_settings_() {
    Message default_message(0.0, Settings(), Settings());
    auto msg = communicator_.receive_message("muscle_settings_in", {}, default_message);
    if (is_close_port(msg.data()))
        return false;

    if (!msg.data().is_a<Settings>()) {
        std::ostringstream oss;
        oss << "'" << instance_name_ << "' received a message on";
        oss << " muscle_settings_in that is not a Settings. It seems that the";
        oss << " simulation is miswired or the sending instance is broken.";
        shutdown_();
        throw std::logic_error(oss.str());
    }

    Settings settings(msg.settings());
    for (auto const & key_val : msg.data().as<Settings>())
        settings[key_val.first] = key_val.second;
    settings_manager_.overlay = settings;
    return true;
}

/* Pre-receive on the given port and slot, if any.
 */
void Instance::pre_receive_(
        std::string const & port_name, Optional<int> slot,
        bool apply_overlay) {
    Reference port_ref(port_name);
    if (slot.is_set())
        port_ref += slot.get();

    Message msg = communicator_.receive_message(port_name, slot);
    if (apply_overlay) {
        apply_overlay_(msg);
        check_compatibility_(port_name, msg.settings());
        msg.unset_settings();
    }
    f_init_cache_.emplace(port_ref, msg);
}

/* Receives on all ports connected to F_INIT.
 *
 * This receives all incoming messages on F_INIT and stores them in
 * f_init_cache_.
 */
void Instance::pre_receive_f_init_(bool apply_overlay) {
    f_init_cache_.clear();
    auto ports = communicator_.list_ports();
    if (ports.count(Operator::F_INIT) == 1) {
        for (auto const & port_name : ports.at(Operator::F_INIT)) {
            auto port = communicator_.get_port(port_name);
            if (!port.is_connected())
                continue;
            if (!port.is_vector())
                pre_receive_(port_name, {}, apply_overlay);
            else {
                pre_receive_(port_name, 0, apply_overlay);
                // The above receives the length, if needed, so now we can get
                // the rest.
                for (int slot = 0; slot < port.get_length(); ++slot)
                    pre_receive_(port_name, slot, apply_overlay);
            }
        }
    }
}

/* Sets local overlay if we don't already have one.
 *
 * @param message The message to apply the overlay from.
 */
void Instance::apply_overlay_(Message const & message) {
    if (settings_manager_.overlay.empty())
        if (message.has_settings())
            settings_manager_.overlay = message.settings();
}

/* Checks whether a received overlay matches the current one.
 *
 * @param port_name Name of the port on which the overlay was received.
 * @param overlay The received overlay.
 */
void Instance::check_compatibility_(
        std::string const & port_name,
        Optional<Settings> const & overlay)
{
    if (!overlay.is_set())
        return;
    if (settings_manager_.overlay != overlay.get()) {
        std::ostringstream oss;
        oss << "Unexpectedly received data from a parallel universe on port";
        oss << " '" << port_name << "'. My settings are '";
        oss << settings_manager_.overlay << "' and I received from a";
        oss << " universe with '" << overlay << "'.";
        shutdown_();
        throw std::logic_error(oss.str());
    }
}


/* Closes outgoing ports.
 *
 * This sends a close port message on all slots of all outgoing ports.
 */
void Instance::close_outgoing_ports_() {
    for (auto const & oper_ports : communicator_.list_ports()) {
        if (allows_sending(oper_ports.first)) {
            for (auto const & port_name : oper_ports.second) {
                auto const & port = communicator_.get_port(port_name);
                if (port.is_vector()) {
                    for (int slot = 0; slot < port.get_length(); ++slot)
                        communicator_.close_port(port_name, slot);
                }
                else
                    communicator_.close_port(port_name);
            }
        }
    }
}

/* Receives messages until a ClosePort is received.
 *
 * Receives at least once.
 *
 * @param port_name Port to drain.
 */
void Instance::drain_incoming_port_(std::string const & port_name) {
    auto const & port = communicator_.get_port(port_name);
    while (port.is_open())
        communicator_.receive_message(port_name);
}

/* Receives messages until a ClosePort is received.
 *
 * Works with (resizable) vector ports.
 *
 * @param port_name Port to drain.
 */
void Instance::drain_incoming_vector_port_(std::string const & port_name) {
    auto const & port = communicator_.get_port(port_name);

    bool all_closed = true;
    for (int slot = 0; slot < port.get_length(); ++slot)
        if (port.is_open(slot))
            all_closed = false;

    while (!all_closed) {
        all_closed = true;
        for (int slot = 0; slot < port.get_length(); ++slot) {
            if (port.is_open(slot))
                communicator_.receive_message(port_name, slot);
            if (port.is_open(slot))
                all_closed = false;
        }
    }
}

/* Closes incoming ports.
 *
 * This receives on all incoming ports until a ClosePort is received on them,
 * signaling that there will be no more messages, and allowing the sending
 * instance to shut down cleanly.
 */
void Instance::close_incoming_ports_() {
    for (auto const & oper_ports : communicator_.list_ports()) {
        if (allows_receiving(oper_ports.first)) {
            for (auto const & port_name : oper_ports.second) {
                auto const & port = communicator_.get_port(port_name);
                if (!port.is_connected())
                    continue;
                if (port.is_vector())
                    drain_incoming_vector_port_(port_name);
                else
                    drain_incoming_port_(port_name);
            }
        }
    }
}

/* Closes all ports.
 *
 * This sends a close port message on all slots of all outgoing ports, then
 * receives one on all incoming ports.
 */
void Instance::close_ports_() {
    close_outgoing_ports_();
    close_incoming_ports_();
}

/* Shuts down communication with the outside world and deregisters.
 */
void Instance::shutdown_() {
    if (!is_shut_down_) {
        close_ports_();
        communicator_.shutdown();
        deregister_();
        is_shut_down_ = true;
    }
}

}
