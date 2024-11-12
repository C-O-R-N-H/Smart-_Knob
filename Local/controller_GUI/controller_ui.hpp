#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include "imgui.h"
#include <string>
#include <vector>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <comdef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <iostream>
#include <thread>



class controller_ui {
public:
    controller_ui();
    ~controller_ui();
    void render();


private:
    std::vector<std::string> audio_devices; 
    int selected_device;       
    std::vector<std::string> active_com_ports; 
    int selected_com_port;
    float progress;       

    bool is_started;   
    boost::asio::io_service io_service;
    boost::asio::serial_port serial_port{io_service};   
    boost::asio::streambuf read_buffer;
    boost::asio::io_context io_context;
    std::thread io_thread;


    float get_current_device_volume(size_t index);
    void load_audio_devices();
    void load_com_ports();
    void set_current_device_volume(size_t index, float volume);
    void toggle_start_stop();

    
    bool open_serial_port(const std::string& port);
    void close_serial_port();
    void start_read();
    void handle_read(const boost::system::error_code& error, std::size_t bytes_transferred);
    void start_io_context();


    void temp();
};

