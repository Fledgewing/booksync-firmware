#!/usr/bin/env python3
"""Guard the #183 regression: no external worker stacks on flash/NVS paths."""
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[3]
class StackContract(unittest.TestCase):
    def test_library_worker_owns_internal_stack_until_task_deleted(self):
        source = (ROOT/'src/booksync/system/Worker.cpp').read_text()
        self.assertIn('makeInternalByteBufferNoThrow(stackBytes)', source)
        self.assertIn('xTaskCreateStatic(', source)
        self.assertNotIn('MALLOC_CAP_SPIRAM', source)
        deletion = source.index('vTaskDelete(task)')
        self.assertLess(source.index('eTaskGetState(task) != eSuspended'), deletion)
        self.assertIn('vTaskSuspend(nullptr)', source)
        self.assertGreater(source.index('stack.reset();', deletion), deletion)
        self.assertIn('uxTaskGetStackHighWaterMark(task)', source)
    def test_tailnet_nvs_worker_checks_internal_largest_before_create(self):
        source = (ROOT/'tailnet/shim/Tailnet.cpp').read_text()
        start = source.index('void post(Request r)')
        end = source.index('\n#else', start)
        create = source[start:end]
        self.assertRegex(create, r'largestInternal < TAILNET_WORKER_STACK \+ 8 \* 1024')
        self.assertIn('xTaskCreate(workerTask', create)
        self.assertNotRegex(create, r'xTaskCreate\w*WithCaps|MALLOC_CAP_SPIRAM')
    def test_wifi_uses_the_same_active_lease(self):
        source = (ROOT/'tailnet/shim/TailnetWifiMem.c').read_text()
        wrapper = source[source.index('esp_err_t __wrap_esp_wifi_init('):]
        self.assertLess(wrapper.index('!tailnet_service_permitted()'), wrapper.index('__real_esp_wifi_init('))
        self.assertIn('return ESP_ERR_INVALID_STATE;', wrapper)
    def test_logout_cannot_spawn_a_worker_while_reading(self):
        source = (ROOT/'tailnet/shim/Tailnet.cpp').read_text()
        begin = source.index('bool workerWanted(Request r)')
        end = source.index('void post(Request r)', begin)
        policy = source[begin:end]
        self.assertLess(policy.index('!tailnet_service_permitted()'), policy.index('r == REQ_LOGOUT'))
    def test_sleep_waits_for_network_before_display_or_peripheral_teardown(self):
        source = (ROOT/'src/main.cpp').read_text()
        begin = source.index('void enterDeepSleep(bool fromTimeout) {')
        end = source.index('gpio.stopButtonCapture()', begin)
        preparation = source[begin:end]
        self.assertIn('booksync::prepareSleep(', preparation)
        # Both pending and expired drains return before native teardown.
        self.assertRegex(preparation, r'SleepReadiness::Draining\)\s*\{[^}]*return;')
        self.assertRegex(preparation, r'SleepReadiness::Expired\)\s*\{[^}]*return;')
        bridge = (ROOT/'src/booksync/system/SleepRuntime.cpp').read_text()
        self.assertLess(bridge.index('activityManager.prepareForSleep()'),
                        bridge.index('networkAdmission().finish()'))
        self.assertIn('released && networkDisplayReady()', bridge)
        self.assertIn('step == SleepStep::Ready', bridge)
        self.assertIn('return SleepReadiness::Ready', bridge)
        loop = source[source.index('void loop() {'):]
        self.assertLess(loop.index('networkAdmissionTick()'), loop.index('enterDeepSleep(sleepWaitingFromTimeout)'))
    def test_reminder_visibility_pauses_for_the_entire_sleep_attempt(self):
        source = (ROOT/'src/main.cpp').read_text()
        sleep = source.split('void enterDeepSleep(bool fromTimeout) {', 1)[1].split('void setupDisplayAndFonts', 1)[0]
        self.assertLess(sleep.index('booksync::reminderHidden(true)'), sleep.index('booksync::prepareSleep('))
        expired = sleep.split('SleepReadiness::Expired)', 1)[1].split('return;', 1)[0]
        self.assertIn('booksync::reminderHidden(buttonShortcutController.isQuickLocked())', expired)
        returned = sleep.split('powerManager.startDeepSleep(gpio);', 1)[1]
        self.assertIn('booksync::reminderHidden(buttonShortcutController.isQuickLocked())', returned)
        pending = source.split('if (sleepWaitingForNetwork) {', 1)[1].split('enterDeepSleep(sleepWaitingFromTimeout)', 1)[0]
        self.assertIn('booksync::reminderHidden(buttonShortcutController.isQuickLocked())', pending)
        self.assertIn('booksync::reminderInput()', pending)

    def test_simulator_profiles_mirror_framebuffer_ownership(self):
        config = (ROOT/'platformio.ini').read_text()
        for profile in ('x4-pro-simulator','x4-classic-simulator'):
            block = config.split('[env:'+profile+']',1)[1].split('\n[',1)[0]
            self.assertIn('-DFREEINK_FB_PSRAM=1', block)
        c3 = config.split('[env:simulator-X3]',1)[1].split('\n[',1)[0]
        self.assertNotIn('FREEINK_FB_PSRAM=1', c3)
    def test_no_flash_worker_external_stack_api(self):
        for base in ('src/booksync/system', 'tailnet/shim'):
            for source in (ROOT/base).glob('*.cpp'):
                self.assertNotRegex(source.read_text(), r'xTaskCreate\w*WithCaps\([^;]*MALLOC_CAP_SPIRAM', str(source))
if __name__ == '__main__':
    unittest.main()
