/**
 * LED Indicator
 * Copyright (c) 2024 Tomoatsu Shimada/Walbrix Corporation
 * SPDX-License-Identifier: MIT
 */

#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>

#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cerrno>
#include <cstring>

#include <gpiod.hpp>
#include <argparse/argparse.hpp>
#include <sdbus-c++/sdbus-c++.h>

namespace defaults {
    const char* chipname = "gpiochip0";
    const unsigned int line_num = 13;  // GPIO13

    const char* serviceName = "com.walbrix.LedIndicatorService";
    const char* objectPath = "/com/walbrix/LedIndicator";
    const char* interfaceName = "com.walbrix.LedIndicator";
}

const std::string progname = "led-indicator";

std::string serviceName = defaults::serviceName;
std::string objectPath = defaults::objectPath;
std::string interfaceName = defaults::interfaceName;

enum led_action_t { LED_ON, LED_OFF, LED_BLINK };

led_action_t led_action = LED_OFF;

#define PERROR(s) throw std::runtime_error(std::string(s) + ": " + std::strerror(errno))

bool get_expected_led_state(int blink_interval_ms = 500) {
    if (led_action == LED_ON) return true;
    if (led_action == LED_OFF) return false;
    //else
    return (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() / blink_interval_ms) % 2 == 0;
}

auto create_signalfd(const std::vector<int>& signals = {SIGINT, SIGTERM})
{
    sigset_t mask;
    sigemptyset(&mask);
    for (auto signal : signals) {
        sigaddset(&mask, signal);
    }
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) PERROR("sigprocmask");
    int sfd = signalfd(-1, &mask, 0);
    if (sfd < 0) PERROR("signalfd");
    //else
    return sfd;
}

int service(const std::string& chipname, unsigned int line_num)
{
    std::cout << "Registering D-Bus service: " << serviceName << " at " << objectPath << " with interface: " << interfaceName << std::endl;
    auto connection = sdbus::createSystemBusConnection();
    auto object = sdbus::createObject(*connection, objectPath);
    object->registerMethod("set")
        .onInterface(interfaceName)
        .implementedAs([](const std::string& action) {
            if (action == "on") {
                led_action = LED_ON;
                return true;
            }
            else if (action == "off") {
                led_action = LED_OFF;
                return true;
            }
            else if (action == "blink") {
                led_action = LED_BLINK;
                return true;
            }
            else return false;
        });
    object->registerMethod("get")
        .onInterface(interfaceName)
        .implementedAs([]() {
            return std::string(led_action == LED_ON? "on" : led_action == LED_OFF? "off" : "blink");
        });
    object->finishRegistration();
    connection->requestName(serviceName);
    std::cout << "Service registered" << std::endl;

    gpiod::chip chip(chipname);
    gpiod::line line = chip.get_line(line_num);
    line.request({"led-indicator", gpiod::line_request::DIRECTION_OUTPUT}, 0);
    line.set_value(0);

    auto sigfd = create_signalfd();

    bool exit_requested = false;

    while (!exit_requested) {
        struct pollfd fds[2];
        fds[0].fd = connection->getEventLoopPollData().fd;
        fds[0].events = POLLIN;
        fds[1].fd = sigfd;
        fds[1].events = POLLIN;
        if (poll(fds, 2, 100) < 0) PERROR("poll");
        //else

        if (fds[1].revents & POLLIN) exit_requested = true;

        while(connection->processPendingRequest()) {
            ;
        }
        auto expected_led_state = get_expected_led_state();
        if (line.get_value() != expected_led_state) {
            line.set_value(expected_led_state);
        }
    }

    close(sigfd);

    line.set_value(0);
    line.release();

    connection->releaseName(serviceName);
    std::cout << "Exit." << std::endl;
    return EXIT_SUCCESS;
}

int set(const std::string& action)
{
    auto proxy = sdbus::createProxy(serviceName, objectPath);
    bool result;
    proxy->callMethod("set").onInterface(interfaceName).withArguments(action).storeResultsTo(result);
    std::cout << (result? "success" : "error") << std::endl;
    return result? EXIT_SUCCESS : EXIT_FAILURE;
}

int get()
{
    auto proxy = sdbus::createProxy(serviceName, objectPath);
    std::string result;
    proxy->callMethod("get").onInterface(interfaceName).storeResultsTo(result);
    std::cout << result << std::endl;
    return EXIT_SUCCESS;
}

int policyfile()
{
    std::string content = R"(<!DOCTYPE busconfig PUBLIC
 "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<!-- save this as /etc/dbus-1/system.d/PROGNAME.conf -->
<busconfig>
  <policy user="root">
    <allow own="SERVICE_NAME"/>
  </policy>
  <policy context="default">
    <allow send_destination="SERVICE_NAME"/>
    <allow send_interface="INTERFACE_NAME"/>
  </policy>
</busconfig>)";
    content.replace(content.find("PROGNAME"), 8, progname);
    content.replace(content.find("SERVICE_NAME"), 12, serviceName);
    content.replace(content.find("SERVICE_NAME"), 12, serviceName);
    content.replace(content.find("INTERFACE_NAME"), 14, interfaceName);
    std::cout << content << std::endl;
    return EXIT_SUCCESS;
}

int unitfile(const std::string& chipname, unsigned int line_num)
{
    char exepath[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", exepath, PATH_MAX - 1);
    if (count < 0) PERROR("readlink");
    //else
    exepath[count] = '\0';

    std::string opts1 = "";
    if (serviceName != defaults::serviceName) {
        opts1 += " --service-name=" + serviceName;
    }
    std::string opts2 = "";
    if (chipname != defaults::chipname) {
        opts2 += " --chipname=" + chipname;
    }
    if (line_num != defaults::line_num) {
        opts2 += " --line=" + std::to_string(line_num);
    }

    std::string content = R"(# Save this as /etc/systemd/system/PROGNAME.service
[Unit]
Description=LED Indicator Service
DefaultDependencies=no
Before=network-pre.target

[Service]
Type=dbus
BusName=SERVICE_NAME
ExecStart=EXEPATHOPTS1 serviceOPTS2

[Install]
WantedBy=sysinit.target)";
    content.replace(content.find("PROGNAME"), 8, progname);
    content.replace(content.find("SERVICE_NAME"), 12, serviceName);
    content.replace(content.find("EXEPATH"), 7, exepath);
    content.replace(content.find("OPTS1"), 5, opts1);
    content.replace(content.find("OPTS2"), 5, opts2);
    std::cout << content << std::endl;
    return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
    argparse::ArgumentParser program(progname);
    program.add_argument("-s", "--service-name").help("D-Bus service name").default_value(defaults::serviceName);
    program.add_argument("-o", "--object-path").help("D-Bus object path").default_value(defaults::objectPath);
    program.add_argument("-i", "--interface-name").help("D-Bus interface name").default_value(defaults::interfaceName);

    // "service" subcommand
    argparse::ArgumentParser service_command("service");
    service_command.add_description("Run as D-Bus service");
    service_command.add_argument("-c", "--chipname").help("GPIO chip name").default_value(defaults::chipname);
    service_command.add_argument("-l", "--line").help("GPIO line number").default_value(defaults::line_num).scan<'u', unsigned int>();
    program.add_subparser(service_command);

    // "set" subcommand
    argparse::ArgumentParser set_command("set");
    set_command.add_description("Set LED state");
    set_command.add_argument("action");
    program.add_subparser(set_command);

    // "get" subcommand
    argparse::ArgumentParser get_command("get");
    get_command.add_description("Get LED state");
    program.add_subparser(get_command);

    // "policyfile" subcommand
    argparse::ArgumentParser policyfile_command("policyfile");
    policyfile_command.add_description("Print D-Bus policy file");
    program.add_subparser(policyfile_command);

    // "unitfile" subcommand
    argparse::ArgumentParser unitfile_command("unitfile");
    unitfile_command.add_description("Print systemd unit file");
    unitfile_command.add_argument("-c", "--chipname").help("GPIO chip name").default_value(defaults::chipname);
    unitfile_command.add_argument("-l", "--line").help("GPIO line number").default_value(defaults::line_num).scan<'u', unsigned int>();
    program.add_subparser(unitfile_command);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        exit(EXIT_FAILURE);
    }
    //else

    try {
        serviceName = program.get<std::string>("service-name");
        objectPath = program.get<std::string>("object-path");
        interfaceName = program.get<std::string>("interface-name");

        if (program.is_subcommand_used("service")) {
            return service(service_command.get<std::string>("chipname"), service_command.get<unsigned int>("line"));
        } else if (program.is_subcommand_used("set")) {
            return set(set_command.get<std::string>("action"));
        } else if (program.is_subcommand_used("get")) {
            return get();
        } else if (program.is_subcommand_used("policyfile")) {
            return policyfile();
        } else if (program.is_subcommand_used("unitfile")) {
            return unitfile(unitfile_command.get<std::string>("chipname"), unitfile_command.get<unsigned int>("line"));
        } else {
            std::cerr << program;
        }
        //else
        std::cerr << program;
    } catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
    }
    return EXIT_FAILURE;
}
