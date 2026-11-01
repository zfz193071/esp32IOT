// Compile-only integration fixture. NOT a production firmware/upload profile.
// No GPIO is configured. Placeholder Wi-Fi is rejected by ArduinoNetworkPort.
// Real NVS is read on startup if this fixture is ever run; no automatic writes.
#include <Arduino.h>
#include <Esp32Fan.h>

namespace {
class UnwiredOutput final : public esp32fan::OutputPort {
 public:
  void initializeOff() override {}
  void setFan(bool) override {}
  void setIndicator(bool) override {}
};
UnwiredOutput output;
esp32fan::Esp32SerialSink sink(Serial);
esp32fan::TextDiagnostics diagnostics(sink);
esp32fan::NvsConfigPersistence persistence;
esp32fan::ConfigStore config(persistence);
esp32fan::ArduinoNetworkPort radio("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
esp32fan::NetworkService network(radio);
esp32fan::Esp32SocketCloser closer;
esp32fan::SocketHttpTransport transport(closer);
esp32fan::NetworkHttpApi http(transport);
esp32fan::AppLoop* app = nullptr;
}

void setup() {
  // Entropy setup must precede any ADC/I2S/RF user, not run in global constructors.
  const auto& bootId = esp32fan::esp32BootId();
  Serial.begin(115200);
  Serial.println("ESP32_FAN_COMPILE_CHECK: no GPIO, placeholder Wi-Fi, not production firmware");
  // Exclusive UART ownership transfers to sink after the setup banner.
  sink.start();
  static esp32fan::AppLoop instance(bootId, config, output, diagnostics, 20,
                                   nullptr, &network, &http);
  app = &instance;
  app->start();
  if (!closer.start()) sink.tryWriteLine("Compile-check socket worker unavailable");
}

void loop() {
  if (app) app->step(millis(), false);
  delay(1);
}
