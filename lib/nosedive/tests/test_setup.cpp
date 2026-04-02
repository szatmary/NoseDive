#include "nosedive/nosedive.hpp"
#include "nosedive/setupboard.hpp"
#include "vesc/vescpkg.hpp"
#include "test_helpers.hpp"
#include <vector>
#include <string>

// A small fake VescPackage for testing installs
static vesc::VescPackage make_test_package() {
    vesc::VescPackage pkg;
    pkg.name = "TestRefloat";
    pkg.lisp_data.resize(50, 0xAA);  // 50 bytes of dummy Lisp
    pkg.qml_data.resize(30, 0xBB);   // 30 bytes of dummy QML
    return pkg;
}

// Simulate the full Refloat install protocol responses.
// The setup must be at InstallRefloat with a package loaded.
static void simulate_refloat_install(nosedive::SetupBoard& setup) {
    // 1. LispEraseCode ack
    uint8_t lisp_erase_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::LispEraseCode), 1};
    setup.handle_response(lisp_erase_ack, sizeof(lisp_erase_ack));

    // 2. LispWriteCode acks — one per chunk (50 bytes fits in one chunk of 400)
    uint8_t lisp_write_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::LispWriteCode), 1, 0, 0, 0, 0};
    setup.handle_response(lisp_write_ack, sizeof(lisp_write_ack));

    // 3. QmluiErase ack
    uint8_t qml_erase_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::QmluiErase), 1};
    setup.handle_response(qml_erase_ack, sizeof(qml_erase_ack));

    // 4. QmluiWrite ack — one chunk
    uint8_t qml_write_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::QmluiWrite), 1, 0, 0, 0, 0};
    setup.handle_response(qml_write_ack, sizeof(qml_write_ack));

    // 5. LispSetRunning ack
    uint8_t set_running_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::LispSetRunning), 1};
    setup.handle_response(set_running_ack, sizeof(set_running_ack));
}

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
    setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""};

    // Start wizard — should skip Express, BMS, pause at VESC FW (outdated)
    setup.start();

    // Should pause at FWVESC + Prompt because FW is outdated
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWVESC),
              "wizard: paused at FWVESC (outdated)");
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "wizard: phase is Prompt");
    ASSERT(setup.state().detail.find("update available") != std::string::npos,
           "wizard: detail mentions update available");

    // Skip the firmware update to continue the wizard
    setup.skip();

    // Should pause at FactoryReset (Prompt)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "wizard: at FactoryReset after FW skip");
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "wizard: FactoryReset phase is Prompt");

    // Skip factory reset
    setup.skip();

    // Should pause at InstallRefloat (up to date, awaiting confirmation)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
              "wizard: at InstallRefloat after FactoryReset skip");
    ASSERT(setup.state().detail.find("up to date") != std::string::npos,
           "wizard: Refloat is up to date");

    // Skip past Refloat (already installed)
    setup.skip();

    // Should be at DetectFootpads now
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
              "wizard: at DetectFootpads after skip");
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

    // Feed GetValues → advance past DetectFootpads
    sent.clear();
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

    // Now at ConfigurePower
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::ConfigurePower),
              "wizard: at ConfigurePower");

    // Feed GetValues for power config → should pause at Prompt with cutoff info
    setup.handle_response(values_resp.data(), values_resp.size());

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::ConfigurePower),
              "wizard: still at ConfigurePower (Prompt)");
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "wizard: ConfigurePower phase is Prompt");

    // Confirm cutoffs
    setup.update();

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

    // Should be at Prompt phase on the check step
    std::snprintf(msg, sizeof(msg), "%s: paused at check step (Prompt)", label);
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(expected_check_step), msg);
    std::snprintf(msg, sizeof(msg), "%s: phase is Prompt", label);
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt), msg);
    std::snprintf(msg, sizeof(msg), "%s: detail mentions update available", label);
    ASSERT(setup.state().detail.find("update available") != std::string::npos, msg);

    // User triggers update — stays on same step, phase → Working
    setup.update();
    std::snprintf(msg, sizeof(msg), "%s: still at same step, Working", label);
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(expected_check_step), msg);
    std::snprintf(msg, sizeof(msg), "%s: phase is Working", label);
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Working), msg);

    // Simulate WriteNewAppData ack → WaitReconnect phase
    uint8_t write_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::WriteNewAppData)};
    setup.handle_response(write_ack, 1);
    std::snprintf(msg, sizeof(msg), "%s: phase is WaitReconnect", label);
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::WaitReconnect), msg);

    // Simulate reconnect
    setup.on_reconnected();

    // Feed updated FW version response
    auto fw_resp = build_fw_response(new_major, new_minor, hw_name, uuid_base);
    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Should have advanced past this step
    std::snprintf(msg, sizeof(msg), "%s: advanced past check step", label);
    ASSERT(setup.state().step != expected_check_step ||
           setup.state().phase != nosedive::StepPhase::WaitReconnect, msg);
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
    setup.refloat_info = std::nullopt;
    auto test_pkg = make_test_package();
    setup.refloat_package = &test_pkg;

    setup.start();

    // Should start with CheckFWExpress
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWExpress),
              "wizard_can: at CheckFWExpress");

    // --- Express: outdated 6.05 → update to 6.06 ---
    auto express_fw = build_fw_response(6, 5, "VESC Express T", 0xE0);
    setup.handle_response(express_fw.data(), express_fw.size());
    do_update_cycle(setup, "express_update",
                    nosedive::SetupStep::FWExpress,
                    6, 6, "VESC Express T", 0xE0);

    // Should be at CheckFWBMS now
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWBMS),
              "wizard_can: at CheckFWBMS after Express update");

    // --- BMS: outdated 6.05 → update to 6.06 ---
    auto bms_fw = build_fw_response(6, 5, "VESC BMS", 0xB0);
    setup.handle_response(bms_fw.data(), bms_fw.size());
    do_update_cycle(setup, "bms_update",
                    nosedive::SetupStep::FWBMS,
                    6, 6, "VESC BMS", 0xB0);

    // Should be at CheckFWVESC now
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWVESC),
              "wizard_can: at CheckFWVESC after BMS update");

    // --- VESC: outdated 6.05 → update to 6.06 ---
    auto vesc_fw = build_fw_response(6, 5, "60_MK6", 0xA0);
    setup.handle_response(vesc_fw.data(), vesc_fw.size());
    do_update_cycle(setup, "vesc_update",
                    nosedive::SetupStep::FWVESC,
                    6, 6, "60_MK6", 0xA0);

    // Should be at FactoryReset + Prompt
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "wizard_can: at FactoryReset after all updates");
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "wizard_can: FactoryReset at Prompt");

    // Skip factory reset
    setup.skip();

    // Should be at InstallRefloat + Prompt (no Refloat → "not installed")
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
              "wizard_can: at InstallRefloat after FactoryReset skip");
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "wizard_can: InstallRefloat at Prompt");

    // User confirms install, then simulate protocol
    setup.update();
    simulate_refloat_install(setup);

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
    setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""};

    setup.start();
    // Paused at CheckFWVESC (outdated) — skip to continue
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWVESC),
              "wizard_err: paused at CheckFWVESC (outdated)");
    setup.skip();
    // Paused at FactoryReset — skip to continue
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "wizard_err: paused at FactoryReset");
    setup.skip();
    // Paused at InstallRefloat (up to date) — skip to continue
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
              "wizard_err: paused at InstallRefloat");
    setup.skip();
    // Now at DetectFootpads

    // Feed garbage — should not advance
    uint8_t garbage[] = {0xFF, 0x00};
    setup.handle_response(garbage, 2);
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
              "wizard_err: still at DetectFootpads after garbage");

    // Skip the step
    setup.skip();
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CalibrateIMU),
              "wizard_err: skip advances to CalibrateIMU");

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
    setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""};

    setup.start();

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWVESC),
              "outdated: starts at CheckFWVESC");

    // Outdated VESC FW (6.05 < latest 6.06)
    auto fw_resp = build_fw_response(6, 5, "60_MK6", 0xA0);
    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Should STAY at CheckFWVESC
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWVESC),
              "outdated: paused at CheckFWVESC");
    ASSERT(setup.state().detail.find("update available") != std::string::npos,
           "outdated: detail mentions update available");

    // Skip should advance to FactoryReset
    setup.skip();
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "outdated: skip advances to FactoryReset");
}

// --- Firmware check auto-advances when up to date ---
static void test_fw_check_uptodate_advances() {
    nosedive::SetupBoard setup;

    setup.set_state_callback([&](const nosedive::SetupState&) {});
    setup.set_send_callback([&](const std::vector<uint8_t>&) {});

    setup.can_device_ids = {};
    setup.main_fw = std::nullopt;
    setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""};

    setup.start();

    auto fw_resp = build_fw_response(6, 6, "60_MK6", 0xA0); // UP TO DATE
    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Should advance past CheckFWVESC and pause at FactoryReset
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "uptodate: advances to FactoryReset");
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
    setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""};

    setup.start();

    // Send outdated VESC FW response (6.05)
    auto fw_resp = build_fw_response(6, 5, "60_MK6", 0xA0);
    setup.handle_response(fw_resp.data(), fw_resp.size());

    // Run the full update→reconnect→verify cycle
    do_update_cycle(setup, "update_flow",
                    nosedive::SetupStep::FWVESC,
                    6, 6, "60_MK6", 0xA0);

    // Should pause at FactoryReset
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "update_flow: at FactoryReset after VESC update");
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
    setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""};

    setup.start();

    // Should pause at CheckFWVESC because pre-populated FW is outdated
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWVESC),
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
    setup.refloat_info = std::nullopt;

    setup.start();

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWExpress),
              "express_update: at CheckFWExpress");

    // Outdated Express FW (6.04)
    auto express_fw = build_fw_response(6, 4, "VESC Express T", 0xE0);
    setup.handle_response(express_fw.data(), express_fw.size());

    // Run the full update→reconnect→verify cycle
    do_update_cycle(setup, "express_update",
                    nosedive::SetupStep::FWExpress,
                    6, 6, "VESC Express T", 0xE0);

    // Should have advanced to CheckFWVESC (no BMS)
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWVESC),
              "express_update: at CheckFWVESC after Express update");
}

// --- Refloat install flow ---
static void test_refloat_install() {
    nosedive::SetupBoard setup;

    std::vector<nosedive::SetupStep> steps_seen;
    std::vector<std::string> details_seen;
    std::vector<std::vector<uint8_t>> sent;

    setup.set_state_callback([&](const nosedive::SetupState& s) {
        steps_seen.push_back(s.step);
        details_seen.push_back(s.detail);
    });
    setup.set_send_callback([&](const std::vector<uint8_t>& payload) {
        sent.push_back(payload);
    });

    // Up-to-date FW, no Refloat
    vesc::FWVersion::Response fw;
    fw.major = 6; fw.minor = 6; fw.hw_name = "60_MK6";
    fw.uuid = "test-uuid";
    setup.can_device_ids = {};
    setup.main_fw = fw;
    setup.refloat_info = std::nullopt;

    auto pkg = make_test_package();
    setup.refloat_package = &pkg;

    setup.start();

    // Should pause at FactoryReset first
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "refloat_install: at FactoryReset first");
    setup.skip();

    // Should pause at InstallRefloat with "not installed" prompt
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
              "refloat_install: at InstallRefloat");
    ASSERT(setup.state().detail.find("not installed") != std::string::npos,
           "refloat_install: detail says not installed");

    // Skip should be blocked (not installed)
    auto step_before = setup.state().step;
    setup.skip();
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(step_before),
              "refloat_install: skip is blocked when not installed");

    // User confirms install
    setup.update();

    // Should have sent LispEraseCode
    ASSERT(!sent.empty(), "refloat_install: sent erase command");
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::LispEraseCode),
              "refloat_install: first command is LispEraseCode");

    // Drive the full install
    sent.clear();

    // 1. LispEraseCode ack → should send LispWriteCode chunk
    uint8_t lisp_erase_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::LispEraseCode), 1};
    setup.handle_response(lisp_erase_ack, sizeof(lisp_erase_ack));
    ASSERT(!sent.empty(), "refloat_install: sent lisp write after erase");
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::LispWriteCode),
              "refloat_install: sent LispWriteCode");
    // Verify the chunk contains offset 0 and data
    ASSERT(sent.back().size() == 5 + 50, "refloat_install: lisp chunk is full 50 bytes");

    // 2. LispWriteCode ack → should send QmluiErase
    sent.clear();
    uint8_t lisp_write_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::LispWriteCode), 1, 0, 0, 0, 0};
    setup.handle_response(lisp_write_ack, sizeof(lisp_write_ack));
    ASSERT(!sent.empty(), "refloat_install: sent qml erase after lisp");
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::QmluiErase),
              "refloat_install: sent QmluiErase");

    // 3. QmluiErase ack → should send QmluiWrite chunk
    sent.clear();
    uint8_t qml_erase_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::QmluiErase), 1};
    setup.handle_response(qml_erase_ack, sizeof(qml_erase_ack));
    ASSERT(!sent.empty(), "refloat_install: sent qml write after erase");
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::QmluiWrite),
              "refloat_install: sent QmluiWrite");
    ASSERT(sent.back().size() == 5 + 30, "refloat_install: qml chunk is full 30 bytes");

    // 4. QmluiWrite ack → should send LispSetRunning
    sent.clear();
    uint8_t qml_write_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::QmluiWrite), 1, 0, 0, 0, 0};
    setup.handle_response(qml_write_ack, sizeof(qml_write_ack));
    ASSERT(!sent.empty(), "refloat_install: sent set_running after qml");
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::LispSetRunning),
              "refloat_install: sent LispSetRunning");
    ASSERT(sent.back().size() == 2 && sent.back()[1] == 1,
           "refloat_install: LispSetRunning payload is [cmd, 1]");

    // 5. LispSetRunning ack → should advance past InstallRefloat
    uint8_t running_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::LispSetRunning), 1};
    setup.handle_response(running_ack, sizeof(running_ack));

    ASSERT(setup.refloat_info.has_value(), "refloat_install: refloat_info set after install");
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
              "refloat_install: at DetectBattery after install");

    // Verify progress messages were reported
    bool saw_lisp_progress = false;
    bool saw_qml_progress = false;
    for (auto& d : details_seen) {
        if (d.find("Uploading Lisp") != std::string::npos) saw_lisp_progress = true;
        if (d.find("Uploading QML") != std::string::npos) saw_qml_progress = true;
    }
    ASSERT(saw_lisp_progress, "refloat_install: saw Lisp upload progress");
    ASSERT(saw_qml_progress, "refloat_install: saw QML upload progress");
}

// --- Refloat install with multi-chunk upload ---
static void test_refloat_install_chunked() {
    nosedive::SetupBoard setup;

    std::vector<std::vector<uint8_t>> sent;
    setup.set_state_callback([&](const nosedive::SetupState&) {});
    setup.set_send_callback([&](const std::vector<uint8_t>& payload) {
        sent.push_back(payload);
    });

    // Create a package with data larger than one chunk (400 bytes)
    vesc::VescPackage pkg;
    pkg.name = "BigRefloat";
    pkg.lisp_data.resize(900, 0xCC);  // 900 bytes = 3 chunks (400+400+100)
    pkg.qml_data.resize(10, 0xDD);    // small QML

    vesc::FWVersion::Response fw;
    fw.major = 6; fw.minor = 6; fw.hw_name = "60_MK6";
    setup.can_device_ids = {};
    setup.main_fw = fw;
    setup.refloat_info = std::nullopt;
    setup.refloat_package = &pkg;

    setup.start();

    // Skip FactoryReset
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "chunked: at FactoryReset first");
    setup.skip();

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
              "chunked: at InstallRefloat");

    // User confirms install
    setup.update();

    // LispEraseCode ack
    uint8_t erase_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::LispEraseCode), 1};
    setup.handle_response(erase_ack, sizeof(erase_ack));

    // Should have sent first LispWriteCode chunk (400 bytes)
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::LispWriteCode),
              "chunked: sent first lisp chunk");
    ASSERT(sent.back().size() == 5 + 400, "chunked: first chunk is 400 bytes");

    // Ack chunk 1 → should send chunk 2
    sent.clear();
    uint8_t write_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::LispWriteCode), 1, 0, 0, 0, 0};
    setup.handle_response(write_ack, sizeof(write_ack));
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::LispWriteCode),
              "chunked: sent second lisp chunk");
    ASSERT(sent.back().size() == 5 + 400, "chunked: second chunk is 400 bytes");
    // Verify offset is 400 in the packet
    ASSERT_EQ(sent.back()[1], 0, "chunked: offset byte 0");
    ASSERT_EQ(sent.back()[2], 0, "chunked: offset byte 1");
    ASSERT_EQ(sent.back()[3], 1, "chunked: offset byte 2 (400>>8=1)");
    ASSERT_EQ(sent.back()[4], static_cast<uint8_t>(400 & 0xFF), "chunked: offset byte 3 (400&0xFF)");

    // Ack chunk 2 → should send chunk 3 (100 bytes remaining)
    sent.clear();
    setup.handle_response(write_ack, sizeof(write_ack));
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::LispWriteCode),
              "chunked: sent third lisp chunk");
    ASSERT(sent.back().size() == 5 + 100, "chunked: third chunk is 100 bytes");

    // Ack chunk 3 → lisp done, should send QmluiErase
    sent.clear();
    setup.handle_response(write_ack, sizeof(write_ack));
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::QmluiErase),
              "chunked: sent QmluiErase after all lisp chunks");

    // Complete the rest of the install quickly
    uint8_t qml_erase_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::QmluiErase), 1};
    setup.handle_response(qml_erase_ack, sizeof(qml_erase_ack));
    uint8_t qml_write_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::QmluiWrite), 1, 0, 0, 0, 0};
    setup.handle_response(qml_write_ack, sizeof(qml_write_ack));
    uint8_t running_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::LispSetRunning), 1};
    setup.handle_response(running_ack, sizeof(running_ack));

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
              "chunked: at DetectBattery after full install");
}

// --- Refloat version check: outdated pauses, up-to-date advances ---
static void test_refloat_version_check() {
    // Test 1: Up-to-date Refloat pauses for confirmation, skip() continues
    {
        nosedive::SetupBoard setup;
        setup.set_state_callback([&](const nosedive::SetupState&) {});
        setup.set_send_callback([&](const std::vector<uint8_t>&) {});

        vesc::FWVersion::Response fw;
        fw.major = 6; fw.minor = 6; fw.hw_name = "60_MK6";
        setup.can_device_ids = {};
        setup.main_fw = fw;
        setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""}; // latest

        setup.start();

        // Skip FactoryReset
        setup.skip();

        // Should pause at InstallRefloat
        ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
                  static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
                  "refloat_ver: up-to-date pauses at InstallRefloat");
        ASSERT(setup.state().detail.find("up to date") != std::string::npos,
               "refloat_ver: detail says up to date");

        // skip() continues
        setup.skip();
        ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
                  static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
                  "refloat_ver: skip advances to DetectFootpads");
    }

    // Test 2: Outdated Refloat pauses
    {
        nosedive::SetupBoard setup;
        setup.set_state_callback([&](const nosedive::SetupState&) {});
        setup.set_send_callback([&](const std::vector<uint8_t>&) {});

        vesc::FWVersion::Response fw;
        fw.major = 6; fw.minor = 6; fw.hw_name = "60_MK6";
        setup.can_device_ids = {};
        setup.main_fw = fw;
        setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 1, 0, ""}; // outdated

        setup.start();
        setup.skip(); // Skip FactoryReset

        ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
                  static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
                  "refloat_ver: outdated pauses at InstallRefloat");
        ASSERT(setup.state().detail.find("update available") != std::string::npos,
               "refloat_ver: detail mentions update available");
    }

    // Test 3: Outdated Refloat — skip advances past it
    {
        nosedive::SetupBoard setup;
        setup.set_state_callback([&](const nosedive::SetupState&) {});
        setup.set_send_callback([&](const std::vector<uint8_t>&) {});

        vesc::FWVersion::Response fw;
        fw.major = 6; fw.minor = 6; fw.hw_name = "60_MK6";
        setup.can_device_ids = {};
        setup.main_fw = fw;
        setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 0, 0, ""}; // outdated

        setup.start();
        setup.skip(); // Skip FactoryReset

        ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
                  static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
                  "refloat_ver_skip: paused at InstallRefloat");

        setup.skip();
        ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
                  static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
                  "refloat_ver_skip: skip advances to DetectFootpads");
    }

    // Test 4: Outdated Refloat — update() triggers install
    {
        nosedive::SetupBoard setup;
        std::vector<std::vector<uint8_t>> sent;
        setup.set_state_callback([&](const nosedive::SetupState&) {});
        setup.set_send_callback([&](const std::vector<uint8_t>& p) {
            sent.push_back(p);
        });

        vesc::FWVersion::Response fw;
        fw.major = 6; fw.minor = 6; fw.hw_name = "60_MK6";
        setup.can_device_ids = {};
        setup.main_fw = fw;
        setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 0, 0, ""}; // outdated
        auto pkg = make_test_package();
        setup.refloat_package = &pkg;

        setup.start();
        setup.skip(); // Skip FactoryReset

        ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
                  static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
                  "refloat_ver_update: paused at InstallRefloat");

        sent.clear();
        setup.update();

        // Should have started install (sent LispEraseCode)
        ASSERT(!sent.empty(), "refloat_ver_update: sent erase after update()");
        ASSERT_EQ(sent.back()[0],
                  static_cast<uint8_t>(vesc::CommPacketID::LispEraseCode),
                  "refloat_ver_update: sent LispEraseCode");

        // Complete the install
        simulate_refloat_install(setup);

        ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
                  static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
                  "refloat_ver_update: at DetectFootpads after install");
    }
}

// --- LatestRefloat::is_outdated unit tests ---
static void test_refloat_version_comparison() {
    using LR = nosedive::LatestRefloat;
    // Latest is 1.2.1
    ASSERT(LR::is_outdated(1, 2, 0), "refloat_cmp: 1.2.0 < 1.2.1");
    ASSERT(LR::is_outdated(1, 1, 9), "refloat_cmp: 1.1.9 < 1.2.1");
    ASSERT(LR::is_outdated(0, 9, 9), "refloat_cmp: 0.9.9 < 1.2.1");
    ASSERT(!LR::is_outdated(1, 2, 1), "refloat_cmp: 1.2.1 == 1.2.1");
    ASSERT(!LR::is_outdated(1, 2, 2), "refloat_cmp: 1.2.2 > 1.2.1");
    ASSERT(!LR::is_outdated(1, 3, 0), "refloat_cmp: 1.3.0 > 1.2.1");
    ASSERT(!LR::is_outdated(2, 0, 0), "refloat_cmp: 2.0.0 > 1.2.1");
}

// --- Factory reset flow ---
static void test_factory_reset() {
    nosedive::SetupBoard setup;

    std::vector<std::vector<uint8_t>> sent;
    std::vector<std::string> details_seen;
    setup.set_state_callback([&](const nosedive::SetupState& s) {
        details_seen.push_back(s.detail);
    });
    setup.set_send_callback([&](const std::vector<uint8_t>& payload) {
        sent.push_back(payload);
    });

    vesc::FWVersion::Response fw;
    fw.major = 6; fw.minor = 6; fw.hw_name = "60_MK6";
    setup.can_device_ids = {};
    setup.main_fw = fw;
    setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""};

    setup.start();

    // Should pause at FactoryReset with Prompt
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "factory_reset: at FactoryReset");
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "factory_reset: phase is Prompt");

    // Cannot skip from non-Prompt phase (already at Prompt, so skip should work)
    // Test that skip works
    {
        nosedive::SetupBoard setup2;
        setup2.set_state_callback([&](const nosedive::SetupState&) {});
        setup2.set_send_callback([&](const std::vector<uint8_t>&) {});
        vesc::FWVersion::Response fw2;
        fw2.major = 6; fw2.minor = 6; fw2.hw_name = "60_MK6";
        setup2.can_device_ids = {};
        setup2.main_fw = fw2;
        setup2.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""};
        setup2.start();
        setup2.skip();
        ASSERT_EQ(static_cast<uint8_t>(setup2.state().step),
                  static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
                  "factory_reset: skip advances to InstallRefloat");
    }

    // User confirms reset
    sent.clear();
    setup.update();

    // Should be Working, and sent GetMCConfDefault
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Working),
              "factory_reset: phase is Working after update()");
    ASSERT(!sent.empty(), "factory_reset: sent command after update()");
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::GetMCConfDefault),
              "factory_reset: sent GetMCConfDefault");

    // Simulate GetMCConfDefault response (cmd + fake config blob)
    std::vector<uint8_t> mc_default;
    mc_default.push_back(static_cast<uint8_t>(vesc::CommPacketID::GetMCConfDefault));
    for (int i = 0; i < 100; i++) mc_default.push_back(static_cast<uint8_t>(i)); // fake blob
    sent.clear();
    setup.handle_response(mc_default.data(), mc_default.size());

    // Should have sent SetMCConf with the blob
    ASSERT(!sent.empty(), "factory_reset: sent SetMCConf");
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::SetMCConf),
              "factory_reset: sent SetMCConf");
    // Verify the blob content (should match what we sent minus cmd byte)
    ASSERT(sent.back().size() == 101, "factory_reset: SetMCConf has cmd + 100 byte blob");

    // Simulate SetMCConf ack
    uint8_t mc_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::SetMCConf)};
    sent.clear();
    setup.handle_response(mc_ack, sizeof(mc_ack));

    // Should have sent GetAppConfDefault
    ASSERT(!sent.empty(), "factory_reset: sent GetAppConfDefault");
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::GetAppConfDefault),
              "factory_reset: sent GetAppConfDefault");

    // Simulate GetAppConfDefault response
    std::vector<uint8_t> app_default;
    app_default.push_back(static_cast<uint8_t>(vesc::CommPacketID::GetAppConfDefault));
    for (int i = 0; i < 80; i++) app_default.push_back(static_cast<uint8_t>(i + 50));
    sent.clear();
    setup.handle_response(app_default.data(), app_default.size());

    // Should have sent SetAppConf with the blob
    ASSERT(!sent.empty(), "factory_reset: sent SetAppConf");
    ASSERT_EQ(sent.back()[0],
              static_cast<uint8_t>(vesc::CommPacketID::SetAppConf),
              "factory_reset: sent SetAppConf");
    ASSERT(sent.back().size() == 81, "factory_reset: SetAppConf has cmd + 80 byte blob");

    // Simulate SetAppConf ack
    uint8_t app_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::SetAppConf)};
    setup.handle_response(app_ack, sizeof(app_ack));

    // Should have advanced to InstallRefloat
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
              "factory_reset: at InstallRefloat after reset complete");

    // Verify progress messages
    bool saw_motor_reset = false;
    bool saw_app_reset = false;
    bool saw_complete = false;
    for (auto& d : details_seen) {
        if (d.find("motor configuration") != std::string::npos) saw_motor_reset = true;
        if (d.find("app configuration") != std::string::npos) saw_app_reset = true;
        if (d.find("Factory reset complete") != std::string::npos) saw_complete = true;
    }
    ASSERT(saw_motor_reset, "factory_reset: saw motor reset progress");
    ASSERT(saw_app_reset, "factory_reset: saw app reset progress");
    ASSERT(saw_complete, "factory_reset: saw completion message");
}

// Helper: build a fake BMS response
static std::vector<uint8_t> build_bms_response(uint8_t cell_count, double voltage,
                                                 double current, double soc) {
    vesc::Buffer buf;
    buf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::BMSGetValues));
    buf.append_float32(voltage, 1e6);  // total voltage
    buf.append_float32(current, 1e6);  // current
    buf.append_float32(soc, 1e6);      // SoC (0-1)
    buf.append_uint8(cell_count);
    double cell_v = voltage / cell_count;
    for (uint8_t i = 0; i < cell_count; i++) {
        buf.append_float16(cell_v, 1000);
    }
    // Balancing bitmap (8 bytes)
    for (int i = 0; i < 8; i++) buf.append_uint8(0);
    // Temp sensors
    buf.append_uint8(2);
    buf.append_float16(28.0, 100);
    buf.append_float16(29.5, 100);
    // Humidity
    buf.append_float16(35.0, 100);
    return buf.take();
}

// --- ConfigurePower queries BMS when present ---
static void test_configure_power_with_bms() {
    nosedive::SetupBoard setup;

    std::vector<std::vector<uint8_t>> sent;
    setup.set_state_callback([&](const nosedive::SetupState&) {});
    setup.set_send_callback([&](const std::vector<uint8_t>& p) {
        sent.push_back(p);
    });

    vesc::FWVersion::Response fw;
    fw.major = 6; fw.minor = 6; fw.hw_name = "60_MK6";
    setup.can_device_ids = {10}; // BMS on CAN 10
    setup.main_fw = fw;
    setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""};

    setup.start();
    // Skip through to ConfigurePower: FactoryReset→InstallRefloat→...
    // FWExpress skipped (no Express), FWBMS check starts
    // Feed BMS FW response (up to date)
    auto bms_fw = build_fw_response(6, 6, "VESC BMS", 0xB0);
    setup.handle_response(bms_fw.data(), bms_fw.size());
    // Auto-advances past FWBMS → FWVESC (FW pre-populated, up to date) → FactoryReset
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "power_bms: at FactoryReset");
    setup.skip(); // Skip FactoryReset
    setup.skip(); // Skip InstallRefloat (up to date)

    // At DetectFootpads — skip through remaining detection steps
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
              "power_bms: at DetectFootpads");
    setup.skip(); // DetectFootpads
    setup.skip(); // CalibrateIMU
    setup.skip(); // DetectMotor
    setup.skip(); // ConfigureWheel

    // Now at ConfigurePower
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::ConfigurePower),
              "power_bms: at ConfigurePower");

    // Should have sent ForwardCAN + BMSGetValues (not plain GetValues)
    ASSERT(!sent.empty(), "power_bms: sent command");
    auto& last = sent.back();
    ASSERT_EQ(last[0], static_cast<uint8_t>(vesc::CommPacketID::ForwardCAN),
              "power_bms: sent ForwardCAN");
    ASSERT_EQ(last[1], 10, "power_bms: target CAN ID 10");
    ASSERT_EQ(last[2], static_cast<uint8_t>(vesc::CommPacketID::BMSGetValues),
              "power_bms: inner payload is BMSGetValues");

    // Feed BMS response (20S battery, 75.6V, 0A, 85% SoC)
    auto bms_resp = build_bms_response(20, 75.6, 0.0, 0.85);
    setup.handle_response(bms_resp.data(), bms_resp.size());

    // Should pause at Prompt with cutoff info
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::ConfigurePower),
              "power_bms: at ConfigurePower Prompt");
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "power_bms: phase is Prompt");
    ASSERT(setup.state().detail.find("20S") != std::string::npos,
           "power_bms: detail mentions 20S");
    ASSERT(setup.state().detail.find("cutoff") != std::string::npos,
           "power_bms: detail mentions cutoffs");

    // Confirm — should send SetBatteryCut and advance to Done
    sent.clear();
    setup.update();

    // Verify SetBatteryCut was sent
    bool sent_battery_cut = false;
    for (auto& p : sent) {
        if (!p.empty() && p[0] == static_cast<uint8_t>(vesc::CommPacketID::SetBatteryCut)) {
            sent_battery_cut = true;
        }
    }
    ASSERT(sent_battery_cut, "power_bms: sent SetBatteryCut");

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::Done),
              "power_bms: at Done after confirm");
}

// --- ConfigurePower falls back to GetValues without BMS ---
static void test_configure_power_no_bms() {
    nosedive::SetupBoard setup;

    std::vector<std::vector<uint8_t>> sent;
    setup.set_state_callback([&](const nosedive::SetupState&) {});
    setup.set_send_callback([&](const std::vector<uint8_t>& p) {
        sent.push_back(p);
    });

    vesc::FWVersion::Response fw;
    fw.major = 6; fw.minor = 6; fw.hw_name = "60_MK6";
    setup.can_device_ids = {}; // No BMS
    setup.main_fw = fw;
    setup.refloat_info = vesc::RefloatInfo{"Refloat", 1, 2, 1, ""};

    setup.start();
    setup.skip(); // FactoryReset
    setup.skip(); // InstallRefloat
    setup.skip(); // DetectFootpads
    setup.skip(); // CalibrateIMU
    setup.skip(); // DetectMotor
    setup.skip(); // ConfigureWheel

    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::ConfigurePower),
              "power_no_bms: at ConfigurePower");

    // Should have sent plain GetValues (no BMS on CAN)
    ASSERT(!sent.empty(), "power_no_bms: sent command");
    ASSERT_EQ(sent.back()[0], static_cast<uint8_t>(vesc::CommPacketID::GetValues),
              "power_no_bms: sent GetValues (no BMS)");

    // Feed a GetValues response with ~75.6V (should estimate 20S)
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
    vbuf.append_float16(75.6, 10);    // voltage (~20S)
    vbuf.append_float32(0.0, 10000);  // amp_hours
    vbuf.append_float32(0.0, 10000);  // amp_hours_charged
    vbuf.append_float32(0.0, 10000);  // watt_hours
    vbuf.append_float32(0.0, 10000);  // watt_hours_charged
    vbuf.append_int32(0);             // tachometer
    vbuf.append_int32(0);             // tachometer_abs
    vbuf.append_uint8(0);             // fault
    auto values_resp = vbuf.take();

    setup.handle_response(values_resp.data(), values_resp.size());

    // Should be at Prompt with estimated cell count
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "power_no_bms: phase is Prompt");
    ASSERT(setup.state().detail.find("20S") != std::string::npos,
           "power_no_bms: estimated 20S");

    // Skip should advance to Done without sending SetBatteryCut
    sent.clear();
    setup.skip();
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::Done),
              "power_no_bms: skip advances to Done");
    // Verify no SetBatteryCut was sent
    bool sent_cut = false;
    for (auto& p : sent) {
        if (!p.empty() && p[0] == static_cast<uint8_t>(vesc::CommPacketID::SetBatteryCut))
            sent_cut = true;
    }
    ASSERT(!sent_cut, "power_no_bms: skip did not send SetBatteryCut");
}

// --- Cell count estimation ---
static void test_cell_count_estimation() {
    using SB = nosedive::SetupBoard;
    // 20S: nominal ~74V (20*3.7)
    ASSERT_EQ(SB::estimate_cell_count(75.6), 20, "cells: 75.6V → 20S");
    ASSERT_EQ(SB::estimate_cell_count(74.0), 20, "cells: 74.0V → 20S");
    // 15S: nominal ~55.5V
    ASSERT_EQ(SB::estimate_cell_count(55.0), 15, "cells: 55.0V → 15S");
    ASSERT_EQ(SB::estimate_cell_count(56.0), 15, "cells: 56.0V → 15S");
    // 16S: nominal ~59.2V
    ASSERT_EQ(SB::estimate_cell_count(59.0), 16, "cells: 59.0V → 16S");
    // 18S: nominal ~66.6V
    ASSERT_EQ(SB::estimate_cell_count(67.0), 18, "cells: 67.0V → 18S");
    // 24S: nominal ~88.8V
    ASSERT_EQ(SB::estimate_cell_count(89.0), 24, "cells: 89.0V → 24S");
    // Edge: 0V
    ASSERT_EQ(SB::estimate_cell_count(0.0), 0, "cells: 0V → 0S");
}

// --- BMSGetValues decode ---
static void test_bms_get_values_decode() {
    auto bms_resp = build_bms_response(20, 75.6, -2.5, 0.85);
    auto parsed = vesc::BMSGetValues::Response::decode(bms_resp.data(), bms_resp.size());
    ASSERT(parsed.has_value(), "bms_decode: parsed OK");
    ASSERT_EQ(parsed->cell_count, 20, "bms_decode: 20 cells");
    ASSERT(parsed->voltage > 75.0 && parsed->voltage < 76.0,
           "bms_decode: voltage ~75.6V");
    ASSERT(parsed->current < -2.0 && parsed->current > -3.0,
           "bms_decode: current ~-2.5A");
    ASSERT(parsed->soc > 0.84 && parsed->soc < 0.86,
           "bms_decode: SoC ~0.85");
    ASSERT_EQ(parsed->cell_voltages.size(), 20u,
              "bms_decode: 20 cell voltages");
    ASSERT_EQ(parsed->temp_count, 2, "bms_decode: 2 temp sensors");
    ASSERT(parsed->temperatures[0] > 27.0 && parsed->temperatures[0] < 29.0,
           "bms_decode: temp0 ~28°C");

    // Too short → nullopt
    uint8_t too_short[] = {static_cast<uint8_t>(vesc::CommPacketID::BMSGetValues), 0, 0};
    ASSERT(!vesc::BMSGetValues::Response::decode(too_short, 3).has_value(),
           "bms_decode: rejects too-short payload");
}

// Helper: simulate factory reset protocol (GetMCConfDefault→SetMCConf→GetAppConfDefault→SetAppConf)
static void simulate_factory_reset(nosedive::SetupBoard& setup) {
    // GetMCConfDefault response
    std::vector<uint8_t> mc_default;
    mc_default.push_back(static_cast<uint8_t>(vesc::CommPacketID::GetMCConfDefault));
    mc_default.resize(101, 0x11);
    setup.handle_response(mc_default.data(), mc_default.size());

    // SetMCConf ack
    uint8_t mc_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::SetMCConf)};
    setup.handle_response(mc_ack, sizeof(mc_ack));

    // GetAppConfDefault response
    std::vector<uint8_t> app_default;
    app_default.push_back(static_cast<uint8_t>(vesc::CommPacketID::GetAppConfDefault));
    app_default.resize(81, 0x22);
    setup.handle_response(app_default.data(), app_default.size());

    // SetAppConf ack
    uint8_t app_ack[] = {static_cast<uint8_t>(vesc::CommPacketID::SetAppConf)};
    setup.handle_response(app_ack, sizeof(app_ack));
}

// Helper: build a fake COMM_GET_VALUES response with given voltage
static std::vector<uint8_t> build_values_response(double voltage) {
    vesc::Buffer buf;
    buf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::GetValues));
    buf.append_float16(28.0, 10);     // temp_mosfet
    buf.append_float16(25.0, 10);     // temp_motor
    buf.append_float32(0.0, 100);     // avg_motor_current
    buf.append_float32(0.0, 100);     // avg_input_current
    buf.append_float32(0.0, 100);     // avg_id
    buf.append_float32(0.0, 100);     // avg_iq
    buf.append_float16(0.0, 1000);    // duty_cycle
    buf.append_float32(0.0, 1);       // rpm
    buf.append_float16(voltage, 10);  // voltage
    buf.append_float32(0.0, 10000);   // amp_hours
    buf.append_float32(0.0, 10000);   // amp_hours_charged
    buf.append_float32(0.0, 10000);   // watt_hours
    buf.append_float32(0.0, 10000);   // watt_hours_charged
    buf.append_int32(0);              // tachometer
    buf.append_int32(0);              // tachometer_abs
    buf.append_uint8(0);              // fault
    return buf.take();
}

// Helper: build a fake COMM_GET_IMU_DATA response
static std::vector<uint8_t> build_imu_response(double pitch, double roll) {
    vesc::Buffer buf;
    buf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::GetIMUData));
    buf.append_uint16(0x001F);
    buf.append_float32(roll, 1e6);    // roll
    buf.append_float32(pitch, 1e6);   // pitch
    buf.append_float32(0.0, 1e6);     // yaw
    buf.append_float32(0.0, 1e6);     // accel_x
    buf.append_float32(0.0, 1e6);     // accel_y
    buf.append_float32(-9.81, 1e6);   // accel_z
    buf.append_float32(0.0, 1e6);     // gyro_x
    buf.append_float32(0.0, 1e6);     // gyro_y
    buf.append_float32(0.0, 1e6);     // gyro_z
    buf.append_float32(0.25, 1e6);    // mag_x
    buf.append_float32(0.0, 1e6);     // mag_y
    buf.append_float32(-0.45, 1e6);   // mag_z
    buf.append_float32(1.0, 1e6);     // qw
    buf.append_float32(0.0, 1e6);     // qx
    buf.append_float32(0.0, 1e6);     // qy
    buf.append_float32(0.0, 1e6);     // qz
    return buf.take();
}

// Helper: build a fake COMM_DETECT_APPLY_ALL_FOC response
static std::vector<uint8_t> build_motor_detect_response(int16_t result) {
    vesc::Buffer buf;
    buf.append_uint8(static_cast<uint8_t>(vesc::CommPacketID::DetectApplyAllFOC));
    buf.append_int16(result);
    return buf.take();
}

// --- Full end-to-end wizard test ---
// Exercises every step with real protocol responses:
//   Express FW update → BMS FW update → VESC FW update →
//   Factory Reset → Refloat Install →
//   DetectFootpads → CalibrateIMU → DetectMotor →
//   ConfigureWheel → ConfigurePower (BMS) → Done
static void test_full_e2e_wizard() {
    nosedive::SetupBoard setup;

    std::vector<nosedive::SetupStep> steps_seen;
    std::vector<nosedive::StepPhase> phases_seen;
    std::vector<std::string> titles_seen;
    std::vector<std::vector<uint8_t>> sent;

    setup.set_state_callback([&](const nosedive::SetupState& s) {
        steps_seen.push_back(s.step);
        phases_seen.push_back(s.phase);
        titles_seen.push_back(s.title);
    });
    setup.set_send_callback([&](const std::vector<uint8_t>& p) {
        sent.push_back(p);
    });

    // Board with Express (CAN 253), BMS (CAN 10), no existing Refloat
    setup.can_device_ids = {10, 253};
    setup.main_fw = std::nullopt;
    setup.refloat_info = std::nullopt;
    auto pkg = make_test_package();
    setup.refloat_package = &pkg;

    // ========== START ==========
    setup.start();

    // --- 1. FWExpress: outdated → update ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWExpress),
              "e2e: at FWExpress");
    ASSERT(setup.state().title == "Express Firmware", "e2e: FWExpress title");

    auto express_fw = build_fw_response(6, 5, "VESC Express T", 0xE0);
    setup.handle_response(express_fw.data(), express_fw.size());
    do_update_cycle(setup, "e2e_express",
                    nosedive::SetupStep::FWExpress,
                    6, 6, "VESC Express T", 0xE0);

    // --- 2. FWBMS: outdated → update ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWBMS),
              "e2e: at FWBMS");
    ASSERT(setup.state().title == "BMS Firmware", "e2e: FWBMS title");

    auto bms_fw = build_fw_response(6, 5, "VESC BMS", 0xB0);
    setup.handle_response(bms_fw.data(), bms_fw.size());
    do_update_cycle(setup, "e2e_bms",
                    nosedive::SetupStep::FWBMS,
                    6, 6, "VESC BMS", 0xB0);

    // --- 3. FWVESC: outdated → update ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FWVESC),
              "e2e: at FWVESC");
    ASSERT(setup.state().title == "Controller Firmware", "e2e: FWVESC title");

    auto vesc_fw = build_fw_response(6, 5, "60_MK6", 0xA0);
    setup.handle_response(vesc_fw.data(), vesc_fw.size());
    do_update_cycle(setup, "e2e_vesc",
                    nosedive::SetupStep::FWVESC,
                    6, 6, "60_MK6", 0xA0);

    // --- 4. FactoryReset: user confirms ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::FactoryReset),
              "e2e: at FactoryReset");
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "e2e: FactoryReset at Prompt");
    ASSERT(setup.state().title == "Factory Reset", "e2e: FactoryReset title");

    setup.update();
    simulate_factory_reset(setup);

    // --- 5. InstallRefloat: not installed → user installs ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::InstallRefloat),
              "e2e: at InstallRefloat");
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "e2e: InstallRefloat at Prompt");
    ASSERT(setup.state().detail.find("not installed") != std::string::npos,
           "e2e: Refloat not installed");
    ASSERT(setup.state().title == "Refloat Package", "e2e: InstallRefloat title");

    setup.update();
    simulate_refloat_install(setup);

    // --- 6. DetectFootpads ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectFootpads),
              "e2e: at DetectFootpads");
    ASSERT(setup.state().title == "Footpad Sensors", "e2e: DetectFootpads title");

    auto values_resp = build_values_response(75.6);
    setup.handle_response(values_resp.data(), values_resp.size());

    // --- 7. CalibrateIMU ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::CalibrateIMU),
              "e2e: at CalibrateIMU");
    ASSERT(setup.state().title == "IMU Calibration", "e2e: CalibrateIMU title");

    auto imu_resp = build_imu_response(1.2, 0.5);
    setup.handle_response(imu_resp.data(), imu_resp.size());

    // --- 8. DetectMotor ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::DetectMotor),
              "e2e: at DetectMotor");
    ASSERT(setup.state().title == "Motor Detection", "e2e: DetectMotor title");

    auto motor_resp = build_motor_detect_response(0); // success
    setup.handle_response(motor_resp.data(), motor_resp.size());

    // --- 9. ConfigureWheel ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::ConfigureWheel),
              "e2e: at ConfigureWheel");
    ASSERT(setup.state().title == "Wheel Setup", "e2e: ConfigureWheel title");

    uint8_t mcconf_resp[] = {static_cast<uint8_t>(vesc::CommPacketID::GetMCConf)};
    setup.handle_response(mcconf_resp, 1);

    // --- 10. ConfigurePower (BMS present) ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::ConfigurePower),
              "e2e: at ConfigurePower");
    ASSERT(setup.state().title == "Battery Setup", "e2e: ConfigurePower title");

    // Should have sent ForwardCAN + BMSGetValues
    ASSERT(!sent.empty(), "e2e: sent BMS query");
    auto& last_sent = sent.back();
    ASSERT_EQ(last_sent[0], static_cast<uint8_t>(vesc::CommPacketID::ForwardCAN),
              "e2e: sent ForwardCAN for BMS");

    auto bms_resp = build_bms_response(20, 75.6, 0.0, 0.85);
    setup.handle_response(bms_resp.data(), bms_resp.size());

    // At Prompt with BMS info
    ASSERT_EQ(static_cast<uint8_t>(setup.state().phase),
              static_cast<uint8_t>(nosedive::StepPhase::Prompt),
              "e2e: ConfigurePower at Prompt");
    ASSERT(setup.state().detail.find("20S") != std::string::npos,
           "e2e: BMS detected 20S");

    // User adjusts to 21S, then confirms
    setup.set_cells(21);
    ASSERT(setup.state().detail.find("21S") != std::string::npos,
           "e2e: adjusted to 21S");

    // Change back to 20S and confirm
    setup.set_cells(20);
    sent.clear();
    setup.update();

    // Verify SetBatteryCut was sent
    bool sent_cut = false;
    for (auto& p : sent) {
        if (!p.empty() && p[0] == static_cast<uint8_t>(vesc::CommPacketID::SetBatteryCut))
            sent_cut = true;
    }
    ASSERT(sent_cut, "e2e: sent SetBatteryCut");

    // --- 11. Done ---
    ASSERT_EQ(static_cast<uint8_t>(setup.state().step),
              static_cast<uint8_t>(nosedive::SetupStep::Done),
              "e2e: at Done");
    ASSERT(setup.state().title == "Setup Complete", "e2e: Done title");
    ASSERT(!setup.is_running(), "e2e: wizard not running after Done");

    // Verify we hit every step
    auto saw_step = [&](nosedive::SetupStep s) {
        for (auto& st : steps_seen) if (st == s) return true;
        return false;
    };
    ASSERT(saw_step(nosedive::SetupStep::FWExpress), "e2e: saw FWExpress");
    ASSERT(saw_step(nosedive::SetupStep::FWBMS), "e2e: saw FWBMS");
    ASSERT(saw_step(nosedive::SetupStep::FWVESC), "e2e: saw FWVESC");
    ASSERT(saw_step(nosedive::SetupStep::FactoryReset), "e2e: saw FactoryReset");
    ASSERT(saw_step(nosedive::SetupStep::InstallRefloat), "e2e: saw InstallRefloat");
    ASSERT(saw_step(nosedive::SetupStep::DetectFootpads), "e2e: saw DetectFootpads");
    ASSERT(saw_step(nosedive::SetupStep::CalibrateIMU), "e2e: saw CalibrateIMU");
    ASSERT(saw_step(nosedive::SetupStep::DetectMotor), "e2e: saw DetectMotor");
    ASSERT(saw_step(nosedive::SetupStep::ConfigureWheel), "e2e: saw ConfigureWheel");
    ASSERT(saw_step(nosedive::SetupStep::ConfigurePower), "e2e: saw ConfigurePower");
    ASSERT(saw_step(nosedive::SetupStep::Done), "e2e: saw Done");

    // Verify every title was non-empty
    for (auto& t : titles_seen) {
        ASSERT(!t.empty(), "e2e: all titles non-empty");
    }
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
    test_refloat_install();
    test_refloat_install_chunked();
    test_refloat_version_check();
    test_refloat_version_comparison();
    test_factory_reset();
    test_configure_power_with_bms();
    test_configure_power_no_bms();
    test_bms_get_values_decode();
    test_cell_count_estimation();
    test_full_e2e_wizard();

    std::printf("\n%d/%d setup tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
