#include "nosedive/nosedive.hpp"
#include "nosedive/setupboard.hpp"
#include "test_helpers.hpp"
#include <vector>
#include <string>

// --- Setup wizard integration test ---
static void test_setup_wizard() {
    nosedive::SetupBoard setup;

    // Track state transitions
    std::vector<nosedive::SetupStep> steps_seen;
    std::vector<std::string> details_seen;
    setup.set_state_callback([&](const nosedive::SetupState& s) {
        steps_seen.push_back(s.step);
        details_seen.push_back(s.detail);
    });

    // Track sent payloads
    std::vector<std::vector<uint8_t>> sent;
    setup.set_send_callback([&](const std::vector<uint8_t>& payload) {
        sent.push_back(payload);
    });

    // No CAN devices (no Express, no BMS) — should skip to VESC FW check
    setup.can_device_ids = {};
    // Provide main FW info — version 6.05 is outdated (latest is 6.06)
    vesc::FWVersion::Response fw;
    fw.major = 6;
    fw.minor = 5;
    fw.hw_name = "60_MK6";
    fw.uuid = "aabbccddeeff";
    fw.custom_config_count = 1; // Refloat installed
    setup.main_fw = fw;
    setup.has_refloat = true;

    // Start wizard — should skip Express, BMS, pause at VESC FW (outdated)
    setup.start();

    // Should pause at CheckFWVESC because FW is outdated
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "wizard: paused at CheckFWVESC (outdated)");
    ASSERT(setup.state().detail.find("update available") != std::string::npos,
           "wizard: detail mentions update available");

    // Skip the firmware update to continue the wizard
    setup.skip();

    // Should be at DetectBattery now (Refloat already installed, skip that too)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectBattery),
              "wizard: at DetectBattery after skip");
    ASSERT(!sent.empty(), "wizard: sent GetValues request");

    // Build a fake COMM_GET_VALUES response
    vesc::Buffer vbuf;
    vbuf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::GetValues));
    vbuf.append_float16(28.0, 10);    // temp_mosfet
    vbuf.append_float16(25.0, 10);    // temp_motor
    vbuf.append_float32(0.0, 100);    // avg_motor_current
    vbuf.append_float32(0.0, 100);    // avg_input_current
    vbuf.append_float32(0.0, 100);    // avg_id
    vbuf.append_float32(0.0, 100);    // avg_iq
    vbuf.append_float16(0.0, 1000);   // duty_cycle
    vbuf.append_float32(0.0, 1);      // rpm
    vbuf.append_float16(63.0, 10);    // voltage
    vbuf.append_float32(0.0, 10000);  // amp_hours
    vbuf.append_float32(0.0, 10000);  // amp_hours_charged
    vbuf.append_float32(0.0, 10000);  // watt_hours
    vbuf.append_float32(0.0, 10000);  // watt_hours_charged
    vbuf.append_int32(0);             // tachometer
    vbuf.append_int32(0);             // tachometer_abs
    vbuf.append_uint8(0);             // fault
    auto values_resp = vbuf.take();

    // Feed GetValues response → should advance past DetectBattery and DetectFootpads
    sent.clear();
    setup.handle_response(values_resp.data(), values_resp.size());

    // DetectFootpads also needs a GetValues response
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
              "wizard: at DetectFootpads");

    setup.handle_response(values_resp.data(), values_resp.size());

    // Now at CalibrateIMU
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CalibrateIMU),
              "wizard: at CalibrateIMU");

    // Build fake COMM_GET_IMU_DATA response
    vesc::Buffer ibuf;
    ibuf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::GetIMUData));
    ibuf.append_uint16(0x001F); // mask
    // Bit 0: RPY
    ibuf.append_float32(0.5, 1e6);  // roll
    ibuf.append_float32(1.2, 1e6);  // pitch
    ibuf.append_float32(0.0, 1e6);  // yaw
    // Bit 1: accel
    ibuf.append_float32(0.0, 1e6);
    ibuf.append_float32(0.0, 1e6);
    ibuf.append_float32(-9.81, 1e6);
    // Bit 2: gyro
    ibuf.append_float32(0.0, 1e6);
    ibuf.append_float32(0.0, 1e6);
    ibuf.append_float32(0.0, 1e6);
    // Bit 3: mag
    ibuf.append_float32(0.25, 1e6);
    ibuf.append_float32(0.0, 1e6);
    ibuf.append_float32(-0.45, 1e6);
    // Bit 4: quaternion
    ibuf.append_float32(1.0, 1e6);
    ibuf.append_float32(0.0, 1e6);
    ibuf.append_float32(0.0, 1e6);
    ibuf.append_float32(0.0, 1e6);
    auto imu_resp = ibuf.take();

    setup.handle_response(imu_resp.data(), imu_resp.size());

    // Now at DetectMotor
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectMotor),
              "wizard: at DetectMotor");

    // Build fake COMM_DETECT_APPLY_ALL_FOC response (success)
    vesc::Buffer dbuf;
    dbuf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::DetectApplyAllFOC));
    dbuf.append_int16(0); // result = 0 = success
    auto detect_resp = dbuf.take();

    setup.handle_response(detect_resp.data(), detect_resp.size());

    // Now at ConfigureWheel
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::ConfigureWheel),
              "wizard: at ConfigureWheel");

    // Build fake COMM_GET_MCCONF response (just cmd byte is enough for our handler)
    uint8_t mcconf_resp[] = {static_cast<uint8_t>(vesc::CommPacketID::GetMCConf)};
    setup.handle_response(mcconf_resp, 1);

    // Should be Done
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::Done),
              "wizard: Done");

    // Verify we saw all the expected steps
    ASSERT(steps_seen.size() >= 6, "wizard: saw enough step transitions");
}

// --- Setup wizard with CAN devices ---
static void test_setup_wizard_with_can() {
    nosedive::SetupBoard setup;

    std::vector<nosedive::SetupStep> steps_seen;
    setup.set_state_callback([&](const nosedive::SetupState& s) {
        steps_seen.push_back(s.step);
    });
    setup.set_send_callback([&](const std::vector<uint8_t>&) {});

    // Express on CAN 253, BMS on CAN 10
    setup.can_device_ids = {10, 253};
    setup.main_fw = std::nullopt;
    setup.has_refloat = false;

    setup.start();

    // Should start with CheckFWExpress
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWExpress),
              "wizard_can: at CheckFWExpress");

    // Build Express FW response (outdated: 6.05)
    vesc::Buffer ebuf;
    ebuf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::FWVersion));
    ebuf.append_uint8(6); // major
    ebuf.append_uint8(5); // minor — outdated
    const char* hw = "VESC Express T";
    for (size_t i = 0; hw[i]; i++) ebuf.append_uint8(hw[i]);
    ebuf.append_uint8(0); // null
    for (int i = 0; i < 12; i++) ebuf.append_uint8(0xE0 + i); // uuid
    auto express_fw = ebuf.take();

    setup.handle_response(express_fw.data(), express_fw.size());

    // Should pause at CheckFWExpress (outdated)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWExpress),
              "wizard_can: paused at CheckFWExpress (outdated)");
    // Skip the update
    setup.skip();

    // Should advance to CheckFWBMS
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWBMS),
              "wizard_can: at CheckFWBMS");

    // Build BMS FW response (outdated: 6.05)
    vesc::Buffer bbuf;
    bbuf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::FWVersion));
    bbuf.append_uint8(6);
    bbuf.append_uint8(5); // outdated
    const char* bms_hw = "VESC BMS";
    for (size_t i = 0; bms_hw[i]; i++) bbuf.append_uint8(bms_hw[i]);
    bbuf.append_uint8(0);
    for (int i = 0; i < 12; i++) bbuf.append_uint8(0xB0 + i);
    auto bms_fw = bbuf.take();

    setup.handle_response(bms_fw.data(), bms_fw.size());

    // Should pause at CheckFWBMS (outdated)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWBMS),
              "wizard_can: paused at CheckFWBMS (outdated)");
    // Skip the update
    setup.skip();

    // Should advance to CheckFWVESC
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "wizard_can: at CheckFWVESC");

    // Build main VESC FW response (outdated: 6.05)
    vesc::Buffer mbuf;
    mbuf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::FWVersion));
    mbuf.append_uint8(6);
    mbuf.append_uint8(5); // outdated
    const char* vesc_hw = "60_MK6";
    for (size_t i = 0; vesc_hw[i]; i++) mbuf.append_uint8(vesc_hw[i]);
    mbuf.append_uint8(0);
    for (int i = 0; i < 12; i++) mbuf.append_uint8(0xA0 + i);
    auto vesc_fw = mbuf.take();

    setup.handle_response(vesc_fw.data(), vesc_fw.size());

    // Should pause at CheckFWVESC (outdated)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "wizard_can: paused at CheckFWVESC (outdated)");
    // Skip the update
    setup.skip();

    // Should advance to InstallRefloat (no Refloat)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
              "wizard_can: at InstallRefloat");

    // Simulate Refloat install complete
    uint8_t install_resp[] = {static_cast<uint8_t>(vesc::CommPacketID::WriteNewAppData)};
    setup.handle_response(install_resp, 1);

    // Should be past InstallRefloat now
    ASSERT(static_cast<uint8_t>(setup.state().step) >
           static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
           "wizard_can: past InstallRefloat");
}

// --- Setup wizard error and retry ---
static void test_setup_wizard_error() {
    nosedive::SetupBoard setup;

    setup.set_state_callback([&](const nosedive::SetupState&) {});
    setup.set_send_callback([&](const std::vector<uint8_t>&) {});

    setup.can_device_ids = {};
    vesc::FWVersion::Response fw;
    fw.major = 6; fw.minor = 5; fw.hw_name = "60_MK6";
    fw.custom_config_count = 1;
    setup.main_fw = fw;
    setup.has_refloat = true;

    setup.start();
    // Paused at CheckFWVESC (outdated) — skip to continue
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "wizard_err: paused at CheckFWVESC (outdated)");
    setup.skip();
    // Now at DetectBattery

    // Feed garbage — should not advance
    uint8_t garbage[] = {0xFF, 0x00};
    setup.handle_response(garbage, 2);
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectBattery),
              "wizard_err: still at DetectBattery after garbage");

    // Skip the step
    setup.skip();
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
              "wizard_err: skip advances");

    // Abort
    setup.abort();
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::Idle),
              "wizard_err: abort goes to Idle");
    ASSERT(!setup.is_running(), "wizard_err: not running after abort");
}

// --- LatestFW::is_outdated unit tests ---
static void test_version_comparison() {
    using LFW = nosedive::LatestFW;

    // Older major
    ASSERT(LFW::is_outdated(5, 99, 6, 6), "version: 5.99 < 6.06");
    // Same major, older minor
    ASSERT(LFW::is_outdated(6, 5, 6, 6), "version: 6.05 < 6.06");
    // Exact match — not outdated
    ASSERT(!LFW::is_outdated(6, 6, 6, 6), "version: 6.06 == 6.06");
    // Newer minor
    ASSERT(!LFW::is_outdated(6, 7, 6, 6), "version: 6.07 > 6.06");
    // Newer major
    ASSERT(!LFW::is_outdated(7, 0, 6, 6), "version: 7.00 > 6.06");
    // Edge: 0.0 vs 0.1
    ASSERT(LFW::is_outdated(0, 0, 0, 1), "version: 0.0 < 0.1");
    ASSERT(!LFW::is_outdated(0, 1, 0, 1), "version: 0.1 == 0.1");
}

// --- Firmware check pauses when outdated ---
static void test_fw_check_outdated_pauses() {
    nosedive::SetupBoard setup;

    std::vector<nosedive::SetupStep> steps_seen;
    setup.set_state_callback([&](const nosedive::SetupState& s) {
        steps_seen.push_back(s.step);
    });
    setup.set_send_callback([&](const std::vector<uint8_t>&) {});

    // No CAN devices, no pre-populated FW — wizard will query VESC FW
    setup.can_device_ids = {};
    setup.main_fw = std::nullopt;
    setup.has_refloat = true;

    setup.start();

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "outdated: starts at CheckFWVESC");

    // Build a FW response with outdated version (6.05 < latest 6.06)
    vesc::Buffer fbuf;
    fbuf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::FWVersion));
    fbuf.append_uint8(6); // major
    fbuf.append_uint8(5); // minor — OUTDATED
    const char* hw = "60_MK6";
    for (size_t i = 0; hw[i]; i++) fbuf.append_uint8(hw[i]);
    fbuf.append_uint8(0); // null terminator
    for (int i = 0; i < 12; i++) fbuf.append_uint8(0xA0 + i); // uuid
    auto fw_resp = fbuf.take();

    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Should STAY at CheckFWVESC — not auto-advance because FW is outdated
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "outdated: paused at CheckFWVESC");

    // Detail should mention update available
    ASSERT(setup.state().detail.find("update available") != std::string::npos,
           "outdated: detail mentions update available");

    // Skip should advance past it
    setup.skip();
    ASSERT(static_cast<uint8_t>(setup.state().step) >
           static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
           "outdated: skip advances past CheckFWVESC");
}

// --- Firmware check auto-advances when up to date ---
static void test_fw_check_uptodate_advances() {
    nosedive::SetupBoard setup;

    setup.set_state_callback([&](const nosedive::SetupState&) {});
    setup.set_send_callback([&](const std::vector<uint8_t>&) {});

    setup.can_device_ids = {};
    setup.main_fw = std::nullopt;
    setup.has_refloat = true;

    setup.start();

    // Build a FW response with current version (6.06 = latest)
    vesc::Buffer fbuf;
    fbuf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::FWVersion));
    fbuf.append_uint8(6);
    fbuf.append_uint8(6); // UP TO DATE
    const char* hw = "60_MK6";
    for (size_t i = 0; hw[i]; i++) fbuf.append_uint8(hw[i]);
    fbuf.append_uint8(0);
    for (int i = 0; i < 12; i++) fbuf.append_uint8(0xA0 + i);
    auto fw_resp = fbuf.take();

    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Should auto-advance past CheckFWVESC (Refloat installed, so skips that too)
    ASSERT(static_cast<uint8_t>(setup.state().step) >
           static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
           "uptodate: auto-advances past CheckFWVESC");

    // Detail on the FW check step should have said "up to date"
    // (We can't easily check historical details, but current step is past it)
}

// --- Firmware update flow: update → WaitReconnect → verify ---
static void test_fw_update_flow() {
    nosedive::SetupBoard setup;

    std::vector<nosedive::SetupStep> steps_seen;
    std::vector<std::vector<uint8_t>> sent;
    setup.set_state_callback([&](const nosedive::SetupState& s) {
        steps_seen.push_back(s.step);
    });
    setup.set_send_callback([&](const std::vector<uint8_t>& payload) {
        sent.push_back(payload);
    });

    setup.can_device_ids = {};
    setup.main_fw = std::nullopt;
    setup.has_refloat = true;

    setup.start();

    // Send outdated FW response
    vesc::Buffer fbuf;
    fbuf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::FWVersion));
    fbuf.append_uint8(6);
    fbuf.append_uint8(5); // OUTDATED
    const char* hw = "60_MK6";
    for (size_t i = 0; hw[i]; i++) fbuf.append_uint8(hw[i]);
    fbuf.append_uint8(0);
    for (int i = 0; i < 12; i++) fbuf.append_uint8(0xA0 + i);
    auto fw_resp = fbuf.take();

    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Paused at CheckFWVESC — user triggers update
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "update_flow: paused at CheckFWVESC");

    sent.clear();
    setup.update();

    // Should be at UpdateFW
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::UpdateFW),
              "update_flow: at UpdateFW");
    // Should have sent erase + write commands
    ASSERT(sent.size() >= 2, "update_flow: sent firmware commands");

    // Simulate WriteNewAppData ack → transitions to WaitReconnect
    uint8_t write_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::WriteNewAppData)};
    setup.handle_response(write_ack, 1);

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::WaitReconnect),
              "update_flow: at WaitReconnect");

    // Simulate board reconnect
    sent.clear();
    setup.on_reconnected();

    // Should have sent FW version query
    ASSERT(!sent.empty(), "update_flow: sent FW query after reconnect");

    // Simulate new FW version response (now 6.06 — updated!)
    vesc::Buffer fbuf2;
    fbuf2.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::FWVersion));
    fbuf2.append_uint8(6);
    fbuf2.append_uint8(6); // NOW UP TO DATE
    for (size_t i = 0; hw[i]; i++) fbuf2.append_uint8(hw[i]);
    fbuf2.append_uint8(0);
    for (int i = 0; i < 12; i++) fbuf2.append_uint8(0xA0 + i);
    auto fw_resp2 = fbuf2.take();

    setup.handle_response(fw_resp2.data(), fw_resp2.size());

    // Should have advanced past CheckFWVESC into later steps
    // (Refloat already installed, so should be at DetectBattery or later)
    ASSERT(static_cast<uint8_t>(setup.state().step) >
           static_cast<uint8_t>(nosedive::SetupStep::WaitReconnect),
           "update_flow: advanced past WaitReconnect after verify");
}

// --- Pre-populated outdated FW should also pause ---
static void test_prepopulated_outdated_fw() {
    nosedive::SetupBoard setup;

    setup.set_state_callback([&](const nosedive::SetupState&) {});
    setup.set_send_callback([&](const std::vector<uint8_t>&) {});

    setup.can_device_ids = {};
    vesc::FWVersion::Response fw;
    fw.major = 6;
    fw.minor = 5; // OUTDATED
    fw.hw_name = "60_MK6";
    fw.uuid = "aabbccddeeff";
    fw.custom_config_count = 1;
    setup.main_fw = fw;
    setup.has_refloat = true;

    setup.start();

    // Should pause at CheckFWVESC because pre-populated FW is outdated
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "prepop_outdated: paused at CheckFWVESC");
    ASSERT(setup.state().detail.find("update available") != std::string::npos,
           "prepop_outdated: detail mentions update available");
}

// --- Express FW check pauses when outdated ---
static void test_express_fw_outdated() {
    nosedive::SetupBoard setup;

    std::vector<nosedive::SetupStep> steps_seen;
    setup.set_state_callback([&](const nosedive::SetupState& s) {
        steps_seen.push_back(s.step);
    });
    setup.set_send_callback([&](const std::vector<uint8_t>&) {});

    setup.can_device_ids = {253}; // Express present
    setup.main_fw = std::nullopt;
    setup.has_refloat = false;

    setup.start();

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWExpress),
              "express_outdated: at CheckFWExpress");

    // Send outdated Express FW response
    vesc::Buffer ebuf;
    ebuf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::FWVersion));
    ebuf.append_uint8(6);
    ebuf.append_uint8(4); // OUTDATED
    const char* hw = "VESC Express T";
    for (size_t i = 0; hw[i]; i++) ebuf.append_uint8(hw[i]);
    ebuf.append_uint8(0);
    for (int i = 0; i < 12; i++) ebuf.append_uint8(0xE0 + i);
    auto express_fw = ebuf.take();

    setup.handle_response(express_fw.data(), express_fw.size());

    // Should pause at CheckFWExpress
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWExpress),
              "express_outdated: paused at CheckFWExpress");
    ASSERT(setup.state().detail.find("update available") != std::string::npos,
           "express_outdated: detail mentions update available");

    // Skip advances
    setup.skip();
    ASSERT(static_cast<uint8_t>(setup.state().step) >
           static_cast<uint8_t>(nosedive::SetupStep::CheckFWExpress),
           "express_outdated: skip advances");
}

int main() {
    test_setup_wizard();
    test_setup_wizard_with_can();
    test_setup_wizard_error();
    test_version_comparison();
    test_fw_check_outdated_pauses();
    test_fw_check_uptodate_advances();
    test_fw_update_flow();
    test_prepopulated_outdated_fw();
    test_express_fw_outdated();

    std::printf("\n%d/%d setup tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
