#include "controller_ui.hpp"

//--Helper Functions-----------------------------------------------------------
std::string wstring_to_string(const std::wstring& wstr) {
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

//--controller_ui Implimentation------------------------------------------------
controller_ui::controller_ui() : selected_device(0), progress(0.0f), selected_com_port(0), is_started(false) {
    load_com_ports();
    load_audio_devices();
    start_io_context();
}

controller_ui::~controller_ui()
{
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
}

//--controller_ui Methods-------------------------------------------------------
void controller_ui::load_com_ports() {
    // Clear any existing ports
    active_com_ports.clear();

    // Open the registry key where COM port information is stored
    HKEY h_key;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("HARDWARE\\DEVICEMAP\\SERIALCOMM"), 0, KEY_READ, &h_key) == ERROR_SUCCESS) {
        DWORD index = 0;
        TCHAR port_name[256];
        TCHAR com_port[256];
        DWORD port_name_size = sizeof(port_name);
        DWORD com_port_size = sizeof(com_port);

        // Enumerate all COM ports
        while (RegEnumValue(h_key, index, port_name, &port_name_size, NULL, NULL, (LPBYTE)com_port, &com_port_size) == ERROR_SUCCESS) {
            #ifdef UNICODE
                std::wstring ws(com_port); // Use `wstring` directly in Unicode builds
                active_com_ports.push_back(wstring_to_string(ws));
            #else
                std::string s(com_port);    // Use `string` directly in ANSI builds
                active_com_ports.push_back(s);
            #endif

            index++;
            port_name_size = sizeof(port_name);
            com_port_size = sizeof(com_port);
        }

        // Close the registry key after enumeration
        RegCloseKey(h_key);
    }
}

void controller_ui::load_audio_devices() {
    // Initialize COM library
    CoInitialize(nullptr);

    // Use MMDevice API to enumerate audio endpoints
    IMMDeviceEnumerator* p_enumeator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator), (void**)&p_enumeator);

    if (SUCCEEDED(hr)) {
        IMMDeviceCollection* p_collection = nullptr;
        hr = p_enumeator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &p_collection);

        if (SUCCEEDED(hr)) {
            UINT count;
            p_collection->GetCount(&count);

            for (UINT i = 0; i < count; i++) {
                IMMDevice* p_device = nullptr;
                hr = p_collection->Item(i, &p_device);
                if (SUCCEEDED(hr)) {
                    IPropertyStore* p_store;
                    hr = p_device->OpenPropertyStore(STGM_READ, &p_store);
                    if (SUCCEEDED(hr)) {
                        PROPVARIANT var_name;
                        PropVariantInit(&var_name);
                        hr = p_store->GetValue(PKEY_Device_FriendlyName, &var_name);
                        if (SUCCEEDED(hr)) {
                            // Convert LPWSTR to std::string safely using the helper function
                            std::wstring ws(var_name.pwszVal);
                            std::string deviceName = wstring_to_string(ws);
                            // Store device name
                            audio_devices.push_back(deviceName);
                            PropVariantClear(&var_name);
                        }
                        p_store->Release();
                    }
                    p_device->Release();
                }
            }
            p_collection->Release();
        }
        p_enumeator->Release();
    }

    // Uninitialize COM library
    CoUninitialize();
}

float controller_ui::get_current_device_volume(size_t index) {
    // Initialize COM library
    CoInitialize(nullptr);

    IMMDeviceEnumerator* p_enumeator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator), (void**)&p_enumeator);

    float volume = 0.0f;  // Default volume level if no match is found
    bool device_found = false;

    if (SUCCEEDED(hr)) {
        IMMDeviceCollection* p_collection = nullptr;
        hr = p_enumeator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &p_collection);

        if (SUCCEEDED(hr)) {
            UINT count;
            p_collection->GetCount(&count);

            for (UINT i = 0; i < count; i++) {
                IMMDevice* p_device = nullptr;
                hr = p_collection->Item(i, &p_device);
                if (SUCCEEDED(hr)) {
                    IPropertyStore* p_store;
                    hr = p_device->OpenPropertyStore(STGM_READ, &p_store);
                    if (SUCCEEDED(hr)) {
                        PROPVARIANT var_name;
                        PropVariantInit(&var_name);
                        hr = p_store->GetValue(PKEY_Device_FriendlyName, &var_name);

                        // Check if this device matches the selected item
                        if (SUCCEEDED(hr)) {
                            std::wstring ws(var_name.pwszVal);
                            std::string deviceName(ws.begin(), ws.end());
                            if (deviceName == audio_devices[index]) {
                                // Activate the IAudioEndpointVolume interface for the matched device
                                IAudioEndpointVolume* pVolume = nullptr;
                                hr = p_device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&pVolume);
                                if (SUCCEEDED(hr)) {
                                    // Get the current volume level as a float between 0.0 and 1.0
                                    hr = pVolume->GetMasterVolumeLevelScalar(&volume);
                                    pVolume->Release();
                                }
                                device_found = true;  // Mark that we've found the device
                            }
                            PropVariantClear(&var_name);
                        }
                        p_store->Release();
                    }
                    p_device->Release();

                    // Break the loop if the device was found and processed
                    if (device_found) {
                        break;
                    }
                }
            }
            p_collection->Release();
        }
        p_enumeator->Release();
    }

    // Uninitialize COM library
    CoUninitialize();

    // Returns a float value between 0.0 and 1.0
    return volume; 
}

void controller_ui::set_current_device_volume(size_t index, float volume) {
    // Initialize COM library
    CoInitialize(nullptr);

    IMMDeviceEnumerator* p_enumeator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator), (void**)&p_enumeator);

    if (SUCCEEDED(hr)) {
        IMMDeviceCollection* p_collection = nullptr;
        hr = p_enumeator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &p_collection);

        if (SUCCEEDED(hr)) {
            UINT count;
            p_collection->GetCount(&count);

            for (UINT i = 0; i < count; i++) {
                IMMDevice* p_device = nullptr;
                hr = p_collection->Item(i, &p_device);
                if (SUCCEEDED(hr)) {
                    IPropertyStore* p_store;
                    hr = p_device->OpenPropertyStore(STGM_READ, &p_store);
                    if (SUCCEEDED(hr)) {
                        PROPVARIANT var_name;
                        PropVariantInit(&var_name);
                        hr = p_store->GetValue(PKEY_Device_FriendlyName, &var_name);

                        // Check if this device matches the selected item
                        if (SUCCEEDED(hr)) {
                            std::wstring ws(var_name.pwszVal);
                            std::string deviceName(ws.begin(), ws.end());
                            if (deviceName == audio_devices[index]) {
                                // Activate the IAudioEndpointVolume interface for the matched device
                                IAudioEndpointVolume* pVolume = nullptr;
                                hr = p_device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&pVolume);
                                if (SUCCEEDED(hr)) {
                                    // Set the volume level as a float between 0.0 and 1.0
                                    hr = pVolume->SetMasterVolumeLevelScalar(volume, NULL);
                                    pVolume->Release();
                                }
                            }
                            PropVariantClear(&var_name);
                        }
                        p_store->Release();
                    }
                    p_device->Release();
                }
            }
            p_collection->Release();
        }
        p_enumeator->Release();
    }

    // Uninitialize COM library
    CoUninitialize();
}

 














// void controller_ui::toggle_start_stop() {
//     is_started = !is_started;  // Toggle the state
//     // Add additional logic here for start/stop handling if needed
//     if (is_started)
//     {
//         std::cout << "Started" << std::endl;
//     }
//     else
//     {
//         std::cout << "Stopped" << std::endl;
//     }
    
// }

void controller_ui::toggle_start_stop() {
    is_started = !is_started;  // Toggle the state

    if (is_started) {
        if (!serial_port.is_open() && open_serial_port(active_com_ports[selected_com_port].c_str())) {
            std::cout << "Started: Serial port " << active_com_ports[selected_com_port].c_str() << " opened successfully." << std::endl;
        } else {
            std::cerr << "Failed to open serial port " << active_com_ports[selected_com_port].c_str() << std::endl;
            is_started = false;  // Reset to stopped if open fails
        }
    } else {
        close_serial_port();
        std::cout << "Stopped: Serial port " << active_com_ports[selected_com_port].c_str() << " closed." << std::endl;
    }
}

bool controller_ui::open_serial_port(const std::string& port) {
    try {
        serial_port.open(port);
        serial_port.set_option(boost::asio::serial_port_base::baud_rate(115200));
        serial_port.set_option(boost::asio::serial_port_base::character_size(8));
        serial_port.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        serial_port.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        serial_port.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));

        // Start asynchronous read operation
        start_read();

        return true;
    } catch (boost::system::system_error& e) {
        std::cerr << "Error opening serial port: " << e.what() << std::endl;
        return false;
    }
}

void controller_ui::start_read() {
    // Start an asynchronous read, with `handle_read` as the completion handler
    std::cout << "Starting async read..." << std::endl;
    read_buffer.consume(read_buffer.size());
    boost::asio::async_read_until(
        serial_port, read_buffer, '\n',  // Read until a newline character or adjust to your needs
        boost::bind(&controller_ui::handle_read, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred)
    );
    // serial_port.async_read_some(
    //     boost::asio::buffer(read_buffer.prepare(64)),  // Adjust buffer size as needed
    //     boost::bind(&controller_ui::handle_read, this,
    //                 boost::asio::placeholders::error,
    //                 boost::asio::placeholders::bytes_transferred)
    // );
}

void controller_ui::handle_read(const boost::system::error_code& error, std::size_t bytes_transferred) {
    std::cout << "Received: " << std::endl;
    if (!error) {
        read_buffer.commit(bytes_transferred);
        std::istream is(&read_buffer);
        std::string data;
        std::getline(is, data);

        std::cout << "Received: " << data << std::endl;

        // Start another async read operation
        start_read();
    } else {
        std::cerr << "Error reading from serial port: " << error.message() << std::endl;
    }
}

void controller_ui::close_serial_port() {
    if (serial_port.is_open()) {
        boost::system::error_code ec;
        serial_port.cancel(ec);  // Cancel any pending operations
        if (ec) {
            std::cerr << "Error cancelling serial port: " << ec.message() << std::endl;
        }
        serial_port.close(ec);  // Now close the port
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (ec) {
            std::cerr << "Error closing serial port: " << ec.message() << std::endl;
        } else {
            std::cout << "Serial port closed successfully." << std::endl;
        }
    }
}

void controller_ui::start_io_context() {
    io_thread = std::thread([this]() {
        io_context.run();
    });
}

//--controller_ui Rendering-----------------------------------------------------
void controller_ui::render() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 6.0f;  // Adjust for rounded corners (increase for more rounding)
    style.GrabRounding = 4.0f;   // Rounding for grab handles (like sliders)
    style.FramePadding.y = 8.0f;  // Increase vertical padding for a taller appearance

    
    ImGui::Begin("Controller UI", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // Custom width for dropdown, progress bar, and slider
    float custom_width = 450.0f;  // Adjust as desired
    float window_width = ImGui::GetWindowSize().x;
    float center_offset = ((window_width - custom_width) / 2);
    // std::cout << center_offset << std::endl;

    // COM Port selection dropdown
    ImGui::SetCursorPosX(center_offset); 
    ImGui::Text("Active COM Port:");
    ImGui::SetCursorPosX(center_offset); 
    ImGui::SetNextItemWidth(custom_width-150);  // Set dropdown width
    if (ImGui::BeginCombo("##COMPortCombo", active_com_ports[selected_com_port].c_str())) {  // Unique identifier for combo box
        for (int i = 0; i < active_com_ports.size(); ++i) {
            bool is_selected = (selected_com_port == i);
            if (ImGui::Selectable(active_com_ports[i].c_str(), is_selected)) {
                selected_com_port = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    // Start/Stop Button
    ImGui::SameLine();  // Position the button next to the dropdown
    if (is_started) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));  // Red for "Stop"
        if (ImGui::Button("Stop", ImVec2(80.0f, 0.0f))) {
            toggle_start_stop();  // Call placeholder handler function
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.5f, 0.0f, 1.0f));  // Green for "Start"
        if (ImGui::Button("Start", ImVec2(80.0f, 0.0f))) {
            toggle_start_stop();  // Call placeholder handler function
        }
        ImGui::PopStyleColor();
    }

    // Dropdown menu with device names
    ImGui::SetCursorPosX(center_offset); 
    ImGui::Text("Selected Device:");
    ImGui::SetCursorPosX(center_offset); 
    ImGui::SetNextItemWidth(custom_width);  // Set dropdown width
    std::string combo_label = std::to_string(selected_device) + ": " + audio_devices[selected_device];
    if (ImGui::BeginCombo("##DeviceCombo", combo_label.c_str())) {  // Unique identifier for combo box
        for (int i = 0; i < audio_devices.size(); ++i) {
            std::string label = std::to_string(i) + ": " + audio_devices[i];
            bool is_selected = (selected_device == i);
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                selected_device = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // Get the current volume level of the selected device
    progress = get_current_device_volume(selected_device);  // Update progress based on current volume

    // Display the volume level on the progress bar
    ImGui::SetCursorPosX(center_offset); 
    ImGui::Text("Volume Level:");
    ImGui::SetCursorPosX(center_offset); 
    ImGui::ProgressBar(progress, ImVec2(custom_width, 20.0f));  // Use custom width and taller height for progress bar

    // Volume control slider
    ImGui::SetCursorPosX(center_offset); 
    ImGui::Text("Set Volume:");
    ImGui::SetCursorPosX(center_offset); 
    ImGui::SetNextItemWidth(custom_width);  // Set slider width
    if (ImGui::SliderFloat("##VolumeSlider", &progress, 0.0f, 1.0f)) {
        set_current_device_volume(selected_device, progress);
    }




    // Sub-window for channel controls
    ImGui::Separator();
    ImGui::SetCursorPosX(center_offset); 
    ImGui::Text("Channel Volumes:");
    ImGui::SetCursorPosX(center_offset); 
    ImGui::SetCursorPosX(center_offset); 
    ImGui::BeginChild("Channel Window", ImVec2(custom_width, 290), true, ImGuiWindowFlags_HorizontalScrollbar);

    float padding = 20.0f;
    size_t num_channels = audio_devices.size();
    std::vector<float> channel_volumes;
    for (size_t i = 0; i < num_channels; i++)
    {
        channel_volumes.push_back(get_current_device_volume(i));
    }
    
    for (size_t i = 0; i < num_channels; ++i) {
        ImGui::BeginGroup();  // Group each channel’s controls together

        // Vertical slider to adjust volume
        if(ImGui::VSliderFloat(("##ChannelSlider" + std::to_string(i)).c_str(), ImVec2(35, 220), &channel_volumes[i], 0.0f, 1.0f, ""))
        {
            set_current_device_volume(i, channel_volumes[i]);
        }

        // Display the index below the slider in bold
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);  // Add some spacing

        // Calculate the centered position for the text
        float text_width = ImGui::CalcTextSize(std::to_string(i).c_str()).x;
        float slider_center_x = ImGui::GetCursorPosX() + (35 - text_width) * 0.5f;
        ImGui::SetCursorPosX(slider_center_x);

        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);   // Optional: Use a bold font if available
        ImGui::Text("%d", static_cast<int>(i));            // Display index as bold text
        ImGui::PopFont();

        ImGui::EndGroup();

        if (i < num_channels - 1)
        {
            ImGui::SameLine(0.0f, padding);
        }
    }

    ImGui::EndChild();
    ImGui::End();

}


