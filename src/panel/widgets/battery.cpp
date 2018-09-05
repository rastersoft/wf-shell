#include "battery.hpp"
#include <gtk-utils.hpp>
#include <iostream>
#include <algorithm>
#include <config.hpp>

#define UPOWER_NAME "org.freedesktop.UPower"
#define DISPLAY_DEVICE "/org/freedesktop/UPower/devices/DisplayDevice"

#define ICON           "IconName"
#define TYPE           "Type"
#define STATE          "State"
#define PERCENTAGE     "Percentage"
#define TIMETOFULL     "TimeToFull"
#define TIMETOEMPTY    "TimeToEmpty"
#define SHOULD_DISPLAY "IsPresent"

static std::string get_device_type_description(uint32_t type)
{
    if (type == 2)
        return "Battery ";
    if (type == 3)
        return "UPS ";
    return "";
}

void WayfireBatteryInfo::on_properties_changed(
    const Gio::DBus::Proxy::MapChangedProperties& properties,
    const std::vector<Glib::ustring>& invalidated)
{
    bool invalid_icon = false, invalid_details = false;
    bool invalid_state = false;
    for (auto& prop : properties)
    {
        if (prop.first == ICON)
            invalid_icon = true;

        if (prop.first == TYPE || prop.first == STATE || prop.second == PERCENTAGE ||
            prop.first == TIMETOFULL || prop.second == TIMETOEMPTY)
        {
            invalid_details = true;
        }

        if (prop.first == SHOULD_DISPLAY)
            invalid_state = true;
    }

    if (invalid_icon)
        update_icon();

    if (invalid_details)
        update_details();

    if (invalid_state)
        update_state();
}

int WayfireBatteryInfo::calculate_icon_size()
{
    return std::min(panel_size_opt->as_int(), size_opt->as_int());
}

void WayfireBatteryInfo::update_icon()
{
    Glib::Variant<Glib::ustring> icon_name;
    display_device->get_cached_property(icon_name, ICON);

    WfIconLoadOptions options;
    options.invert = invert_opt->as_int();
    options.user_scale = button.get_scale_factor();
    set_image_icon(icon, icon_name.get(), calculate_icon_size(), options);
}

static std::string state_descriptions[] = {
    "Unknown",           // 0
    "Charging",          // 1
    "Discharging",       // 2
    "Empty",             // 3
    "Fully charged",     // 4
    "Pending charge",    // 5
    "Pending discharge", // 6
};

static bool is_charging(uint32_t state)
{
    return (state == 1) || (state == 5);
}

static bool is_discharging(uint32_t state)
{
    return (state == 2) || (state == 6);
}

static std::string format_digit(int digit)
{
    return digit <= 9 ? ("0" + std::to_string(digit)) :
        std::to_string(digit);
}

static std::string uint_to_time(int64_t time)
{
    int hrs = time / 3600;
    int min = (time / 60) % 60;

    return format_digit(hrs) + ":" + format_digit(min);
}

void WayfireBatteryInfo::update_font()
{
    if (font_opt->as_string() == "default")
    {
        label.unset_font();
    } else
    {
        label.override_font(Pango::FontDescription(font_opt->as_string()));
    }
}

void WayfireBatteryInfo::update_details()
{
    Glib::Variant<guint32> type;
    display_device->get_cached_property(type, TYPE);

    Glib::Variant<guint32> vstate;
    display_device->get_cached_property(vstate, STATE);
    uint32_t state = vstate.get();

    Glib::Variant<gdouble> vpercentage;
    display_device->get_cached_property(vpercentage, PERCENTAGE);
    auto percentage_string = std::to_string((int)vpercentage.get()) + "%";

    Glib::Variant<gint64> time_to_full;
    display_device->get_cached_property(time_to_full, TIMETOFULL);

    Glib::Variant<gint64> time_to_empty;
    display_device->get_cached_property(time_to_empty, TIMETOEMPTY);

    std::string description = percentage_string + ", " + state_descriptions[state];
    if (is_charging(state))
    {
        description += ", " + uint_to_time(time_to_full.get()) + " until full";
    }
    else if (is_discharging(state))
    {
        description += ", " + uint_to_time(time_to_empty.get()) + " remaining";
    }

    button.set_tooltip_text(
        get_device_type_description(type.get()) + description);

    if (status == BATTERY_STATUS_PERCENT)
    {
        label.set_text(percentage_string);
    }
    else if (status == BATTERY_STATUS_FULL)
    {
        label.set_text(description);
    } else
    {
        label.set_text("");
    }
}

void WayfireBatteryInfo::update_state()
{
    std::cout << "unimplemented reached, in battery.cpp: "
        "\n\tWayfireBatteryInfo::update_state()" << std::endl;
}

bool WayfireBatteryInfo::setup_dbus()
{
    auto cancellable = Gio::Cancellable::create();
    connection = Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SYSTEM, cancellable);
    if (!connection)
    {
        std::cerr << "Failed to connect to dbus" << std::endl;
        return false;
    }

    upower_proxy = Gio::DBus::Proxy::create_sync(connection, UPOWER_NAME,
                                                 "/org/freedesktop/UPower",
                                                 "org.freedesktop.UPower");
    if (!upower_proxy)
    {
        std::cerr << "Failed to connect to UPower" << std::endl;
        return false;
    }

    display_device = Gio::DBus::Proxy::create_sync(connection,
                                                   UPOWER_NAME,
                                                   DISPLAY_DEVICE,
                                                   "org.freedesktop.UPower.Device");
    if (!display_device)
        return false;

    Glib::Variant<bool> present;
    display_device->get_cached_property(present, SHOULD_DISPLAY);
    if (present.get())
    {
        display_device->signal_properties_changed().connect(
            sigc::mem_fun(this, &WayfireBatteryInfo::on_properties_changed));

        return true;
    }

    return false;
}

// TODO: simplify config loading

static const std::string default_font = "default";
void WayfireBatteryInfo::init(Gtk::HBox *container, wayfire_config *config)
{
    if (!setup_dbus())
        return;

    button_box.add(icon);
    button.get_style_context()->add_class("flat");

    auto section = config->get_section("panel");

    status_opt = section->get_option("battery_status", "1");
    status_updated = [=] () {
        status = (WfBatteryStatusDescription) std::min(2, std::max(status_opt->as_int(), 0));
        update_details();
    };
    status_opt->add_updated_handler(&status_updated);
    status_updated();

    font_opt = section->get_option("battery_font", "default");
    font_updated = [=] () {
        update_font();
    };
    font_opt->add_updated_handler(&font_updated);

    panel_size_opt = section->get_option("panel_thickness", DEFAULT_PANEL_HEIGHT);
    panel_size_updated = [=] () {
        int default_size = panel_size_opt->as_int() * 0.7;
        size_opt->default_value = std::to_string(default_size);
        update_icon();
    };
    panel_size_opt->add_updated_handler(&panel_size_updated);

    int default_size = panel_size_opt->as_int() * 0.7;
    size_opt = section->get_option("battery_icon_size", std::to_string(default_size));
    invert_opt = section->get_option("battery_icon_invert", "0");
    icon_attr_updated = [=] () {
        update_icon();
    };
    size_opt->add_updated_handler(&icon_attr_updated);
    invert_opt->add_updated_handler(&icon_attr_updated);

    update_font();
    update_icon();
    update_details();

    container->pack_start(button, Gtk::PACK_SHRINK);
    button_box.add(label);

    button.add(button_box);

    button.property_scale_factor().signal_changed()
        .connect(sigc::mem_fun(this, &WayfireBatteryInfo::update_icon));
}

WayfireBatteryInfo::~WayfireBatteryInfo()
{
    if (status_opt) // otherwise DBus connection failed altogether
    {
        status_opt->rem_updated_handler(&status_updated);
        font_opt->rem_updated_handler(&font_updated);
        panel_size_opt->rem_updated_handler(&panel_size_updated);
        size_opt->rem_updated_handler(&icon_attr_updated);
        invert_opt->rem_updated_handler(&icon_attr_updated);
    }
}

