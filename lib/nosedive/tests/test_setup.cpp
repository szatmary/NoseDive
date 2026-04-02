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

// Helper: build a fake COMM_FW_VERSION response
static std::vector<uint8_t> build_fw_response(uint8_t major, uint8_t minor,
                                               const char* hw_name, uint8_t uuid_base) {
    vesc::Buffer buf;
    buf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::FWVersion));
    buf.append_uint8(major);
    buf.append_uint8(minor);
    for (size_t i = 0; hw_name[i]; i++) buf.append_uint8(hw_name[i]);
    buf.append_uint8(0); // null terminator
    for (int i = 0; i < 12; i++) buf.append_uint8(uuid_base + i); // uuid
    return buf.take();
}

// Helper: run the update→reconnect→verify cycle on the current step
static void do_update_cycle(nosedive::SetupBoard& setup,
                            const char* label,
                            nosedive::SetupStep expected_check_step,
                            uint8_t new_major, uint8_t new_minor,
                            const char* hw_name, uint8_t uuid_base) {
    char msg[128];

    // Should be paused at the check step
    std::snprintf(msg, sizeof(msg), "%s: paused at check step (outdated)", label);
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(expected_check_step), msg);
    std::snprintf(msg, sizeof(msg), "%s: detail mentions update available", label);
    ASSERT(setup.state().detail.find("update available") != std::string::npos, msg);

    // User triggers update
    setup.update();
    std::snprintf(msg, sizeof(msg), "%s: at UpdateFW", label);
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::UpdateFW), msg);

    // Simulate WriteNewAppData ack → WaitReconnect
    uint8_t write_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::WriteNewAppData)};
    setup.handle_response(write_ack, 1);
    std::snprintf(msg, sizeof(msg), "%s: at WaitReconnect", label);
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::WaitReconnect), msg);

    // Simulate reconnect
    setup.on_reconnected();

    // Feed updated FW version response
    auto fw_resp = build_fw_response(new_major, new_minor, hw_name, uuid_base);
    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Should have advanced past WaitReconnect
    std::snprintf(msg, sizeof(msg), "%s: advanced past WaitReconnect", label);
    ASSERT(setup.state().step != nosedive::SetupStep::WaitReconnect &&
           setup.state().step != nosedive::SetupStep::UpdateFW, msg);
}

// --- Setup wizard with CAN devices: update all 3 firmware ---
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

    // --- Express: outdated 6.05 → update to 6.06 ---
    auto express_fw = build_fw_response(6, 5, "VESC Express T", 0xE0);
    setup.handle_response(express_fw.data(), express_fw.size());
    do_update_cycle(setup, "express_update",
                    nosedive::SetupStep::CheckFWExpress,
                    6, 6, "VESC Express T", 0xE0);

    // Should be at CheckFWBMS now
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWBMS),
              "wizard_can: at CheckFWBMS after Express update");

    // --- BMS: outdated 6.05 → update to 6.06 ---
    auto bms_fw = build_fw_response(6, 5, "VESC BMS", 0xB0);
    setup.handle_response(bms_fw.data(), bms_fw.size());
    do_update_cycle(setup, "bms_update",
                    nosedive::SetupStep::CheckFWBMS,
                    6, 6, "VESC BMS", 0xB0);

    // Should be at CheckFWVESC now
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "wizard_can: at CheckFWVESC after BMS update");

    // --- VESC: outdated 6.05 → update to 6.06 ---
    auto vesc_fw = build_fw_response(6, 5, "60_MK6", 0xA0);
    setup.handle_response(vesc_fw.data(), vesc_fw.size());
    do_update_cycle(setup, "vesc_update",
                    nosedive::SetupStep::CheckFWVESC,
                    6, 6, "60_MK6", 0xA0);

    // Should advance to InstallRefloat (no Refloat)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
              "wizard_can: at InstallRefloat after all updates");

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

    setup.set_state_callback([&](const nosedive::SetupState&) {});
    setup.set_send_callback([&](const std::vector<uint8_t>&) {});

    setup.can_device_ids = {};
    setup.main_fw = std::nullopt;
    setup.has_refloat = true;

    setup.start();

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "outdated: starts at CheckFWVESC");

    // Outdated VESC FW (6.05 < latest 6.06)
    auto fw_resp = build_fw_response(6, 5, "60_MK6", 0xA0);
    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Should STAY at CheckFWVESC
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "outdated: paused at CheckFWVESC");
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

    auto fw_resp = build_fw_response(6, 6, "60_MK6", 0xA0); // UP TO DATE
    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Should auto-advance past CheckFWVESC (Refloat installed, so skips that too)
    ASSERT(static_cast<uint8_t>(setup.state().step) >
           static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
           "uptodate: auto-advances past CheckFWVESC");
}

// --- Firmware update flow: update → WaitReconnect → verify ---
static void test_fw_update_flow() {
    nosedive::SetupBoard setup;

    std::vector<std::vector<uint8_t>> sent;
    setup.set_state_callback([&](const nosedive::SetupState&) {});
    setup.set_send_callback([&](const std::vector<uint8_t>& payload) {
        sent.push_back(payload);
    });

    setup.can_device_ids = {};
    setup.main_fw = std::nullopt;
    setup.has_refloat = true;

    setup.start();

    // Send outdated VESC FW response (6.05)
    auto fw_resp = build_fw_response(6, 5, "60_MK6", 0xA0);
    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Run the full update→reconnect→verify cycle
    do_update_cycle(setup, "update_flow",
                    nosedive::SetupStep::CheckFWVESC,
                    6, 6, "60_MK6", 0xA0);

    // Should have advanced to DetectBattery (Refloat already installed)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectBattery),
              "update_flow: at DetectBattery after VESC update");
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

// --- Express FW update flow ---
static void test_express_fw_update() {
    nosedive::SetupBoard setup;

    setup.set_state_callback([&](const nosedive::SetupState&) {});
    setup.set_send_callback([&](const std::vector<uint8_t>&) {});

    setup.can_device_ids = {253}; // Express present
    setup.main_fw = std::nullopt;
    setup.has_refloat = false;

    setup.start();

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWExpress),
              "express_update: at CheckFWExpress");

    // Outdated Express FW (6.04)
    auto express_fw = build_fw_response(6, 4, "VESC Express T", 0xE0);
    setup.handle_response(express_fw.data(), express_fw.size());

    // Run the full update→reconnect→verify cycle
    do_update_cycle(setup, "express_update",
                    nosedive::SetupStep::CheckFWExpress,
                    6, 6, "VESC Express T", 0xE0);

    // Should have advanced to CheckFWVESC (no BMS)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CheckFWVESC),
              "express_update: at CheckFWVESC after Express update");
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
    test_express_fw_update();

    std::printf("\n%d/%d setup tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
